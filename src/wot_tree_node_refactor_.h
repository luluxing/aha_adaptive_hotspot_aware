#ifndef WOT_TREE_NODE_REFACTOR_H
#define WOT_TREE_NODE_REFACTOR_H

#include <atomic>
#include <memory>
#include <vector>
#include <set>

#include "wot_types_refactor_.h"
#include "lsmt/lsmt.h"
#include "leveldb/include/env.h"
#include "leveldb/include/options.h"
#include "leveldb/dbformat.h"

namespace WOT_NAMESPACE {

using leveldb::Env;
using leveldb::Options;
using leveldb::InternalKeyComparator;
using leveldb::TableCache;
using leveldb::FileMetaData;

class Node {
public:
    uint32_t pg_id_;

    // Constructor for nodes with LSMT
    Node(BplusTree* tree, bool is_leaf, int level, Env* env, const std::string& dbname,
         std::atomic<uint64_t>& flush_id, const Options* opt, const InternalKeyComparator& icmp,
         TableCache* table_cache, LSMTStatus stat, int leaf_limit, bool allow_single = false);

    // Constructor for nodes without LSMT
    Node(BplusTree* tree, bool is_leaf, std::atomic<uint64_t>& flush_id,
         const InternalKeyComparator& icmp);

    ~Node();

    // No copying allowed
    Node(const Node&) = delete;
    void operator=(const Node&) = delete;

    // Display and debugging
    void NodePrint(int level);
    void NodePrintStat(int level);

    // Core operations
    SlicePageMap MaybeSplitOrFlush();
    void UpdatePivots(std::vector<SlicePageMap>* new_pivots,
                      std::vector<std::pair<Slice, uint32_t>>* old_pivots);

    // Buffer operations
    OpState BufferAddFiles(std::vector<std::shared_ptr<FileMetaData>>* files, Page p);
    OpState BufferAppendFiles(std::vector<std::shared_ptr<FileMetaData>>* files);
    void GetObsoleteFilesAndInstallNewBuffer(std::set<uint64_t>& files);
    void InstallNewBuffer();
    void InstallNewPage();
    
    void MayUpdateMinPivot(Page p, const Slice& k);
    Status BufferCompactTree(std::vector<Slice>*, std::vector<std::shared_ptr<FileMetaData>>*);
    Status BufferCompactTree(int*, std::vector<std::shared_ptr<FileMetaData>>*);
    void BufferCompactBottomLevel(Page page);
    void BufferCompactFilesInRange(int level,
                                   std::vector<std::shared_ptr<FileMetaData>>* files,
                                   const Page page);
    const std::vector<std::shared_ptr<FileMetaData>>* 
        BufferGetAndCompactBottomLevel(Page page);
    void BufferClear();
    void BufferClearAll();
    void SetBufferStatus(LSMTStatus status);
    void SetBufferLevelLimit(int level);
    void BufferClearBottomLevelFiles();
    void BufferFinalizeMergeLeaf(int level, std::vector<std::shared_ptr<FileMetaData>>*);

    // Access methods
    LevelDBLSMT* GetNodeLSMT() { return node_lsmt_; }

    // Split operations
    void FlushFilesToChildren(int level, std::vector<std::shared_ptr<FileMetaData>>* files,
                              std::vector<SlicePageMap>* new_pivots,
                              std::vector<std::pair<Slice, uint32_t>>* old_pivots,
                              bool one_level_only = false);
    SlicePageMap SplitSmallLeaf();
    SlicePageMap SplitNodeAndLSMT(std::vector<SlicePageMap>* new_pivots,
                                  std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    SlicePageMap SplitLeafAndLSMT();

    // Node state flags
    bool node_overflow_ = false;
    bool node_above_leaf = false;

    // Compaction operations
    SlicePageMap SplitNodeWithBuffer(std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    OpState BufferCompactTopLevel(Page page);
    int GetNodeLSMTFileSizeInLevel(int level);
    bool BufferTopLevelOverflows();
    
    void CompactChildren(SlicePageMap* children_need_compaction,
                         SlicePageMap* overflow_children);
    void FinalizeSubtree(Page this_page, int level_of_files,
                        std::vector<Node*>* tmp_nodes,
                        std::vector<std::shared_ptr<FileMetaData>>* flushed, int id);
    void AppendFilesToChild(uint32_t child_pg_id, std::vector<Node*>* tmp_nodes,
                           std::vector<std::shared_ptr<FileMetaData>>* remains, 
                           std::vector<std::shared_ptr<FileMetaData>>* flushed,
                           const Slice& pivot, SlicePageMap* overflow_children,
                           SlicePageMap* children_need_compaction);

    // File distribution operations
    void CompactAndFlushToChildren(std::vector<SlicePageMap>* new_pivots,
                                  std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    void DistributeFilesToNodes(std::vector<std::shared_ptr<FileMetaData>>* files,
                               std::vector<uint32_t>* new_pages,
                               std::vector<Slice>* guards,
                               std::vector<SlicePageMap>* new_pivots,
                               std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    SlicePageMap FlushFilesToChildrenNoSplit(int level, 
                                             std::vector<std::shared_ptr<FileMetaData>>* files);
    SlicePageMap FlushAndMergeSmallLeaf(uint32_t child, int level,
                                       std::vector<std::shared_ptr<FileMetaData>>* files);

private:
    BplusTree* tree_;
    const InternalKeyComparator* icmp_;
    std::atomic<uint64_t>& flush_id_;
    
    LevelDBLSMT* node_lsmt_;
    LevelDBLSMT* lsmt4compact_;

    Page extra_page_;
    std::string tmp_min_;

    friend class BplusTree;
    friend class UnsortedTreeIterator;
    friend class SortedTreeIterator;
};

} // namespace WOT_NAMESPACE

#endif // WOT_TREE_NODE_REFACTOR_H