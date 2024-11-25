#include <vector>
#include "wot_state.h"
#include "wot_index.h"

namespace WOT_NAMESPACE {

void BplusTreeState::NewIterator(BplusTree* tree, std::vector<Iterator*>* iter_vec,
                                  const Slice& low, const Slice& up, MemTable** mem, 
                                  MemTable** imm, std::vector<int>* locked_nodes,
                                  const Comparator* user_comparator, int* non_page_num) {
  int memnum = 0, lsmt = 0, node_num = 0, page_num = 0;
  bool in_hotspot = HotScan(tree, low, up);

  LockLSMT(tree, locked_nodes);

#ifdef BREAKDOWN
  uint64_t start = _rdtsc();
#endif
  tree->GetMutex().Lock();
#ifdef BREAKDOWN
  uint64_t end = _rdtsc();
  tree->reader_stat_.wait_for_lock_time.fetch_add(end - start);
#endif
  DoAddMemToIter(tree, in_hotspot, iter_vec, mem);
  DoAddImmToIter(tree, in_hotspot, iter_vec, imm);
  memnum = iter_vec->size();

  if (tree->GetRootLSMT() != nullptr) {
    DoAddLSMTtoIter(tree, in_hotspot, iter_vec);
    lsmt = iter_vec->size() - memnum;
  }

  DoMayTriggerWOTAdapt(tree, in_hotspot);
#ifdef BREAKDOWN
  start = _rdtsc();
#endif
  tree->lock_manager_->ReadLock(tree->root_pg_id_);
#ifdef BREAKDOWN
  end = _rdtsc();
  tree->reader_stat_.wait_for_lock_time.fetch_add(end - start);
  tree->reader_stat_.wait_root_time.fetch_add(end - start);
#endif
  locked_nodes->push_back(tree->root_pg_id_);
  tree->GetMutex().Unlock();
  std::vector<uint32_t> nodes_vec;
  nodes_vec.push_back(tree->root_pg_id_);
  int idx = 0;
  int node_level = 0;
  int next_level_index = 0;
  auto pi = tree->NewPageIterator();
  bool page_added = false;
  while (idx < nodes_vec.size()) {
    if (nodes_vec[idx] != tree->root_pg_id_) {
#ifdef BREAKDOWN
      start = _rdtsc();
#endif
      tree->lock_manager_->ReadLock(nodes_vec[idx]);
#ifdef BREAKDOWN
      end = _rdtsc();
      tree->reader_stat_.wait_for_lock_time.fetch_add(end - start);
#endif
      locked_nodes->push_back(nodes_vec[idx]);
    }
    ReadTbb a;
    Node* node = nullptr;
    if (tree->node_table_.find(a, nodes_vec[idx])) {
      node = a->second;// tree->node_table_[nodes_vec[idx]];
    }
    a.release();
    if (idx == next_level_index) {
      node_level++;
      next_level_index = nodes_vec.size();
    }
    // Page cur_page = tree->buffer_manager_->Pin(nodes_vec[idx]);
    // Adapt the node buffer if needed
    if (node != nullptr) {
      assert(node->pg_id_ == nodes_vec[idx]);
      DoAddWOTtoIter(tree, node_level, node, in_hotspot, iter_vec);
      node_num = iter_vec->size() - memnum - lsmt;
    } else {
    // if (((TreePageHeader) cur_page)->is_leaf_ && ((TreePageHeader) cur_page)->item_num_ > 0) {
      // iter_vec->push_back(tree->NewPageIterator(nodes_vec[idx]));
      ((PageIterator*) pi)->AddPage(nodes_vec[idx]);
      page_added = true;
      page_num++;
    }
    
    if (node != nullptr) {
#ifdef BREAKDOWN
      start = _rdtsc();
#endif
      FindChildrenNodes(tree, nodes_vec[idx], low, up, nodes_vec, user_comparator);
#ifdef BREAKDOWN
      end = _rdtsc();
      tree->reader_stat_.traverse_time.fetch_add(end - start);
#endif
    }
    // tree->buffer_manager_->Unpin(nodes_vec[idx]);
    idx++;
  }
  if (page_added) {
    iter_vec->push_back(pi);
  }
  if (((double) rand() / (RAND_MAX)) < 0.00001) {
    fprintf(stdout, "iter vec size %ld with %d mem, %d lsmt, %d nodes, %d pages.\n",
            iter_vec->size(), memnum, lsmt, node_num, page_num);
  }
  *non_page_num = memnum + lsmt + node_num;
  DoValidate(tree, in_hotspot, node_num, user_comparator);
}

void BplusTreeState::FindChildrenNodes(BplusTree* tree, uint32_t pg_id, const Slice& low,
                                       const Slice& up, std::vector<uint32_t>& nodes_vec,
                                       const Comparator* user_comparator) {
  Page cur_page = tree->buffer_manager_->Pin(pg_id);
  if (((TreePageHeader) cur_page)->is_leaf_) {
    tree->buffer_manager_->Unpin(pg_id);
    return;
  }
  int offset = low.empty() ? 0 : PageFindOffset(cur_page, low);
  if (!low.empty() && offset <= 0) {
    // low may be smaller than min key
    const Slice& mink = PageReadPivotAtOffset(cur_page, 0);
    if (!up.empty() && user_comparator->Compare(up, mink)< 0) {
      tree->buffer_manager_->Unpin(pg_id);
      return;
    }
    offset = 0;
  }
  
  uint32_t item_num = ((TreePageHeader) cur_page)->item_num_;
  assert(item_num > 0);
  do {
    const Slice& k = PageReadPivotAtOffset(cur_page, offset);
    if (!up.empty() && user_comparator->Compare(up, k) < 0) break;
    nodes_vec.push_back(PageReadChildAtOffset(cur_page, offset));
    offset++;
  } while (offset < item_num);
  tree->buffer_manager_->Unpin(pg_id);
}

void BplusTreeState::ChangeState(BplusTree* tree, BplusTreeState* state) {
  tree->ChangeState(state);
}

bool BplusTreeState::HotScan(BplusTree* tree, const Slice& low, const Slice& up) {
  return tree->RangeWithinHotspot(low, up);
}

void BplusTreeState::LockLSMT(BplusTree* tree, std::vector<int>* locked_nodes) {
#ifdef BREAKDOWN
  uint64_t start = _rdtsc();
#endif
  tree->lock_manager_->ReadLock(-1);
#ifdef BREAKDOWN
  uint64_t end = _rdtsc();
  tree->reader_stat_.wait_for_lock_time.fetch_add(end - start);
  tree->reader_stat_.wait_lsmt_time.fetch_add(end - start);
#endif
  locked_nodes->push_back(-1);
}

void BplusTreeReadOptState::DoAddMemToIter(BplusTree* tree, bool in_hotspot,
                                          std::vector<Iterator*>* iter_vec, 
                                          MemTable** mem) {
  if (tree->mem_empty_) return;
  // The required range is in hotspot but mem is cold only
  if (in_hotspot && tree->mem_cold_only_) return;
  iter_vec->push_back(tree->GetMem()->NewIterator());
  tree->GetMem()->Ref();
  *mem = tree->GetMem();
  DoMayAdaptMem(tree, in_hotspot, 0);
}

void BplusTreeReadOptState::DoAddImmToIter(BplusTree* tree, bool in_hotspot,
                                          std::vector<Iterator*>* iter_vec, 
                                          MemTable** imm) {
  if (!tree->has_imm_) return;
  // The required range is in hotspot but imm is cold only
  if (in_hotspot && tree->imm_cold_only_) return;
  iter_vec->push_back(tree->GetImm()->NewIterator());
  tree->GetImm()->Ref();
  *imm = tree->GetImm();
  DoMayAdaptMem(tree, in_hotspot, 1);
}

// Schedule the memtable to be adapted to read-optimized index
void BplusTreeReadOptState::DoMayAdaptMem(BplusTree* tree,
                                          bool is_hot, int which) {
  if (!is_hot) return;
  if (which == 0) {
    tree->GetAdaptStrategy()->ScheduleMemAdapt();
  } else if (which == 1) {
    tree->GetAdaptStrategy()->ScheduleImmAdapt();
  } else {
    fprintf(stderr, "Wrong memtable type!\n");
    std::abort();
  }
}

// Schedule the lsmt to be adapted to read-optimized index
void BplusTreeReadOptState::DoAddLSMTtoIter(BplusTree* tree, bool is_hot,
                                            std::vector<Iterator*>* iter_vec) {
  int level = tree->GetRootLSMT()->GetBottomLevel();
  // if (level < 0) return;
  if ((is_hot && !tree->GetRootLSMT()->HasHotspot(tree->GetLowHotKey(), tree->GetHighHotKey()))
      || level < 0) {
    // No need to put the lsmt into the iterator list
  } else {
    if (iter_vec != nullptr) {
      iter_vec->push_back(tree->GetRootLSMT()->NewMergedIterator(ReadOptions()));
    }
  }
  
  // If memtable is nonempty, which suggests it should be flushed, no more actions
  if (!tree->mem_empty_ && is_hot && !tree->mem_cold_only_) return;
  if (tree->has_imm_ && is_hot && !tree->imm_cold_only_) return;

  if (is_hot && tree->GetAdaptStrategy()->HasTargetData(
        tree->GetLowHotKey(), tree->GetHighHotKey(), tree->GetRootLSMT())) {
    tree->GetAdaptStrategy()->ScheduleLSMTAdapt();
  }
}

void BplusTreeReadOptState::DoAddWOTtoIter(BplusTree* tree, int node_level, Node* node,
                                           bool is_hot,std::vector<Iterator*>* iter_vec) {
  bool nonempty_level = node->GetNodeLSMT() != nullptr &&
                        node->GetNodeLSMT()->GetBottomLevel() >= 0;
  if (!nonempty_level) return;
  if (is_hot && !node->GetNodeLSMT()->HasHotspot(tree->GetLowHotKey(), tree->GetHighHotKey()))
    return;

  if (iter_vec != nullptr) {
    iter_vec->push_back(node->GetNodeLSMT()->NewMergedIterator(ReadOptions()));
  }
  if (is_hot && tree->GetAdaptStrategy()->HasTargetData(
      tree->GetLowHotKey(), tree->GetHighHotKey(), node->GetNodeLSMT())) {
    tree->GetAdaptStrategy()->AddWorkToQueue(node->pg_id_, node_level);
  }
}

// Schedule the wot to be adapted to read-optimized index
void BplusTreeReadOptState::DoMayTriggerWOTAdapt(BplusTree* tree, bool in_hotspot) {
  // If memtable is nonempty, which suggests it should be flushed, no more actions
  if (!tree->mem_empty_ && in_hotspot && !tree->mem_cold_only_) return;
  if (tree->has_imm_ && in_hotspot && !tree->imm_cold_only_) return;

  // If lsmt is nonempty, which suggests it should be flushed, no more actions
  if (tree->GetRootLSMT() != nullptr && tree->GetRootLSMT()->GetBottomLevel() >= 0
      && in_hotspot &&
      tree->GetRootLSMT()->HasHotspot(tree->GetLowHotKey(), tree->GetHighHotKey()))
      return;

  if (tree->GetAdaptStrategy()->GetWorkQueueSize() > 0) {
    tree->AddWOTCompactionWork();
  }
}

void BplusTreeReadOptState::DoValidate(BplusTree* tree, bool in_hotspot, int non_page_num,
                                       const Comparator* user_comparator) {
  MutexLock l(&validate_mtx_);
  
  if (!in_hotspot) return;
  if (!proactive_validation_) return;
  validate_cnt_.fetch_add(1);
  if (non_page_num == 0) {
    pure_page_cnt_.fetch_add(1);
  }
  if (validate_cnt_.load() % validate_group_ == 0) {
    if (!validation_triggered_ && 
        (double) pure_page_cnt_.load() / validate_group_ > validate_threshold_ &&
        tree->GetAdaptStrategy()->GetWorkQueueSize() <= 5) {
      tree->GetAdaptStrategy()->ResetWorkQueueSize(INT_MAX);
      ValidateRange(tree, user_comparator);
      validation_triggered_ = true;
      fprintf(stdout, "Proactive validation triggered.\n");
    } else if (validation_triggered_ &&
              (double) pure_page_cnt_.load() / validate_group_ >= 1.0 &&
              tree->GetAdaptStrategy()->GetWorkQueueSize() <= 0) {
      proactive_validation_ = false;
      fprintf(stdout, "No more adaptive work, proactive validation stopped.\n");
      tree->GetAdaptStrategy()->ResetWorkQueueSize(-1);
      validation_triggered_ = false;
    }
    pure_page_cnt_.store(0);
    validate_cnt_.store(0);
  }
}

void BplusTreeReadOptState::ValidateRange(BplusTree* tree, const Comparator* user_comparator) {
  tree->GetMutex().Lock();
  if (!tree->mem_empty_) {
    DoMayAdaptMem(tree, true, 0);
  }
  if (tree->has_imm_) {
    DoMayAdaptMem(tree, true, 1);
  }
  if (tree->GetRootLSMT() != nullptr) {
    DoAddLSMTtoIter(tree, true, nullptr);
  }
  tree->GetMutex().Unlock();
  std::vector<uint32_t> nodes_vec;
  nodes_vec.push_back(tree->root_pg_id_);
  int idx = 0;
  int node_level = 0;
  int next_level_index = 0;
  const Slice& low = tree->GetLowHotKey();
  const Slice& up = tree->GetHighHotKey();
  // We do not lock the visited nodes here
  while (idx < nodes_vec.size()) {
    ReadTbb a;
    Node* node = nullptr;
    if (tree->node_table_.find(a, nodes_vec[idx])) {
      node = a->second;// tree->node_table_[nodes_vec[idx]];
    }
    a.release();
    if (idx == next_level_index) {
      node_level++;
      next_level_index = nodes_vec.size();
    }

    if (node != nullptr) {
      DoAddWOTtoIter(tree, node_level, node, true, nullptr);
      FindChildrenNodes(tree, nodes_vec[idx], low, up, nodes_vec, user_comparator);
    }
    idx++;
  }
}

void BplusTreeWriteOptState::DoAddMemToIter(BplusTree* tree, bool in_hotspot,
                                          std::vector<Iterator*>* iter_vec, 
                                          MemTable** mem) {
  if (tree->mem_empty_) return;
  // The required range is in hotspot but mem is cold only
  if (in_hotspot && tree->mem_cold_only_) return;
  iter_vec->push_back(tree->GetMem()->NewIterator());
  tree->GetMem()->Ref();
  *mem = tree->GetMem();
}

void BplusTreeWriteOptState::DoAddImmToIter(BplusTree* tree, bool in_hotspot,
                                          std::vector<Iterator*>* iter_vec, 
                                          MemTable** imm) {
  if (!tree->has_imm_) return;
  // The required range is in hotspot but imm is cold only
  if (in_hotspot && tree->imm_cold_only_) return;
  iter_vec->push_back(tree->GetImm()->NewIterator());
  tree->GetImm()->Ref();
  *imm = tree->GetImm();
}

// Do nothing
void BplusTreeWriteOptState::DoMayAdaptMem(BplusTree* tree,
                                          bool is_hot, int which) {}

void BplusTreeWriteOptState::DoAddLSMTtoIter(BplusTree* tree, bool is_hot,
                                             std::vector<Iterator*>* iter_vec) {
  int level = tree->GetRootLSMT()->GetBottomLevel();
  if (level >= 0) {
    if (is_hot && !tree->GetRootLSMT()->HasHotspot(tree->GetLowHotKey(), tree->GetHighHotKey()))
      return;
    iter_vec->push_back(tree->GetRootLSMT()->NewMergedIterator(ReadOptions()));
    // Bol scan is worse if using below
    // tree->GetRootLSMT()->AddIterators(ReadOptions(), iter_vec);
  }
}

void BplusTreeWriteOptState::DoAddWOTtoIter(BplusTree* tree, int node_level, Node* node,
                                            bool is_hot, std::vector<Iterator*>* iter_vec) {
  bool nonempty_level = node->GetNodeLSMT() != nullptr &&
                        node->GetNodeLSMT()->GetBottomLevel() >= 0;
  if (nonempty_level) {
    if (is_hot && !node->GetNodeLSMT()->HasHotspot(tree->GetLowHotKey(), tree->GetHighHotKey()))
      return;
    iter_vec->push_back(node->GetNodeLSMT()->NewMergedIterator(ReadOptions()));
  }
}

// Do nothing
void BplusTreeWriteOptState::DoMayTriggerWOTAdapt(BplusTree* tree, bool in_hotspot) {}

// Do nothing
void BplusTreeWriteOptState::DoValidate(BplusTree* tree, bool in_hotspot, int non_page_num,
                                        const Comparator* user_comparator) {}

} // namespace WOT_NAMESPACE