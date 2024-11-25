#include "default_strategy.h"
#include "wot_index.h"
#include "wot_buf_mgr/buffer_pool_helper.h"

namespace WOT_NAMESPACE {

// TODO: we need to set a bit in the node page to indicate that the node is
// free from such hotspot range. If the node accepts any buffer from parent,
// unset the bit again. Otherwise we may waste in trying to adapt also include
// this node in reading.

// TODO: when we find files in range are empty, we set the bit. When we flush to
// this node we unset. When we read, we check the bit.

bool HotOnlyAdapt::HasTargetData(LevelDBLSMT* lsmt) {
  return lsmt->HasHotspot();
}

void HotOnlyAdapt::CompactRootBufferForFlush(int* level_of_files,
                      std::vector<std::shared_ptr<FileMetaData>>* files) {
  // if (low_hot_key_.empty() || up_hot_key_.empty()) {
  if (hotspots_.Empty()) {
    DefaultAdapt::CompactRootBufferForFlush(level_of_files, files);
    return;
  }
  bool level_mismatch = false;
  do {
    files->clear();
    int level = -1;
    int hotspot_idx = 0;
    while (hotspots_.HasMoreHotspot(hotspot_idx)) {
      const std::string& low_hot_key = hotspots_.GetLowKey(hotspot_idx);
      const std::string& up_hot_key = hotspots_.GetUpKey(hotspot_idx);
      level = tree_->GetRootLSMT()->GetFilesInRange(files, low_hot_key, up_hot_key);
      if (level != -1 && files->size() != 0) {
        break;
      }
      hotspot_idx++;
    }
    // int level = tree_->GetRootLSMT()->GetFilesInRange(files, low_hot_key_, up_hot_key_);
    if (level == -1 && files->size() == 0) {
      tree_->lock_manager_->EscalateLock(-1);
      tree_->GetRootLSMT()->SetHotspot(false);
      tree_->lock_manager_->AlleviateLock(-1);
      return;
    }
    const std::string& low_hot_key = hotspots_.GetLowKey(hotspot_idx);
    const std::string& up_hot_key = hotspots_.GetUpKey(hotspot_idx);
    assert(level != -1 && files->size() != 0);
    bool root_updated = false;
    if (FileAcrossPivots(tree_->buffer_manager_, tree_->root_pg_id_, files,
                          tree_->internal_comparator_.user_comparator())) {
      // Find the smallest key in files
      Slice file_min_key = files->at(0)->smallest.user_key();
      for (auto const& file : *files) {
        if (file->smallest.user_key().compare(file_min_key) < 0) {
          file_min_key = file->smallest.user_key();
        }
      }
      tree_->lock_manager_->EscalateLock(tree_->root_pg_id_);
      Page rt = tree_->buffer_manager_->Lookup(tree_->root_pg_id_);
      tree_->root_->MayUpdateMinPivot(rt, file_min_key);
      tree_->lock_manager_->AlleviateLock(tree_->root_pg_id_);
      root_updated = true;

      // If the file ranges do not match, compact again with guards
      rt = tree_->buffer_manager_->Lookup(tree_->root_pg_id_);
      // We reuse files as a compaction input
      std::set<uint64_t> dead_files;
      // tree_->buffer_manager_->WriteUnlock(tree_->root_pg_id_);
      tree_->lock_manager_->EscalateLock(-1);
      tree_->GetRootLSMT()->CompactFilesInRange(level, files, rt);
      tree_->GetRootLSMT()->GetObsoleteFiles(dead_files);
      tree_->lock_manager_->AlleviateLock(-1);
      LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead_files);
      files->clear();
      int nl = tree_->GetRootLSMT()->GetFilesInRange(files, low_hot_key, up_hot_key);
      // assert(nl == level);
      level_mismatch = (nl != level || files->size() == 0);
    }
    if (files->size() > 0 && !root_updated) {
      tree_->lock_manager_->EscalateLock(tree_->root_pg_id_);
      Page rt = tree_->buffer_manager_->Lookup(tree_->root_pg_id_);
      tree_->root_->MayUpdateMinPivot(rt, files->at(0)->smallest.user_key());
      tree_->lock_manager_->AlleviateLock(tree_->root_pg_id_);
      fprintf(stdout, "==== %s ====\n", files->at(0)->smallest.user_key().ToString().c_str());
      PagePrint(rt);
      fflush(stdout);
    }
    *level_of_files = level;
  } while (level_mismatch);
}

void HotOnlyAdapt::CompactNodeBufferForFlush(Node* cur_node, Page page, int* level_of_files,
                      std::vector<std::shared_ptr<FileMetaData>>* files) {
  // if (low_hot_key_.empty() || up_hot_key_.empty()) {
  if (hotspots_.Empty()) {
    DefaultAdapt::CompactNodeBufferForFlush(cur_node, page, level_of_files, files);
    return;
  }
  bool level_mismatch = false;
  do {
    files->clear();
    int level = -1;
    int hotspot_idx = 0;
    while (hotspots_.HasMoreHotspot(hotspot_idx)) {
      const std::string& low_hot_key = hotspots_.GetLowKey(hotspot_idx);
      const std::string& up_hot_key = hotspots_.GetUpKey(hotspot_idx);
      level = tree_->GetRootLSMT()->GetFilesInRange(files, low_hot_key, up_hot_key);
      if (level != -1 && files->size() != 0) {
        break;
      }
      hotspot_idx++;
    }
    // int level = cur_node->GetNodeLSMT()->GetFilesInRange(files, low_hot_key_, up_hot_key_);
    if (level == -1 && files->size() == 0) {
      tree_->lock_manager_->EscalateLock(cur_node->pg_id_);
      cur_node->GetNodeLSMT()->SetHotspot(false);
      tree_->lock_manager_->AlleviateLock(cur_node->pg_id_);
      return;
    }
    const std::string& low_hot_key = hotspots_.GetLowKey(hotspot_idx);
    const std::string& up_hot_key = hotspots_.GetUpKey(hotspot_idx);
    assert(level != -1 && files->size() != 0);
    // level = level == -1 ? 0 : level;
    if (FileAcrossPivots(tree_->buffer_manager_, cur_node->pg_id_, files,
                          tree_->internal_comparator_.user_comparator())) {
      // We reuse files as a compaction input
      cur_node->BufferCompactFilesInRange(level, files, page);
      std::set<uint64_t> dead_files;
#ifdef BREAKDOWN
      uint64_t start = _rdtsc();
#endif
      tree_->lock_manager_->EscalateLock(cur_node->pg_id_);
      cur_node->GetObsoleteFilesAndInstallNewBuffer(dead_files);
      tree_->lock_manager_->AlleviateLock(cur_node->pg_id_);
#ifdef BREAKDOWN
      uint64_t end = _rdtsc();
      tree_->writer_stat_.compact_ltime.fetch_add(end - start);
#endif
      LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead_files);
      files->clear();
      int nl = cur_node->GetNodeLSMT()->GetFilesInRange(files, low_hot_key, up_hot_key);
      // assert(nl == level);
      level_mismatch = (nl != level || files->size() == 0);
    }
    *level_of_files = level;
  } while (level_mismatch);
}

}  // namespace WOT_NAMESPACE