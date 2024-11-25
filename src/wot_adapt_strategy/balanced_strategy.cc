#include "default_strategy.h"
#include "wot_index.h"

namespace WOT_NAMESPACE {

bool BalancedAdapt::CanProceedAdapt(Node* node, int* adapt_node) {
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

  if (*adapt_node == tree_->root_pg_id_) {
    return false;
  }
  
  ReadTbb a;
  if (!tree_->node_table_.find(a, *adapt_node)) {
    return false;
  }
  node = a->second;// node_table_[adapt_node];
  a.release();

  // node is non-nullptr
  if (node->GetNodeLSMT() == nullptr || node->GetNodeLSMT()->GetBottomLevel() < 0) {
    return false;
  }
  return true;
}

void BalancedAdapt::AdaptWOT() {
  int adapt_node;
  Node* node;
  if (!CanProceedAdapt(node, &adapt_node)) {
    return;
  }
  ReadTbb a;
  if (tree_->node_table_.find(a, adapt_node)) {
    node = a->second;
  } else {
    return;
  }
  a.release();

  if (node == nullptr || node->GetNodeLSMT() == nullptr  ||
      node->pg_id_ != adapt_node ||
      node->GetNodeLSMT()->GetBottomLevel() < 0) {
    return;
  }

  std::vector<uint32_t> nodes_pool;
  nodes_pool.push_back(tree_->root_pg_id_);
  tree_->lock_manager_->ReadLock(tree_->root_pg_id_);

  NewFindNodePath(adapt_node, node, &nodes_pool);
  Node* cur_node = node;
  Page page = tree_->buffer_manager_->Pin(cur_node->pg_id_);
  bool leaf_page = ((TreePageHeader) page)->is_leaf_;

  if (cur_node == nullptr || cur_node->GetNodeLSMT() == nullptr  ||
      cur_node->pg_id_ != nodes_pool[nodes_pool.size() - 1] ||
      node->GetNodeLSMT()->GetBottomLevel() < 0) {
    tree_->buffer_manager_->Unpin(cur_node->pg_id_);
    for (auto const& node_pair : nodes_pool) {
      tree_->lock_manager_->ReadUnlock(node_pair);
    }
    return;
  }
  
  bool small_leaf = leaf_page &&
                    cur_node->GetNodeLSMT()->GetLSMTStatus() == LSMTStatus::kSmallLeaf;
  std::vector<std::shared_ptr<FileMetaData>> files;
  int level_of_files = -1;
  if (!leaf_page) {
    // Not the original large leaf nor the small leaf
    CompactNodeBufferForFlush(cur_node, page, &level_of_files, &files);
    if (files.size() == 0) {
      tree_->buffer_manager_->Unpin(cur_node->pg_id_);
      for (auto const& node_pair : nodes_pool) {
        tree_->lock_manager_->ReadUnlock(node_pair);
      }
      return;
    }
  }
  std::vector<SlicePageMap> new_pivots;
  std::vector<std::pair<Slice, uint32_t>> old_pivots;
  if (!leaf_page) {
    assert(level_of_files >= 0);
    cur_node->FlushFilesToChildren(level_of_files, &files, &new_pivots, &old_pivots);
    files.clear();
  }
  // cur_node->node_lsmt_->ClearBottomLevelFiles();
  Node* tmp_node = cur_node;
  bool root_split = false;
  int pool_idx = nodes_pool.size() - 1;
  while (new_pivots.size() > 0 || leaf_page) {
    int pg_id = tmp_node->pg_id_;
    if (new_pivots.size() > 0) {
      tmp_node->UpdatePivots(&new_pivots, &old_pivots);
    }
    SlicePageMap result;
    if (small_leaf) {
      assert(new_pivots.size() == 0 && old_pivots.size() == 0);
      result = tmp_node->SplitSmallLeaf();
      if (result.size() == 0) {
        delete tmp_node;
      }
    } else if (leaf_page) {
      assert(new_pivots.size() == 0 && old_pivots.size() == 0);
      result = tmp_node->SplitLeafAndLSMT();
      // assert(result.size() > 1);
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
      if (leaf_page) {
        tree_->buffer_manager_->UnpinAndRelease(pg_id);  
      } else {
        tree_->buffer_manager_->Unpin(pg_id);
      }
      assert(pool_idx > 0);
      // assert(tmp_node->pg_id_ == nodes_pool[pool_idx]);
      pool_idx--;
      uint32_t parent_id = nodes_pool[pool_idx];
      ReadTbb a;
      if (tree_->node_table_.find(a, parent_id)) {
        tmp_node = a->second;
      } else {
        fprintf(stdout, "Error: parent node not found\n");
        break;
      }
      a.release();
      Page p = tree_->buffer_manager_->Pin(tmp_node->pg_id_);
      leaf_page = ((TreePageHeader) p)->is_leaf_;
      small_leaf = leaf_page &&
                   tmp_node->GetNodeLSMT()->GetLSMTStatus() == LSMTStatus::kSmallLeaf;

      new_pivots.push_back(result);
      int off = PageGetOffsetByChild(p, pg_id);
      assert(off >= 0);
      const Slice& op = PageReadPivotAtOffset(p, off);
      // verify the pivot matches the split results
      auto k = result.begin()->first;
      assert(tree_->internal_comparator_.user_comparator()->Compare(op, k) == 0);
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
      tree_->lock_manager_->ReadUnlock(*it);
    }
  }
  if (!root_split) {
    tree_->buffer_manager_->Unpin(tmp_node->pg_id_);
  }
}

void BalancedAdapt::NewFindNodePath(uint32_t adapt_node, Node* node,
                                      std::vector<uint32_t>* nodes_pool) {
  Page adapt_page = tree_->buffer_manager_->Pin(adapt_node);
  Slice min_key;
  if (node->GetNodeLSMT() != nullptr && node->GetNodeLSMT()->GetBottomLevel() >= 0) {
    min_key = node->GetNodeLSMT()->GetOneUserKey();
  } else {
    int off = ((TreePageHeader) adapt_page)->item_num_ > 1 ? 1 : 0;
    min_key = PageReadPivotAtOffset(adapt_page, off);
  }
  assert(min_key.size() > 0);

  uint32_t cur_node_id;
  int pool_idx = 0;
  while (pool_idx < nodes_pool->size()) {
    cur_node_id = nodes_pool->at(pool_idx);
    if (cur_node_id == adapt_node) {
      break;
    }
    Page p = tree_->buffer_manager_->Pin(cur_node_id);
    if (((TreePageHeader) p)->is_leaf_) {
      tree_->buffer_manager_->Unpin(cur_node_id);
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
    assert(tree_->internal_comparator_.user_comparator()
            ->Compare(min_key, PageReadPivotAtOffset(p, i-1)) >= 0);
    uint32_t child = PageReadChildAtOffset(p, i - 1);
    tree_->buffer_manager_->Unpin(cur_node_id);
    nodes_pool->push_back(child);
    tree_->lock_manager_->ReadLock(child);

    pool_idx++;
  }
  tree_->buffer_manager_->Unpin(adapt_node);
}

}  // namespace WOT_NAMESPACE