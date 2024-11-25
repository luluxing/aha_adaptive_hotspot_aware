#include "default_strategy.h"
#include "wot_index.h"
#include "leveldb/port/port_stdcxx.h"
#include "wot_buf_mgr/buffer_pool_helper.h"

namespace WOT_NAMESPACE {

DefaultAdapt::DefaultAdapt(BplusTree* tree)
: tree_(tree) {}

// void DefaultAdapt::SetHotKeys(const Slice& low, const Slice& up) {
//   // assert(low_hot_key_.size() == 0 && up_hot_key_.size() == 0);
//   low_hot_key_ = low.ToString();
//   up_hot_key_ = up.ToString();
// }

void DefaultAdapt::SetHotspots(const Hotspots& hotspots) {
  hotspots_ = hotspots;
}

void DefaultAdapt::ResetWorkQueueSize(int s) {
  adapt_mutex_.Lock();
  if (s <= 0) queue_size_ = 10;
  else queue_size_ = s;
  adapt_mutex_.Unlock();
}

int DefaultAdapt::GetWorkQueueSize() {
  adapt_mutex_.Lock();
  int ret = adapt_work_queue_.size();
  adapt_mutex_.Unlock();
  return ret;
}

void DefaultAdapt::ClearWorkQueue() {
  adapt_mutex_.Lock();
  adapt_work_queue_.clear();
  adapt_mutex_.Unlock();
}

void DefaultAdapt::AddWorkToQueue(int node_id, int node_level) {
  if (GetWorkQueueSize() > queue_size_) {
    return;
  }
  adapt_work_queue_.push(node_id);
}

bool DefaultAdapt::GetWorkFromQueue(int* node_id) {
  return adapt_work_queue_.try_pop(*node_id);
}

bool DefaultAdapt::HasTargetData(LevelDBLSMT* lsmt) {
  return true;
}

void DefaultAdapt::ScheduleMemAdapt() {
  tree_->AddMemCompactionWork();
}

void DefaultAdapt::ScheduleImmAdapt() {
  tree_->AddImmCompactionWork();
}

void DefaultAdapt::ScheduleLSMTAdapt() {
  // We require the tree to have at least 2 levels to schedule LSMT adapt
  assert(tree_->TreeHeight() > 1);
  tree_->AddLSMTCompactionWork();
}

void DefaultAdapt::CompactRootBufferForFlush(int* level_of_files,
                                std::vector<std::shared_ptr<FileMetaData>>* files) {
  // tree_->GetRootLSMT()->GetBottomCompactedFiles(files);
  // *level_of_files = tree_->GetRootLSMT()->GetBottomLevel();
  *level_of_files = tree_->GetRootLSMT()->GetAllBottomLevelFiles(files);
  if (files->size() == 0 ||
      FileAcrossPivots(tree_->buffer_manager_, tree_->root_pg_id_, files,
                      tree_->internal_comparator_.user_comparator())) {
    tree_->lock_manager_->EscalateLock(tree_->root_pg_id_);
    Page rt = tree_->buffer_manager_->Lookup(tree_->root_pg_id_);
    tree_->root_->MayUpdateMinPivot(rt, files->at(0)->smallest.user_key());
    tree_->lock_manager_->AlleviateLock(tree_->root_pg_id_);

    // If the file ranges do not match, compact again with guards
    files->clear();
    rt = tree_->buffer_manager_->Lookup(tree_->root_pg_id_);
    // TODO: we have to lock the root node just to compact the bottom level.
    // Can we do better not to lock the root node?
    tree_->lock_manager_->EscalateLock(-1);
    tree_->GetRootLSMT()->CompactBottomLevel(rt);
    tree_->GetRootLSMT()->RemoveObsoleteFiles();
    *level_of_files = tree_->GetRootLSMT()->GetAllBottomLevelFiles(files);
    tree_->lock_manager_->AlleviateLock(-1);
  }
}

void DefaultAdapt::AdaptLSMT() {
  tree_->lock_manager_->ReadLock(-1);
  // if (tree_->GetRootLSMT()->GetBottomLevel() < 0) {
  //   tree_->lock_manager_->ReadUnlock(-1);
  //   return;
  // }
  std::vector<std::shared_ptr<FileMetaData>> files;
  int level_of_files;
  tree_->lock_manager_->ReadLock(tree_->root_pg_id_);
  CompactRootBufferForFlush(&level_of_files, &files);
  if (files.size() == 0) {
    tree_->lock_manager_->ReadUnlock(tree_->root_pg_id_);
    tree_->lock_manager_->ReadUnlock(-1);
    return;
  }
  tree_->FlushLSMTfilesToBol(level_of_files, &files, true);
  files.clear();
}

void DefaultAdapt::CompactNodeBufferForFlush(Node* cur_node, Page page, int* level_of_files,
                        std::vector<std::shared_ptr<FileMetaData>>* files) {
  // cur_node->GetNodeLSMT()->GetBottomCompactedFiles(files);
  // *level_of_files = cur_node->GetNodeLSMT()->GetBottomLevel();
  *level_of_files = cur_node->GetNodeLSMT()->GetAllBottomLevelFiles(files);
  if (files->size() == 0 ||
        FileAcrossPivots(tree_->buffer_manager_, cur_node->pg_id_, files,
                          tree_->internal_comparator_.user_comparator())) {
    files->clear();
    cur_node->BufferCompactBottomLevel(page);
    tree_->lock_manager_->EscalateLock(cur_node->pg_id_);
    cur_node->InstallNewBuffer();
    tree_->lock_manager_->AlleviateLock(cur_node->pg_id_);
    // cur_node->GetNodeLSMT()->GetBottomCompactedFiles(files);
    // *level_of_files = cur_node->GetNodeLSMT()->GetBottomLevel();
    *level_of_files = cur_node->GetNodeLSMT()->GetAllBottomLevelFiles(files);
  }
}

void DefaultAdapt::AdaptWOT() {
  int adapt_node;
  Node* node;
#ifdef BREAKDOWN
  uint64_t start = _rdtsc(), end;
#endif
  if (!CanProceedAdapt(node, &adapt_node)) {
#ifdef BREAKDOWN
    end = _rdtsc();
    tree_->writer_stat_.initial_check_time.fetch_add(end - start);
#endif
    return;
  }
#ifdef BREAKDOWN
  end = _rdtsc();
  tree_->writer_stat_.initial_check_time.fetch_add(end - start);
#endif
  ReadTbb a;
  if (tree_->node_table_.find(a, adapt_node)) {
    node = a->second;
  } else {
    return;
  }
  a.release();

  std::vector<std::pair<Node*, int>> nodes_pool;
  nodes_pool.push_back(std::make_pair(tree_->root_, -1));
  tree_->lock_manager_->ReadLock(tree_->root_pg_id_);
  uint32_t pool_idx = 0;
#ifdef BREAKDOWN
  start = _rdtsc();
#endif
  Node* cur_node = FindNodePath(adapt_node, node, &nodes_pool, &pool_idx);
#ifdef BREAKDOWN
  end = _rdtsc();
  tree_->writer_stat_.search_path_time.fetch_add(end - start);
#endif

  Page page = tree_->buffer_manager_->Pin(cur_node->pg_id_);
  bool leaf_page = ((TreePageHeader) page)->is_leaf_;

  if (cur_node == nullptr || cur_node->GetNodeLSMT() == nullptr  ||
      node->GetNodeLSMT()->GetBottomLevel() < 0) {
    tree_->buffer_manager_->Unpin(cur_node->pg_id_);
    for (auto const& node_pair : nodes_pool) {
      tree_->lock_manager_->ReadUnlock(node_pair.first->pg_id_);
    }
    return;
  }
  bool small_leaf = leaf_page &&
                    cur_node->GetNodeLSMT()->GetLSMTStatus() == LSMTStatus::kSmallLeaf;
  std::vector<std::shared_ptr<FileMetaData>> files;
  int level_of_files = -1;
  if (!small_leaf) {
#ifdef BREAKDOWN
    start = _rdtsc();
#endif
    CompactNodeBufferForFlush(cur_node, page, &level_of_files, &files);
#ifdef BREAKDOWN
    end = _rdtsc();
    tree_->writer_stat_.real_work_time.fetch_add(end - start);
#endif
    if (files.size() == 0) {
      tree_->buffer_manager_->Unpin(cur_node->pg_id_);
      for (auto const& node_pair : nodes_pool) {
        tree_->lock_manager_->ReadUnlock(node_pair.first->pg_id_);
      }
      return;
    }
  }
  std::vector<SlicePageMap> new_pivots;
  std::vector<std::pair<Slice, uint32_t>> old_pivots;
#ifdef BREAKDOWN
  start = _rdtsc();
#endif
  if (!small_leaf) {
    assert(level_of_files >= 0);
    cur_node->FlushFilesToChildren(level_of_files, &files, &new_pivots, &old_pivots, true);
  }
  // cur_node->node_lsmt_->ClearBottomLevelFiles();
  Node* tmp_node = cur_node;
  bool root_split = false;
  Node* small_leaf_node = nullptr;
  while (new_pivots.size() > 0 || small_leaf) {
    int pg_id = tmp_node->pg_id_;
    if (new_pivots.size() > 0) {
      tmp_node->UpdatePivots(&new_pivots, &old_pivots);
    }
    SlicePageMap result;
    if (small_leaf) {
      result = tmp_node->SplitSmallLeaf();
      // small_leaf = false;  
      if (result.size() == 0) {
        small_leaf_node = tmp_node;
        // delete tmp_node;
        // WriteTbb a;
        // if (tree_->node_table_.find(a, pg_id)) {
        //   a->second = nullptr;
        // }
        // tree_->node_table_.erase(pg_id);
        // a.release();
      }    
    } else if (tmp_node->node_overflow_) {
      result = tmp_node->SplitNodeAndLSMT(
                                  &new_pivots, &old_pivots);
    }
    if (result.size() == 0) {
      break;
    }
    new_pivots.clear();
    old_pivots.clear();
    
    if (pg_id != tree_->root_pg_id_) {
      if (small_leaf) {
        tree_->buffer_manager_->UnpinAndRelease(pg_id);
      } else {
        tree_->buffer_manager_->Unpin(pg_id);
      }
      uint32_t parent_idx = nodes_pool[pool_idx].second;
      assert(parent_idx < nodes_pool.size());
      tmp_node = nodes_pool[parent_idx].first;
      pool_idx = parent_idx;
      Page p = tree_->buffer_manager_->Pin(tmp_node->pg_id_);

      small_leaf = ((TreePageHeader) p)->is_leaf_ && tmp_node->GetNodeLSMT() != nullptr &&
                    tmp_node->GetNodeLSMT()->GetLSMTStatus() == LSMTStatus::kSmallLeaf;
      new_pivots.push_back(result);
      const Slice& op = PageReadPivotAtOffset(p, PageGetOffsetByChild(p, pg_id));
      old_pivots.push_back(std::make_pair(op, pg_id));
    } else {
      tree_->lock_manager_->EscalateLock(tree_->root_pg_id_);
      tree_->UpdateRoot(result);
      tree_->lock_manager_->AlleviateLock(tree_->root_pg_id_);
      root_split = true;
    }
  }
  for (auto it = nodes_pool.begin(); it != nodes_pool.end(); it++) {
    if (root_split && it == nodes_pool.begin()) {
      tree_->lock_manager_->ReadUnlock(0);
    } else {
      tree_->lock_manager_->ReadUnlock(it->first->pg_id_);
      if (small_leaf_node != nullptr && it->first == small_leaf_node) {
        delete it->first;
        WriteTbb a;
        if (tree_->node_table_.find(a, it->first->pg_id_)) {
          a->second = nullptr;
        }
        tree_->node_table_.erase(it->first->pg_id_);
        a.release();
      }
    }
  }
  if (!root_split) {
    tree_->buffer_manager_->Unpin(tmp_node->pg_id_);
  }
#ifdef BREAKDOWN
  end = _rdtsc();
  tree_->writer_stat_.real_work_time.fetch_add(end - start);
#endif
}

bool DefaultAdapt::CanProceedAdapt(Node* node, int* adapt_node) {
  node = nullptr;

  if (tree_->shutting_down_.load(std::memory_order_acquire) ||
        GetWorkQueueSize() == 0) {
    ClearWorkQueue();
    return false;
  }

  if (!GetWorkFromQueue(adapt_node)) {
    return false;
  }

  if (*adapt_node < 0) {
    AdaptLSMT();
    return false;
  }
  
  ReadTbb a;
  if (!tree_->node_table_.find(a, *adapt_node)) {
    return false;
  }
  node = a->second;// node_table_[adapt_node];
  a.release();

  tree_->lock_manager_->ReadLock(*adapt_node);
  Page adapt_page = tree_->buffer_manager_->Pin(*adapt_node);
  bool is_leaf = ((TreePageHeader) adapt_page)->is_leaf_;
  tree_->buffer_manager_->Unpin(*adapt_node);
  // if (node->GetNodeLSMT() == nullptr) {
  //   tree_->lock_manager_->ReadUnlock(*adapt_node);
  //   return false;
  // }
  if (is_leaf && node->GetNodeLSMT()->GetLSMTStatus() != LSMTStatus::kSmallLeaf) {
    // Page will be pinned after finishing bulk loading leaf
#ifdef BREAKDOWN
    uint64_t start = _rdtsc(), end;
#endif
    BulkloadLeafnode(*adapt_node);
#ifdef BREAKDOWN
    end = _rdtsc();
    tree_->writer_stat_.adapt_leaf_time.fetch_add(end - start);
#endif
    tree_->lock_manager_->ReadUnlock(*adapt_node);
    return false;
  }
  if (node->GetNodeLSMT()->GetBottomLevel() < 0) {// || !node->GetNodeLSMT()->HasHotspot()) {
    tree_->lock_manager_->ReadUnlock(*adapt_node);
    return false;
  }
  tree_->lock_manager_->ReadUnlock(*adapt_node);
  return true;
}

void DefaultAdapt::BulkloadLeafnode(uint32_t node_id) {
  ReadTbb a;
  assert(tree_->node_table_.find(a, node_id));
  // assert(node_table_.contains(node_id));
  Node* old_node = a->second;// node_table_[node_id];
  a.release();
  if (old_node->GetNodeLSMT() == nullptr ||
          old_node->GetNodeLSMT()->GetBottomLevel() < 0) {
    return;
  }

  auto min_key = old_node->GetNodeLSMT()->GetSmallestUserKey();

  const std::vector<std::shared_ptr<FileMetaData>>* bottom_files;
  bool compacted = false;
  if (old_node->GetNodeLSMT()->NeedCompactBottomLevel()) {
    // A new lsmt has been created
    bottom_files = old_node->BufferGetAndCompactBottomLevel(nullptr);
  } else {
    // There are multiple files in level-i (i>0) or one file in level-0
    bottom_files = old_node->GetNodeLSMT()->GetBottomLevelFiles(nullptr);
  }
  std::vector<std::shared_ptr<FileMetaData>> out_file_names;
  if (bottom_files->size() == 1) {
    // Count the number of data entries in the file.
    // If zero, we can just delete the node. Otherwise,
    // make the node as SmallLeaf.
    auto entry_num = bottom_files->at(0)->file_entry;
    if (entry_num == 0) {
      delete old_node;
      WriteTbb a;
      if (tree_->node_table_.find(a, node_id)) {
        a->second = nullptr;
      }
      tree_->node_table_.erase(node_id);
      a.release();
    } else {
      tree_->lock_manager_->EscalateLock(node_id);
      old_node->SetBufferStatus(LSMTStatus::kSmallLeaf);
      old_node->InstallNewBuffer();
      tree_->lock_manager_->AlleviateLock(node_id);
    }
    return;
  }
  
  assert(bottom_files->size() > 1);
  int bottom_size = bottom_files->size();
  Page old_page = tree_->buffer_manager_->Pin(node_id);
  
  std::vector<uint32_t> top_level;
  int add_height = tree_->BulkloadFiles(bottom_files, &top_level, true);
  if (add_height > 1) {
    // We need to have a fix for this but this situation should be rare
    // as there cannot be so many files in the leaf.
    fprintf(stderr, "Leaf node overflows when splitting into many smaller ones\n");
    std::abort();
  }
  assert(top_level.size() == 1);
  // LOCK
  // BGObtainLock();
  tree_->lock_manager_->EscalateLock(node_id);
  // tree_->lock_manager_->WriteLock(node_id);
  ReadTbb acc;
  tree_->node_table_.find(acc, top_level[0]);
  delete acc->second;
  acc.release();
  tree_->node_table_.erase(top_level[0]);
  Page new_page = tree_->buffer_manager_->Pin(top_level[0]);
  const Slice& minpivot = PageReadPivotAtOffset(new_page, 0);
  if (tree_->internal_comparator_.user_comparator()->Compare(min_key, minpivot) < 0) {
    PageUpdateMinPivot(new_page, min_key);
  }
  
  memcpy(old_page, new_page, PAGE_SIZE);

  ((TreePageHeader) old_page)->page_id_ = node_id;
  ((TreePageHeader) old_page)->is_leaf_ = false;
  ((TreePageHeader) old_page)->is_dirty_ = true;
  // TODO: need to erase new_page
  tree_->buffer_manager_->UnpinAndRelease(top_level[0]);
  tree_->buffer_manager_->Delete(top_level[0]);

  old_node->SetBufferStatus(LSMTStatus::kBuffer);
  if (!compacted) {
    old_node->BufferClearBottomLevelFiles();
  }

  old_node->InstallNewBuffer();
  tree_->buffer_manager_->UnpinAndRelease(node_id);
  // tree_->lock_manager_->WriteUnlock(node_id);
  tree_->lock_manager_->AlleviateLock(node_id);
  // BGReleaseLock();
  // UNLOCK
}

Node* DefaultAdapt::FindNodePath(uint32_t adapt_node, Node* node,
                    std::vector<std::pair<Node*, int>>* nodes_pool,
                    uint32_t* pool_idx) {
  Page adapt_page = tree_->buffer_manager_->Pin(adapt_node);
  bool is_leaf = ((TreePageHeader) adapt_page)->is_leaf_;
  Slice min_key;
  if (is_leaf) {
    min_key = node->GetNodeLSMT()->GetOneUserKey();
  } else {
    // min_key = buffer_manager_->GetPivotAtOffset(adapt_node, 0);
    min_key = PageReadPivotAtOffset(adapt_page, 0);
  }
  assert(min_key.size() > 0);

  Node* cur_node;
  while (*pool_idx < nodes_pool->size()) {
    cur_node = nodes_pool->at(*pool_idx).first;
    if (cur_node->pg_id_ == adapt_node /*|| (cur_node->GetNodeLSMT() != nullptr &&
        cur_node->GetNodeLSMT()->GetBottomLevel() >= 0)*/) {
      break;
    }
    Page p = tree_->buffer_manager_->Pin(cur_node->pg_id_);
    if (((TreePageHeader) p)->is_leaf_) {
      tree_->buffer_manager_->Unpin(cur_node->pg_id_);
      break;
    }
    uint32_t item_num = ((TreePageHeader) p)->item_num_;
    int i = 0;
    for (; i < item_num; i++) {
      if (tree_->internal_comparator_.user_comparator()
            ->Compare(min_key, PageReadPivotAtOffset(p, i)) < 0) {
        break;
      }
    }
    if (i == 0) {
      std::cerr << "Error: key smaller than minimum\n";
      std::abort();
    }
    uint32_t child = PageReadChildAtOffset(p, i - 1);
    tree_->buffer_manager_->Unpin(cur_node->pg_id_);
    ReadTbb a;
    tree_->node_table_.find(a, child);
    nodes_pool->push_back(std::make_pair(a->second, *pool_idx));
    tree_->lock_manager_->ReadLock(child);
    a.release();

    (*pool_idx)++;
  }
  tree_->buffer_manager_->Unpin(adapt_node);
  return cur_node;
}

} // namespace WOT_NAMESPACE