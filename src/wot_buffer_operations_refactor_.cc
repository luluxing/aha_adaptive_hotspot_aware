#include "wot_buffer_operations_refactor_.h"
#include "wot_tree_node_refactor_.h"
#include "wot_btree_refactor_.h"
#include "leveldb/table/merger.h"
#include "util/utils.h"

namespace WOT_NAMESPACE {

namespace BufferUtils {

size_t CalculateNewSize(std::vector<SlicePageMap>* new_pivots,
                       std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    size_t result = 0;
    for (int i = 0; i < old_pivots->size(); i++) {
        for (auto const& m : (*new_pivots)[i]) {
            // Store key_size, key, child page, item_id
            result += m.first.size() + 3 * ITEMID_SIZE;
        }
        // TODO: need to remove the actual char* array of the old key
        result -= ITEMID_SIZE;  
    }
    return result;
}

size_t CalculateNumber(std::vector<SlicePageMap>* new_pivots,
                      std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    int result = 0;
    for (int i = 0; i < new_pivots->size(); i++) {
        for (auto const& m : (*new_pivots)[i]) {
            result++;
        }
        if (old_pivots->size() > 0) result--;
    }
    return result;
}

bool FileAcrossPivots(BufferManager* buffer_manager, uint32_t page_id,
                     std::vector<std::shared_ptr<FileMetaData>>* files,
                     const Comparator* comparator) {
    // Implementation would check if files span across page pivots
    // This is a simplified stub
    return false;
}

} // namespace BufferUtils

// BufferOperations implementation
OpState BufferOperations::AddFilesToBuffer(Node* node, 
                                          std::vector<std::shared_ptr<FileMetaData>>* files, 
                                          Page page) {
    return node->BufferAddFiles(files, page);
}

OpState BufferOperations::AppendFilesToBuffer(Node* node,
                                             std::vector<std::shared_ptr<FileMetaData>>* files) {
    return node->BufferAppendFiles(files);
}

void BufferOperations::InstallNewBuffer(Node* node) {
    node->InstallNewBuffer();
}

void BufferOperations::InstallNewPage(Node* node) {
    node->InstallNewPage();
}

OpState BufferOperations::CompactTopLevel(Node* node, Page page) {
    return node->BufferCompactTopLevel(page);
}

Status BufferOperations::CompactTree(Node* node, std::vector<Slice>* guards,
                                    std::vector<std::shared_ptr<FileMetaData>>* outfs) {
    return node->BufferCompactTree(guards, outfs);
}

Status BufferOperations::CompactTree(Node* node, int* split_num,
                                    std::vector<std::shared_ptr<FileMetaData>>* outfs) {
    return node->BufferCompactTree(split_num, outfs);
}

void BufferOperations::CompactBottomLevel(Node* node, Page page) {
    node->BufferCompactBottomLevel(page);
}

void BufferOperations::CompactFilesInRange(Node* node, int level,
                                          std::vector<std::shared_ptr<FileMetaData>>* files,
                                          const Page page) {
    node->BufferCompactFilesInRange(level, files, page);
}

void BufferOperations::ClearBuffer(Node* node) {
    node->BufferClear();
}

void BufferOperations::ClearAllBuffer(Node* node) {
    node->BufferClearAll();
}

void BufferOperations::ClearBottomLevelFiles(Node* node) {
    node->BufferClearBottomLevelFiles();
}

void BufferOperations::SetBufferStatus(Node* node, LSMTStatus status) {
    node->SetBufferStatus(status);
}

void BufferOperations::SetBufferLevelLimit(Node* node, int level) {
    node->SetBufferLevelLimit(level);
}

const std::vector<std::shared_ptr<FileMetaData>>* 
BufferOperations::GetAndCompactBottomLevel(Node* node, Page page) {
    return node->BufferGetAndCompactBottomLevel(page);
}

void BufferOperations::GetObsoleteFilesAndInstall(Node* node, std::set<uint64_t>& files) {
    node->GetObsoleteFilesAndInstallNewBuffer(files);
}

void BufferOperations::FinalizeMergeLeaf(Node* node, int level_of_files,
                                        std::vector<std::shared_ptr<FileMetaData>>* files) {
    node->BufferFinalizeMergeLeaf(level_of_files, files);
}

bool BufferOperations::TopLevelOverflows(Node* node) {
    return node->BufferTopLevelOverflows();
}

int BufferOperations::GetFileSizeInLevel(Node* node, int level) {
    return node->GetNodeLSMTFileSizeInLevel(level);
}

void BufferOperations::UpdateMinPivot(Node* node, Page page, const Slice& key) {
    node->MayUpdateMinPivot(page, key);
}

void BufferOperations::EnsureCompactionBuffer(Node* node) {
    // Helper method to ensure compaction buffer exists
    // Implementation would be moved from Node class
}

void BufferOperations::CleanupCompactionBuffer(Node* node) {
    // Helper method to cleanup compaction buffer
    // Implementation would be moved from Node class
}

// BufferTreeOperations implementation
SlicePageMap BufferTreeOperations::FlushFilesToChildren(
    Node* node, int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots,
    bool one_level_only) {
    
    // Implementation would be moved from Node class
    SlicePageMap result;
    // This is a complex operation that would need the full tree context
    return result;
}

SlicePageMap BufferTreeOperations::FlushFilesToChildrenNoSplit(
    Node* node, int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files) {
    
    SlicePageMap result;
    // Implementation would be moved from Node class
    return result;
}

SlicePageMap BufferTreeOperations::FlushAndMergeSmallLeaf(
    Node* node, uint32_t child_id, int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files) {
    
    SlicePageMap result;
    // Implementation would be moved from Node class
    return result;
}

void BufferTreeOperations::AppendFilesToChild(
    Node* parent_node, uint32_t child_pg_id, 
    std::vector<Node*>* tmp_nodes,
    std::vector<std::shared_ptr<FileMetaData>>* remains, 
    std::vector<std::shared_ptr<FileMetaData>>* flushed,
    const Slice& pivot, SlicePageMap* overflow_children,
    SlicePageMap* children_need_compaction) {
    
    // Implementation would be moved from Node class
    parent_node->AppendFilesToChild(child_pg_id, tmp_nodes, remains, flushed,
                                   pivot, overflow_children, children_need_compaction);
}

void BufferTreeOperations::CompactChildren(Node* node, 
                                          SlicePageMap* children_need_compaction,
                                          SlicePageMap* overflow_children) {
    node->CompactChildren(children_need_compaction, overflow_children);
}

void BufferTreeOperations::FinalizeSubtree(Node* node, Page this_page, int level_of_files,
                                          std::vector<Node*>* tmp_nodes,
                                          std::vector<std::shared_ptr<FileMetaData>>* flushed,
                                          int locked_node_id) {
    node->FinalizeSubtree(this_page, level_of_files, tmp_nodes, flushed, locked_node_id);
}

void BufferTreeOperations::DistributeFilesToNodes(
    Node* node, 
    std::vector<std::shared_ptr<FileMetaData>>* files,
    std::vector<uint32_t>* new_pages,
    std::vector<Slice>* guards,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    
    node->DistributeFilesToNodes(files, new_pages, guards, new_pivots, old_pivots);
}

void BufferTreeOperations::CompactAndFlushToChildren(
    Node* node, 
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    
    node->CompactAndFlushToChildren(new_pivots, old_pivots);
}

} // namespace WOT_NAMESPACE