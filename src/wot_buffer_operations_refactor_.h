#ifndef WOT_BUFFER_OPERATIONS_REFACTOR_H
#define WOT_BUFFER_OPERATIONS_REFACTOR_H

#include <vector>
#include <memory>
#include <set>

#include "wot_types_refactor_.h"
#include "leveldb/include/status.h"
#include "leveldb/dbformat.h"

namespace WOT_NAMESPACE {

using leveldb::Status;
using leveldb::FileMetaData;

class BufferOperations {
public:
    BufferOperations() = default;
    ~BufferOperations() = default;

    // File management operations
    static OpState AddFilesToBuffer(Node* node, 
                                   std::vector<std::shared_ptr<FileMetaData>>* files, 
                                   Page page);
    
    static OpState AppendFilesToBuffer(Node* node,
                                      std::vector<std::shared_ptr<FileMetaData>>* files);
    
    static void InstallNewBuffer(Node* node);
    static void InstallNewPage(Node* node);
    
    // Compaction operations
    static OpState CompactTopLevel(Node* node, Page page);
    static Status CompactTree(Node* node, std::vector<Slice>* guards,
                             std::vector<std::shared_ptr<FileMetaData>>* outfs);
    static Status CompactTree(Node* node, int* split_num,
                             std::vector<std::shared_ptr<FileMetaData>>* outfs);
    
    static void CompactBottomLevel(Node* node, Page page);
    static void CompactFilesInRange(Node* node, int level,
                                   std::vector<std::shared_ptr<FileMetaData>>* files,
                                   const Page page);
    
    // Buffer state management
    static void ClearBuffer(Node* node);
    static void ClearAllBuffer(Node* node);
    static void ClearBottomLevelFiles(Node* node);
    
    static void SetBufferStatus(Node* node, LSMTStatus status);
    static void SetBufferLevelLimit(Node* node, int level);
    
    // File retrieval and cleanup
    static const std::vector<std::shared_ptr<FileMetaData>>* 
        GetAndCompactBottomLevel(Node* node, Page page);
    
    static void GetObsoleteFilesAndInstall(Node* node, std::set<uint64_t>& files);
    static void FinalizeMergeLeaf(Node* node, int level_of_files,
                                 std::vector<std::shared_ptr<FileMetaData>>* files);
    
    // Query operations
    static bool TopLevelOverflows(Node* node);
    static int GetFileSizeInLevel(Node* node, int level);
    
    // Pivot and key management
    static void UpdateMinPivot(Node* node, Page page, const Slice& key);
    
private:
    // Helper methods
    static void EnsureCompactionBuffer(Node* node);
    static void CleanupCompactionBuffer(Node* node);
};

class BufferTreeOperations {
public:
    // High-level tree buffer operations
    static SlicePageMap FlushFilesToChildren(Node* node, int level_of_files,
                                            std::vector<std::shared_ptr<FileMetaData>>* files,
                                            std::vector<SlicePageMap>* new_pivots,
                                            std::vector<std::pair<Slice, uint32_t>>* old_pivots,
                                            bool one_level_only = false);
    
    static SlicePageMap FlushFilesToChildrenNoSplit(Node* node, int level_of_files,
                                                   std::vector<std::shared_ptr<FileMetaData>>* files);
    
    static SlicePageMap FlushAndMergeSmallLeaf(Node* node, uint32_t child_id, int level_of_files,
                                              std::vector<std::shared_ptr<FileMetaData>>* files);
    
    // Child node operations
    static void AppendFilesToChild(Node* parent_node, uint32_t child_pg_id, 
                                  std::vector<Node*>* tmp_nodes,
                                  std::vector<std::shared_ptr<FileMetaData>>* remains, 
                                  std::vector<std::shared_ptr<FileMetaData>>* flushed,
                                  const Slice& pivot, SlicePageMap* overflow_children,
                                  SlicePageMap* children_need_compaction);
    
    static void CompactChildren(Node* node, SlicePageMap* children_need_compaction,
                               SlicePageMap* overflow_children);
    
    static void FinalizeSubtree(Node* node, Page this_page, int level_of_files,
                               std::vector<Node*>* tmp_nodes,
                               std::vector<std::shared_ptr<FileMetaData>>* flushed,
                               int locked_node_id);
    
    // File distribution
    static void DistributeFilesToNodes(Node* node, 
                                      std::vector<std::shared_ptr<FileMetaData>>* files,
                                      std::vector<uint32_t>* new_pages,
                                      std::vector<Slice>* guards,
                                      std::vector<SlicePageMap>* new_pivots,
                                      std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    // Compaction strategies
    static void CompactAndFlushToChildren(Node* node, 
                                         std::vector<SlicePageMap>* new_pivots,
                                         std::vector<std::pair<Slice, uint32_t>>* old_pivots);
};

// Utility functions for buffer calculations
namespace BufferUtils {
    size_t CalculateNewSize(std::vector<SlicePageMap>* new_pivots,
                           std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    size_t CalculateNumber(std::vector<SlicePageMap>* new_pivots,
                          std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    bool FileAcrossPivots(BufferManager* buffer_manager, uint32_t page_id,
                         std::vector<std::shared_ptr<FileMetaData>>* files,
                         const Comparator* comparator);
}

} // namespace WOT_NAMESPACE

#endif // WOT_BUFFER_OPERATIONS_REFACTOR_H