#include "wot_tree_iterator_refactor_.h"
#include "wot_btree_refactor_.h"
#include "leveldb/table/merger.h"
#include "leveldb/write_batch_internal.h"
#include "leveldb/memtable.h"
#include "util/coding.h"

#ifdef BREAKDOWN
#include <x86intrin.h>
#define _rdtsc __rdtsc
#endif

namespace WOT_NAMESPACE {

SortedTreeIterator::SortedTreeIterator(BplusTree* tree, uint32_t seed,
                                      const Comparator* cmp,
                                      const InternalKeyComparator& internal_comparator)
    : iter_state_(IterState::kInvalid),
      tree_(tree),
      user_comparator_(cmp),
      valid_(false),
      rnd_(seed),
      bytes_until_read_sampling_(RandomCompactionPeriod()),
      internal_comparator_(internal_comparator),
      inner_iter_(nullptr) {
#ifdef BREAKDOWN
    life_starts_ = _rdtsc();
#endif
}

SortedTreeIterator::~SortedTreeIterator() {
#ifdef BREAKDOWN
    uint64_t life_ends = _rdtsc();
    tree_->GetReaderStats().reader_time.fetch_add(life_ends - life_starts_);
    tree_->GetReaderStats().reader_cnt.fetch_add(1);
#endif
    for (auto const& pg_id : locked_nodes_) {
        tree_->GetLockManager()->ReadUnlock(pg_id);
    }
    if (inner_iter_ != nullptr) {
        delete inner_iter_;
    }
}

void SortedTreeIterator::InitInnerIter(const Slice& low, const Slice& up) {
    MemTable* mem = nullptr;
    MemTable* imm = nullptr;
    std::vector<Iterator*> iter_vec;
    int non_page_num = 0;
    
    tree_->GetBTreeState()->NewIterator(tree_, &iter_vec, low, up, &mem,
                                       &imm, &locked_nodes_,
                                       user_comparator_, &non_page_num);
    
    inner_iter_ = NewMergingIterator(&internal_comparator_, &iter_vec[0],
                                    iter_vec.size());
    
    if (non_page_num == 0) {
        all_page_ = true;
    }
    
    // Set up cleanup for mem/imm tables
    // Note: This would need proper cleanup registration in a real implementation
}

void SortedTreeIterator::SeekBothEnds(const Slice& low, const Slice& up) {
    InitInnerIter(low, up);
    saved_key_.clear();
    
    // Create internal key for seeking
    std::string seek_key;
    AppendInternalKey(&seek_key,
                     ParsedInternalKey(low, tree_->GetSequenceNumber(), kValueTypeForSeek));
    
    inner_iter_->Seek(seek_key);
    valid_ = true;
    
    if (inner_iter_->Valid()) {
        FindNextUserEntry(false, &saved_key_);
    } else {
        valid_ = false;
    }
}

void SortedTreeIterator::FindNextUserEntry(bool skipping, std::string* skip) {
    assert(inner_iter_->Valid());
    do {
        ParsedInternalKey ikey;
        if (ParseKey(&ikey) && ikey.sequence <= tree_->GetSequenceNumber()) {
            switch (ikey.type) {
                case kTypeDeletion:
                    SaveKey(ikey.user_key, skip);
                    skipping = true;
                    break;
                case kTypeValue:
                    if (skipping &&
                        user_comparator_->Compare(ikey.user_key, *skip) <= 0) {
                        // Entry hidden
                    } else {
                        valid_ = true;
                        saved_key_.clear();
                        return;
                    }
                    break;
            }
        }
        inner_iter_->Next();
    } while (inner_iter_->Valid());
    
    saved_key_.clear();
    valid_ = false;
}

void SortedTreeIterator::Next() {
    assert(valid_);
    if (all_page_) {
        inner_iter_->Next();
        if (!inner_iter_->Valid()) {
            valid_ = false;
        }
        return;
    }
    
    SaveKey(ExtractUserKey(inner_iter_->key()), &saved_key_);
    inner_iter_->Next();
    if (!inner_iter_->Valid()) {
        valid_ = false;
        saved_key_.clear();
        return;
    }

    FindNextUserEntry(true, &saved_key_);
}

bool SortedTreeIterator::Valid() { 
    return valid_; 
}

std::string SortedTreeIterator::key() {
    assert(valid_);
    return ExtractUserKey(inner_iter_->key()).ToString();
}

std::string SortedTreeIterator::value() {
    assert(valid_);
    return inner_iter_->value().ToString();
}

void SortedTreeIterator::Seek(const Slice& target) {
    // Implementation for single-point seek
    // This would be similar to SeekBothEnds but for a single target
}

inline bool SortedTreeIterator::ParseKey(ParsedInternalKey* ikey) {
    Slice k = inner_iter_->key();

    size_t bytes_read = k.size() + inner_iter_->value().size();
    while (bytes_until_read_sampling_ < bytes_read) {
        bytes_until_read_sampling_ += RandomCompactionPeriod();
        tree_->RecordReadSample(k);
    }
    assert(bytes_until_read_sampling_ >= bytes_read);
    bytes_until_read_sampling_ -= bytes_read;

    if (!ParseInternalKey(k, ikey)) {
        return false;
    } else {
        return true;
    }
}

inline void SortedTreeIterator::SaveKey(const Slice& k, std::string* dst) {
    dst->assign(k.data(), k.size());
}

size_t SortedTreeIterator::RandomCompactionPeriod() {
    return rnd_.Uniform(2 * config::kReadBytesPeriod);
}

// PageIterator implementation
PageIterator::PageIterator(BplusTree* tree)
    : tree_(tree),
      current_page_index_(0),
      current_item_index_(0),
      valid_(false) {
}

PageIterator::~PageIterator() {
}

void PageIterator::AddPage(uint32_t page_id) {
    page_ids_.push_back(page_id);
}

void PageIterator::Reset() {
    page_ids_.clear();
    current_page_index_ = 0;
    current_item_index_ = 0;
    valid_ = false;
}

bool PageIterator::Valid() const {
    return valid_;
}

void PageIterator::SeekToFirst() {
    current_page_index_ = 0;
    current_item_index_ = 0;
    if (!page_ids_.empty()) {
        LoadCurrentItem();
        AdvanceToNextValidItem();
    } else {
        valid_ = false;
    }
}

void PageIterator::SeekToLast() {
    // Implementation for seeking to last item
    if (!page_ids_.empty()) {
        current_page_index_ = page_ids_.size() - 1;
        // Need to find the last item in the last page
        // This would require page introspection
    }
    valid_ = false; // Simplified for now
}

void PageIterator::Seek(const Slice& target) {
    // Implementation for seeking to specific key
    // Would need to search across pages
    valid_ = false; // Simplified for now
}

void PageIterator::Next() {
    if (!valid_) return;
    
    current_item_index_++;
    AdvanceToNextValidItem();
}

void PageIterator::Prev() {
    // Implementation for moving to previous item
    valid_ = false; // Simplified for now
}

Slice PageIterator::key() const {
    return Slice(current_key_);
}

Slice PageIterator::value() const {
    return Slice(current_value_);
}

Status PageIterator::status() const {
    return status_;
}

void PageIterator::LoadCurrentItem() {
    if (current_page_index_ >= page_ids_.size()) {
        valid_ = false;
        return;
    }

    uint32_t page_id = page_ids_[current_page_index_];
    Page page = tree_->GetBufferManager()->Pin(page_id);
    
    if (current_item_index_ >= ((TreePageHeader)page)->item_num_) {
        tree_->GetBufferManager()->Unpin(page_id);
        valid_ = false;
        return;
    }

    // Extract key and value from page
    // This would depend on page format
    // Simplified implementation
    current_key_ = "key"; // Placeholder
    current_value_ = "value"; // Placeholder
    
    tree_->GetBufferManager()->Unpin(page_id);
    valid_ = true;
}

void PageIterator::AdvanceToNextValidItem() {
    while (current_page_index_ < page_ids_.size()) {
        uint32_t page_id = page_ids_[current_page_index_];
        Page page = tree_->GetBufferManager()->Pin(page_id);
        
        if (current_item_index_ < ((TreePageHeader)page)->item_num_) {
            tree_->GetBufferManager()->Unpin(page_id);
            LoadCurrentItem();
            return;
        }
        
        tree_->GetBufferManager()->Unpin(page_id);
        current_page_index_++;
        current_item_index_ = 0;
    }
    
    valid_ = false;
}

} // namespace WOT_NAMESPACE