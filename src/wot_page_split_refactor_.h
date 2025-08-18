#ifndef WOT_PAGE_SPLIT_REFACTOR_H
#define WOT_PAGE_SPLIT_REFACTOR_H

#include <vector>
#include <memory>

#include "wot_types_refactor_.h"
#include "leveldb/include/status.h"
#include "leveldb/dbformat.h"

namespace WOT_NAMESPACE {

using leveldb::Status;
using leveldb::FileMetaData;

class PageSplitOperations {
public:
    PageSplitOperations() = default;
    ~PageSplitOperations() = default;

    // Node splitting operations
    static SlicePageMap SplitNodeAndLSMT(Node* node,
                                        std::vector<SlicePageMap>* new_pivots,
                                        std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    static SlicePageMap SplitSmallLeaf(Node* node);
    
    static SlicePageMap SplitLeafAndLSMT(Node* node);
    
    static SlicePageMap SplitNodeWithBuffer(Node* node,
                                           std::vector<SlicePageMap>* new_pivots,
                                           std::vector<std::pair<Slice, uint32_t>>* old_pivots);

    // Page management during splits
    static Page TreeSplitInternalPage(BplusTree* tree, uint32_t pg_id,
                                     std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice, uint32_t>>* old_pivots,
                                     std::vector<uint32_t>* new_pages,
                                     std::vector<Slice>* guards,
                                     std::vector<std::shared_ptr<FileMetaData>>* out_file_names);
    
    static Page DistributePivots(BplusTree* tree, uint32_t pg_id, SlicePageMap* temp_pivots,
                                std::vector<uint32_t>* new_pages, std::vector<Slice>* guards);

    // Split calculations and policies
    static int EqualSplitInternalPage(BplusTree* tree, Page page,
                                     std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    // Pivot management during splits
    static void UpdateOrRewritePivots(BplusTree* tree, uint32_t pg_id, 
                                     std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    static void UpdatePivots(Node* node, std::vector<SlicePageMap>* new_pivots,
                            std::vector<std::pair<Slice, uint32_t>>* old_pivots);

    // Node creation and setup during splits
    static void CreateNewNodesForSplit(BplusTree* tree, int split_num, bool leaf_page,
                                      std::vector<uint32_t>* new_pages,
                                      LSMTStatus lsmt_status, uint32_t lsmt_level_lim);
    
    // File distribution during splits
    static void DistributeFilesAfterSplit(BplusTree* tree, Node* node,
                                          std::vector<std::shared_ptr<FileMetaData>>& out_file_names,
                                          std::vector<uint32_t>& new_pages,
                                          bool leaf_page, int split_num,
                                          SlicePageMap& result);

private:
    // Helper methods for split operations
    static int CalculateSplitNumber(Node* node, Page page, bool leaf_page,
                                   std::vector<SlicePageMap>* new_pivots,
                                   std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    static void SetupNewNodeAfterSplit(BplusTree* tree, uint32_t new_page_id,
                                      bool leaf_page, LSMTStatus lsmt_status,
                                      uint32_t lsmt_level_lim);
    
    static void HandleSingleFileSplit(Node* node, Page this_page,
                                     std::vector<std::shared_ptr<FileMetaData>>& out_file_names,
                                     std::vector<uint32_t>& new_pages);
};

class PageSplitPolicy {
public:
    virtual ~PageSplitPolicy() = default;
    
    virtual int DetermineSplitNumber(size_t total_size, size_t target_size) = 0;
    virtual bool ShouldSplit(size_t current_size, size_t threshold) = 0;
    virtual void ConfigureSplitParameters(size_t& file_size_threshold,
                                         size_t& item_count_threshold) = 0;
};

class EqualSplitPolicy : public PageSplitPolicy {
public:
    EqualSplitPolicy() = default;
    virtual ~EqualSplitPolicy() = default;
    
    int DetermineSplitNumber(size_t total_size, size_t target_size) override;
    bool ShouldSplit(size_t current_size, size_t threshold) override;
    void ConfigureSplitParameters(size_t& file_size_threshold,
                                 size_t& item_count_threshold) override;
};

class AdaptiveSplitPolicy : public PageSplitPolicy {
public:
    explicit AdaptiveSplitPolicy(double growth_factor = 1.5);
    virtual ~AdaptiveSplitPolicy() = default;
    
    int DetermineSplitNumber(size_t total_size, size_t target_size) override;
    bool ShouldSplit(size_t current_size, size_t threshold) override;
    void ConfigureSplitParameters(size_t& file_size_threshold,
                                 size_t& item_count_threshold) override;

private:
    double growth_factor_;
};

class PageSplitPolicyFactory {
public:
    static std::unique_ptr<PageSplitPolicy> CreatePolicy(int policy_type);
    
    enum PolicyType {
        EQUAL_SPLIT = 1,
        ADAPTIVE_SPLIT = 2,
        CUSTOM_SPLIT = 3
    };
};

} // namespace WOT_NAMESPACE

#endif // WOT_PAGE_SPLIT_REFACTOR_H