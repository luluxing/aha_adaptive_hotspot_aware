#include "../wot_index.h"
#include "wot_buf_mgr/buffer_pool_helper.h"

namespace WOT_NAMESPACE {


// void Node::CompactWithOneChild(std::vector<SlicePageMap>* new_pivots,
//                           std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
//   Page page = tree_->buffer_manager_->Lookup(pg_id_);
//   node_lsmt_->PrintStat();
//   uint32_t child = PageReadChildAtOffset(page, node_lsmt_->flush_offset);
//   ReadTbb a;
//   assert(tree_->node_table_.find(a, child));
//   // assert(tree_->node_table_.contains(child));
//   Node* n = a->second;// tree_->node_table_[child];
//   Iterator* iter = n->node_lsmt_->NewMergedIterator(ReadOptions());

//   std::vector<std::shared_ptr<FileMetaData>> files;
//   node_lsmt_->CompactWithOneChild(page, iter, &files);

//   n->node_lsmt_->ClearAndRemoveObsoleteFiles();

//   if (tree_->buffer_manager_->FileAcrossPivots(pg_id_, &files,
//                                                icmp_->user_comparator())) {
//     std::cerr << "Error: ranges do not match\n";
//     std::abort();
//   }
//   OpState s = n->AddFiles(&files);
//   if (s != OpState::kOverflow) {
//     return;
//   }
//   SlicePageMap new_kids = n->MaybeSplitOrFlush();
//   if (!new_kids.empty() && old_pivots != nullptr && new_pivots != nullptr) {
//     old_pivots->push_back(std::make_pair(
//             PageReadPivotAtOffset(page, node_lsmt_->flush_offset), child));
//     new_pivots->push_back(new_kids);
//   }

//   node_lsmt_->flush_offset++;
//   if (node_lsmt_->flush_offset >= ((TreePageHeader) page)->item_num_) {
//     node_lsmt_->flush_offset = 0;
//   }

//   files.clear();
// }

// void Node::CompactWithChildren(std::vector<SlicePageMap>* new_pivots,
//                           std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
//   // Page is already pinned
//   // Create a merged iterator of this node_lsmt and all children's
//   std::vector<Iterator*> iter_vec;
//   iter_vec.push_back(node_lsmt_->NewMergedIterator(ReadOptions()));
//   Page page = tree_->buffer_manager_->Lookup(pg_id_);
//   uint32_t item_num = ((TreePageHeader) page)->item_num_;

//   for (int off = 0; off < item_num; off++) {
//     uint32_t child = PageReadChildAtOffset(page, off);
//     ReadTbb a;
//     assert(tree_->node_table_.find(a, child));
//     // assert(tree_->node_table_.contains(child));
//     Node* n = a->second;// tree_->node_table_[child];
//     iter_vec.push_back(n->node_lsmt_->NewMergedIterator(ReadOptions()));
//   }
//   Iterator* iter = NewMergingIterator(icmp_, &iter_vec[0], iter_vec.size());

//   // Send the iterator to lsmt
//   std::vector<std::shared_ptr<FileMetaData>> files;
//   node_lsmt_->CompactWithInput(page, iter, &files);
//   // iter is already deleted
//   node_lsmt_->Clear();
  
//   for (int off = 0; off < item_num; off++) {
//     uint32_t child = PageReadChildAtOffset(page, off);
//     ReadTbb a;
//     tree_->node_table_.find(a, child);
//     Node* n = a->second;// tree_->node_table_[child];
//     n->node_lsmt_->ClearAndRemoveObsoleteFiles();
//   }

//   // Add files to children
//   if (tree_->buffer_manager_->FileAcrossPivots(pg_id_, &files,
//                                                icmp_->user_comparator())) {
//     std::cerr << "Error: ranges do not match\n";
//     std::abort();
//   }  
//   FlushFilesToChildren(&files, new_pivots, old_pivots);
//   files.clear();
// }

// void Node::CompactAndFlushToSomeChildren(
//                           std::vector<SlicePageMap>* new_pivots,
//                           std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
//   // Check the file ranges match the pivots

//   std::vector<std::shared_ptr<FileMetaData>> files;
//   node_lsmt_->RetrieveFilesForTreePushdown(&files, tree_->tmp_count_);


//   if (tree_->buffer_manager_->FileAcrossPivots(pg_id_, &files,
//                                                icmp_->user_comparator())) {
//     // If the file ranges do not match, compact again with guards
//     std::cerr << "Error: ranges do not match\n";
//     std::abort();
//   }  
//   FlushFilesToChildren(&files, new_pivots, old_pivots);

//   // Remove filemetadata in the old LSMT
//   // node_lsmt_->ClearBottomLevelFiles();
//   // files->clear();
// }

// We just finish compacting the LevelDBLSMT and the very last level overflows.
// The page is already pinned. And the node is already read-locked
void Node::CompactAndFlushToChildren(
                          std::vector<SlicePageMap>* new_pivots,
                          std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  std::vector<std::shared_ptr<FileMetaData>> files;
  int level_of_files;
  if (tree_->flush_file_num_ <= 0) {
    level_of_files = node_lsmt_->GetAllBottomLevelFiles(&files);
  } else {
    level_of_files = node_lsmt_->GetSomeBottomLevelFiles(&files, tree_->flush_file_num_);
  }
  // int level_of_files = node_lsmt_->GetAllBottomLevelFiles(&files);
  if (FileAcrossPivots(tree_->buffer_manager_, pg_id_, &files,
                        icmp_->user_comparator())) {

    files.clear();
    Page page = tree_->buffer_manager_->Lookup(pg_id_);
    BufferCompactBottomLevel(page);
#ifdef BREAKDOWN
    uint64_t start = _rdtsc();
#endif
    std::set<uint64_t> dead_files;
    tree_->lock_manager_->EscalateLock(pg_id_);
    GetObsoleteFilesAndInstallNewBuffer(dead_files);
    tree_->lock_manager_->AlleviateLock(pg_id_);
#ifdef BREAKDOWN
    uint64_t end = _rdtsc();
    tree_->writer_stat_.flush_ltime.fetch_add(end - start);
#endif    
    LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead_files);
    level_of_files = node_lsmt_->GetAllBottomLevelFiles(&files);
  }  
  FlushFilesToChildren(level_of_files, &files, new_pivots, old_pivots);

  // Remove filemetadata in the old LSMT
  // node_lsmt_->ClearBottomLevelFiles();
  files.clear();
}


} // namespace WOT_NAMESPACE