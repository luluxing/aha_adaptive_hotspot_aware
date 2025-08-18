#include "wot_page_split_refactor_.h"
#include "wot_tree_node_refactor_.h"
#include "wot_btree_refactor_.h"
#include "wot_buffer_operations_refactor_.h"
#include "leveldb/table/merger.h"
#include "util/utils.h"

#ifdef BREAKDOWN
#include <x86intrin.h>
#define _rdtsc __rdtsc
#endif

namespace WOT_NAMESPACE {

int PageSplitOperations::EqualSplitInternalPage(
    BplusTree* tree, Page page,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    
    if (old_pivots->size() == 0) {
        return 1;
    }
    
    int new_entry_num = BufferUtils::CalculateNumber(new_pivots, old_pivots);
    int cur_entry_num = ((TreePageHeader) page)->item_num_;
    size_t raw = PAGE_SIZE - sizeof(TreePageHeaderData);
    size_t entry_size = old_pivots->at(0).first.size() + 3 * ITEMID_SIZE;
    int capacity = raw / entry_size;
    int total_entry_num = new_entry_num + cur_entry_num;
    int split_num = (total_entry_num + capacity - 1) / capacity;
    
    return split_num;
}

SlicePageMap PageSplitOperations::SplitNodeAndLSMT(
    Node* node,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    
    SlicePageMap result;
    int split_num = CalculateSplitNumber(node, nullptr, false, new_pivots, old_pivots);
    
    BplusTree* tree = node->tree_;
    Page this_page = tree->GetBufferManager()->Lookup(node->pg_id_);
    bool leaf_page = ((TreePageHeader) this_page)->is_leaf_;
    
    assert(split_num >= 2);
    std::vector<std::shared_ptr<FileMetaData>> out_file_names;
    Status s;
    
    if (leaf_page) {
        s = node->BufferCompactTree(&split_num, &out_file_names);
        BufferOperations::ClearBuffer(node);
    }

    std::vector<uint32_t> new_pages;
    CreateNewNodesForSplit(tree, split_num - 1, leaf_page, &new_pages,
                          leaf_page ? LSMTStatus::kLeaf : LSMTStatus::kBuffer,
                          tree->GetConfig().node_lsmt_level_limit);

    std::vector<Slice> guards;
    if (!leaf_page) {
        assert(node->extra_page_ == nullptr);
        node->extra_page_ = TreeSplitInternalPage(tree, node->pg_id_, new_pivots,
                                                 old_pivots, &new_pages, &guards,
                                                 &out_file_names);
    }
    
    if (leaf_page && out_file_names.size() == 1) {
        HandleSingleFileSplit(node, this_page, out_file_names, new_pages);
        return result;
    }
    
    DistributeFilesAfterSplit(tree, node, out_file_names, new_pages, 
                             leaf_page, split_num, result);
    
    return result;
}

SlicePageMap PageSplitOperations::SplitSmallLeaf(Node* node) {
    SlicePageMap result;
    std::vector<Iterator*> iter_vec;
    uint64_t entry_num = 0;
    
    if (node->GetNodeLSMT() && node->GetNodeLSMT()->GetBottomLevel() >= 0) {
        ReadOptions options;
        options.fill_cache = false;
        iter_vec.emplace_back(node->GetNodeLSMT()->NewMergedIterator(options));
        entry_num += node->GetNodeLSMT()->TotalEntryNum();
    }
    
    BplusTree* tree = node->tree_;
    Page this_page = tree->GetBufferManager()->Lookup(node->pg_id_);
    assert(((TreePageHeader) this_page)->item_num_ == 0);
    
    Iterator* leaf_iter = NewMergingIterator(
        &tree->GetInternalComparator(), &iter_vec[0], iter_vec.size());
    
    result = WriteToPage(tree, node->icmp_, leaf_iter, entry_num);
    BufferOperations::ClearAllBuffer(node);
    
    if (result.size() == 1) {
#ifdef BREAKDOWN
        uint64_t small_start = _rdtsc();
#endif
        tree->GetLockManager()->EscalateLock(node->pg_id_);
        Page new_page = tree->GetBufferManager()->Pin(result.begin()->second);
        memcpy(this_page, new_page, PAGE_SIZE);
        tree->GetBufferManager()->Delete(((TreePageHeader) new_page)->page_id_);
        
        ((TreePageHeader) this_page)->page_id_ = node->pg_id_;
        ((TreePageHeader) this_page)->is_leaf_ = true;
        ((TreePageHeader) this_page)->is_dirty_ = true;
        
        std::set<uint64_t> dead_files;
        BufferOperations::GetObsoleteFilesAndInstall(node, dead_files);
        
        if (node->GetNodeLSMT() != nullptr) delete node->GetNodeLSMT();
        node->node_lsmt_ = nullptr;
        result.clear();
        
        if (!tree->GetNodeTable().erase(node->pg_id_)) {
            fprintf(stdout, "Error: cannot erase in node_table: %d!", node->pg_id_);
        }
        
        tree->GetLockManager()->AlleviateLock(node->pg_id_);
#ifdef BREAKDOWN
        uint64_t small_end = _rdtsc();
        tree->GetWriterStats().split_small_leaf_ltime.fetch_add(small_end - small_start);
#endif
        LevelDBLSMT::RemoveFiles(tree->GetEnv(), tree->GetRootLSMTPath(), dead_files);
    } else {
        tree->GetNodesInProgress().push_back(node->pg_id_);
    }
    
    return result;
}

Page PageSplitOperations::TreeSplitInternalPage(
    BplusTree* tree, uint32_t pg_id,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots,
    std::vector<uint32_t>* new_pages,
    std::vector<Slice>* guards,
    std::vector<std::shared_ptr<FileMetaData>>* out_file_names) {
    
    SlicePageMap temp_pivots;
    Page page = tree->GetBufferManager()->Lookup(pg_id);
    uint32_t item_num = ((TreePageHeader) page)->item_num_;
    
    for (int i = 0; i < item_num; i++) {
        bool found = false;
        auto pivot = PageReadPivotAtOffset(page, i);
        for (auto const old_pair : *old_pivots) {
            if (pivot.compare(old_pair.first) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            temp_pivots[pivot] = PageReadChildAtOffset(page, i);
        }
    }
    
    for (auto new_pivot_map : *new_pivots) {
        temp_pivots.merge(new_pivot_map);
    }
    
    Page extra_page = DistributePivots(tree, pg_id, &temp_pivots, new_pages, guards);
    temp_pivots.clear();
    
    ReadTbb a;
    Node* node = nullptr;
    if (tree->GetNodeTable().find(a, pg_id)) {
        node = a->second;
    }
    a.release();
    
    if (node->GetNodeLSMT() != nullptr) {
        BufferOperations::CompactTree(node, guards, out_file_names);
        BufferOperations::ClearBuffer(node);
    }
    
    assert(!node->node_above_leaf);
    return extra_page;
}

Page PageSplitOperations::DistributePivots(BplusTree* tree, uint32_t pg_id, 
                                          SlicePageMap* temp_pivots,
                                          std::vector<uint32_t>* new_pages, 
                                          std::vector<Slice>* guards) {
    int new_pg_num = new_pages->size() + 1;
    int page_item_size = (temp_pivots->size() + new_pg_num - 1) / new_pg_num;
    auto it = temp_pivots->begin();
    int offset = 0;
    
    void* buf = malloc(PAGE_SIZE);
    memset(buf, 0, PAGE_SIZE);
    Page extra_one = (Page) buf;
    PageInit(extra_one, pg_id);
    
    while (it != temp_pivots->end()) {
        PageInsertIndexEntry(extra_one, it->first, it->second);
        if (offset == 0) {
            guards->emplace_back(PageReadPivotAtOffset(extra_one, 0));
        }
        offset++;
        it++;
        if (offset >= page_item_size) break;
    }
    
    offset = 0;
    int cur_page_idx = 0; 
    uint32_t cur_pg_id = new_pages->at(cur_page_idx);
    Page cur_page = tree->GetBufferManager()->Pin(cur_pg_id);
    
    while (it != temp_pivots->end()) {
        PageInsertIndexEntry(cur_page, it->first, it->second);
        if (offset == 0) {
            guards->emplace_back(GetPivotAtOffset(tree->GetBufferManager(), 
                                                 cur_pg_id, tree->GetArenaWrapper(), 0));
        }
        offset++;
        if (offset >= page_item_size && cur_page_idx + 1 < new_pages->size()) {
            cur_page_idx++;
            tree->GetBufferManager()->Unpin(cur_pg_id);
            cur_pg_id = new_pages->at(cur_page_idx);
            cur_page = tree->GetBufferManager()->Pin(cur_pg_id);
            offset = 0;
        }
        it++;
    }
    tree->GetBufferManager()->Unpin(cur_pg_id);
    return extra_one;
}

void PageSplitOperations::UpdateOrRewritePivots(
    BplusTree* tree, uint32_t pg_id, 
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    
    SlicePageMap tmp;
    for (size_t i = 0; i < old_pivots->size(); i++) {
        auto it = new_pivots->at(i).begin();
        assert(old_pivots->at(i).first.compare(it->first) == 0);
        
        uint32_t old_child = old_pivots->at(i).second;
        if (old_child != it->second) {
            UpdateChildbyPivot(tree->GetBufferManager(), pg_id, it->first, it->second);
        }
        it++;
        for (; it != new_pivots->at(i).end(); it++) {
            tmp[it->first] = it->second;
        }
    }
    AddNewPivots(tree->GetBufferManager(), pg_id, &tmp);
}

// Private helper methods
int PageSplitOperations::CalculateSplitNumber(Node* node, Page page, bool leaf_page,
                                             std::vector<SlicePageMap>* new_pivots,
                                             std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    int split_num = 2;
    BplusTree* tree = node->tree_;
    Page this_page = page ? page : tree->GetBufferManager()->Lookup(node->pg_id_);
    
    if (!leaf_page) {
        split_num = EqualSplitInternalPage(tree, this_page, new_pivots, old_pivots);
        split_num = split_num < 2 ? 2 : split_num;
    } else {
        assert(node->GetNodeLSMT() != nullptr);
        uint64_t total_size = node->GetNodeLSMT()->TotalFileSizeLSMT();
        int alloc_size = node->GetNodeLSMT()->output_file_size;
        split_num = static_cast<int>(static_cast<double>(total_size) / alloc_size + 1);
        split_num = split_num < 2 ? 2 : split_num;
    }
    
    return split_num;
}

void PageSplitOperations::CreateNewNodesForSplit(BplusTree* tree, int split_num, 
                                                 bool leaf_page,
                                                 std::vector<uint32_t>* new_pages,
                                                 LSMTStatus lsmt_status, 
                                                 uint32_t lsmt_level_lim) {
    for (int i = 0; i < split_num; i++) {
        uint32_t p_id = tree->GetBufferManager()->Allocate();
        Page p = tree->GetBufferManager()->Pin(p_id);
        ((TreePageHeader) p)->is_leaf_ = leaf_page;
        ((TreePageHeader) p)->is_dirty_ = true;
        new_pages->push_back(((TreePageHeader) p)->page_id_);
        tree->GetBufferManager()->Unpin(p_id);
    }
}

void PageSplitOperations::HandleSingleFileSplit(Node* node, Page this_page,
                                               std::vector<std::shared_ptr<FileMetaData>>& out_file_names,
                                               std::vector<uint32_t>& new_pages) {
#ifdef BREAKDOWN
    uint64_t split_start = _rdtsc();
#endif
    BplusTree* tree = node->tree_;
    tree->GetLockManager()->EscalateLock(node->pg_id_);
    BufferOperations::AddFilesToBuffer(node, &out_file_names, this_page);
    
    std::set<uint64_t> dead_files;
    BufferOperations::GetObsoleteFilesAndInstall(node, dead_files);
    
    tree->GetLockManager()->AlleviateLock(node->pg_id_);
    LevelDBLSMT::RemoveFiles(tree->GetEnv(), tree->GetRootLSMTPath(), dead_files);
    
    for (auto const& page : new_pages) {
        tree->GetBufferManager()->Delete(page);
    }
    out_file_names.clear();
    
#ifdef BREAKDOWN
    uint64_t split_end = _rdtsc();
    tree->GetWriterStats().split_ltime.fetch_add(split_end - split_start);
#endif
}

// PageSplitPolicy implementations
int EqualSplitPolicy::DetermineSplitNumber(size_t total_size, size_t target_size) {
    if (target_size == 0) return 2;
    int split_num = static_cast<int>((total_size + target_size - 1) / target_size);
    return split_num < 2 ? 2 : split_num;
}

bool EqualSplitPolicy::ShouldSplit(size_t current_size, size_t threshold) {
    return current_size > threshold;
}

void EqualSplitPolicy::ConfigureSplitParameters(size_t& file_size_threshold,
                                               size_t& item_count_threshold) {
    file_size_threshold = 1024 * 1024; // 1MB default
    item_count_threshold = 1000;       // 1000 items default
}

AdaptiveSplitPolicy::AdaptiveSplitPolicy(double growth_factor) 
    : growth_factor_(growth_factor) {
}

int AdaptiveSplitPolicy::DetermineSplitNumber(size_t total_size, size_t target_size) {
    if (target_size == 0) return 2;
    
    // Use growth factor to adjust target size
    size_t adjusted_target = static_cast<size_t>(target_size * growth_factor_);
    int split_num = static_cast<int>((total_size + adjusted_target - 1) / adjusted_target);
    return split_num < 2 ? 2 : split_num;
}

bool AdaptiveSplitPolicy::ShouldSplit(size_t current_size, size_t threshold) {
    // More conservative splitting with adaptive policy
    return current_size > static_cast<size_t>(threshold * growth_factor_);
}

void AdaptiveSplitPolicy::ConfigureSplitParameters(size_t& file_size_threshold,
                                                  size_t& item_count_threshold) {
    file_size_threshold = static_cast<size_t>(2 * 1024 * 1024 * growth_factor_); // Adjusted
    item_count_threshold = static_cast<size_t>(1500 * growth_factor_);           // Adjusted
}

std::unique_ptr<PageSplitPolicy> PageSplitPolicyFactory::CreatePolicy(int policy_type) {
    switch (policy_type) {
        case EQUAL_SPLIT:
            return std::make_unique<EqualSplitPolicy>();
        case ADAPTIVE_SPLIT:
            return std::make_unique<AdaptiveSplitPolicy>();
        default:
            return std::make_unique<EqualSplitPolicy>();
    }
}

} // namespace WOT_NAMESPACE