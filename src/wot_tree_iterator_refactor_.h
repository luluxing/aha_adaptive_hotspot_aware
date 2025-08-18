#ifndef WOT_TREE_ITERATOR_REFACTOR_H
#define WOT_TREE_ITERATOR_REFACTOR_H

#include <string>
#include <vector>

#include "wot_types_refactor_.h"
#include "leveldb/include/iterator.h"
#include "leveldb/include/comparator.h"
#include "leveldb/dbformat.h"
#include "util/random.h"

namespace WOT_NAMESPACE {

using leveldb::Iterator;
using leveldb::Comparator;
using leveldb::InternalKeyComparator;
using leveldb::ParsedInternalKey;
using leveldb::Random;

class SortedTreeIterator {
public:
    SortedTreeIterator(BplusTree* tree, uint32_t seed,
                      const Comparator* cmp,
                      const InternalKeyComparator& internal_comparator);

    // No copying allowed
    SortedTreeIterator(const SortedTreeIterator&) = delete;
    SortedTreeIterator& operator=(const SortedTreeIterator&) = delete;

    ~SortedTreeIterator();

    // Iterator interface
    void Seek(const Slice& target);
    void Next();
    bool Valid();
    std::string key();
    std::string value();

    // Range operations
    void SeekBothEnds(const Slice& low, const Slice& up);

private:
    IterState iter_state_;
    BplusTree* tree_;
    const Comparator* const user_comparator_;

    bool valid_;
    Random rnd_;
    size_t bytes_until_read_sampling_;

    Iterator* inner_iter_;
    std::string saved_key_;
    std::vector<int> locked_nodes_;
    bool all_page_ = false;

    const InternalKeyComparator internal_comparator_;
    uint64_t life_starts_;

    // Helper methods
    void InitInnerIter(const Slice& low, const Slice& up);
    void FindNextUserEntry(bool skipping, std::string* skip);
    
    inline bool ParseKey(ParsedInternalKey* ikey);
    inline void SaveKey(const Slice& k, std::string* dst);
    size_t RandomCompactionPeriod();
};

class PageIterator : public Iterator {
public:
    explicit PageIterator(BplusTree* tree);
    virtual ~PageIterator();

    // Iterator interface implementation
    virtual bool Valid() const override;
    virtual void SeekToFirst() override;
    virtual void SeekToLast() override;
    virtual void Seek(const Slice& target) override;
    virtual void Next() override;
    virtual void Prev() override;
    virtual Slice key() const override;
    virtual Slice value() const override;
    virtual Status status() const override;

    // Page-specific methods
    void AddPage(uint32_t page_id);
    void Reset();

private:
    BplusTree* tree_;
    std::vector<uint32_t> page_ids_;
    size_t current_page_index_;
    size_t current_item_index_;
    bool valid_;
    Status status_;
    std::string current_key_;
    std::string current_value_;

    void LoadCurrentItem();
    void AdvanceToNextValidItem();
};

} // namespace WOT_NAMESPACE

#endif // WOT_TREE_ITERATOR_REFACTOR_H