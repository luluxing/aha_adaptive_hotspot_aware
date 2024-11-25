#include "../wot_index.h"
#include "wot_buf_mgr/buffer_pool_helper.h"
#include "utils.h"

namespace WOT_NAMESPACE {

void BplusTree::TreeInsert(std::pair<std::string, std::string> item) {
  // std::pair<std::string, std::string> item;
  // if (!hot_item_queue_.try_pop(item) ) {
  //   tree_insertion_scheduled_.store(false);
  //   tree_insertion_finished_signal_.SignalAll();
  //   return;
  // }
  Slice key = Slice(item.first);
  Slice val = Slice(item.second);

  size_t needed = key.size() + val.size() + 3 * ITEMID_SIZE;
  std::stack<uint32_t> insert_stack;
  insert_stack.push(root_pg_id_);
  Status s = SearchDown(&insert_stack, ExtractUserKey(key), val);
  
  if (!s.ok()) {
    fprintf(stderr, "Cannot insert");
    // tree_insertion_scheduled_.store(false);
    // tree_insertion_finished_signal_.SignalAll();
    return;
  }
  // Nodes are locked but not pinned
  uint32_t target_pg_id = insert_stack.top();
  s = TryInsertLeaf(target_pg_id, key, val);
  if (!s.ok()) {
    s = HandleOverflow(&insert_stack, key, val);
    if (!s.ok()) {
      fprintf(stderr, "Error: tree nodes split incorrect");
      // tree_insertion_scheduled_.store(false);
      // tree_insertion_finished_signal_.SignalAll();
      return;
    }  
  } else {
    // Unlock all nodes in the stack, including the leaf
    UnlockParents(&insert_stack);
  }
  std::stack<uint32_t>().swap(insert_stack);
  // tree_insertion_scheduled_.store(false);
  // tree_insertion_finished_signal_.SignalAll();
}

void BplusTree::MayUpdateMinKey(Page cur_page, const Slice& key) {
  assert(((TreePageHeader) cur_page)->is_leaf_ == false);
  Slice mk = PageReadPivotAtOffset(cur_page, 0);
  if (internal_comparator_.user_comparator()->Compare(key, mk) < 0) {
    PageUpdateMinPivot(cur_page, key);
  }
}

// This key is user_key w/o sequence number
Status BplusTree::SearchDown(std::stack<uint32_t>* insert_stack,
                       const Slice& key, const Slice& value) {
  uint32_t cur_pgid, old_pgid;
  cur_pgid = insert_stack->top();
  lock_manager_->WriteLock(cur_pgid);
  
  Page cur_page = buffer_manager_->Pin(cur_pgid);
  bool reach_bottom = ((TreePageHeader) cur_page)->is_leaf_;
  int offset;
  while (!reach_bottom) {
    MayUpdateMinKey(cur_page, key);

    old_pgid = cur_pgid;
    offset = PageFindOffset(cur_page, key);
    assert(offset >= 0);
    cur_pgid = PageReadChildAtOffset(cur_page, offset);
    lock_manager_->WriteLock(cur_pgid);
    buffer_manager_->Unpin(old_pgid);
    cur_page = buffer_manager_->Pin(cur_pgid);
    if (SafeNode(cur_page, key, value)) {
      UnlockParents(insert_stack);
    }
    insert_stack->push(cur_pgid);
    reach_bottom = ((TreePageHeader) cur_page)->is_leaf_;
  }
  buffer_manager_->Unpin(cur_pgid);
  return Status::OK();
}

void BplusTree::UnlockParents(std::stack<uint32_t>* stack) {
  uint32_t n;
  while (!stack->empty()) {
    n = stack->top();
    stack->pop();
    lock_manager_->WriteUnlock(n);
  }
}

bool BplusTree::SafeNode(Page page, const Slice& key, const Slice& value) {
  size_t needed = 0;
  if (((TreePageHeader) page)->is_leaf_) {
    needed = key.size() + sizeof(uint64_t) + value.size() + 3 * ITEMID_SIZE;
  } else {
    needed = key.size() + 4 * ITEMID_SIZE;
  }
  if (PageGetFreeSpace(page) < needed) {
    return false;
  }
  return true;
}

Status BplusTree::TryInsertLeaf(uint32_t pg_id, const Slice& key,
                            const Slice& val) {
  Page page = buffer_manager_->Pin(pg_id);
  size_t space = key.size() + val.size() + 3 * ITEMID_SIZE;
  if (PageGetFreeSpace(page) < space) {
    buffer_manager_->Unpin(pg_id);
    return Status::NotFound("Leaf node overflow");
  }
  PageInsertLeafEntryWithSeq(page, key, val);
  buffer_manager_->Unpin(pg_id);
  return Status::OK();
}

Status BplusTree::HandleOverflow(std::stack<uint32_t>* insert_stack,
                            const Slice& key, const Slice& val) {
  uint32_t target_pg_id = insert_stack->top();
  std::stack<uint32_t> used_stack;
  Status s;
  insert_stack->pop();
  used_stack.push(target_pg_id);
  std::vector<uint32_t> new_pg_ids;
  std::vector<Slice> new_pivots;
  InsertAndSplitLeafPages(target_pg_id, key, val,
                          &new_pg_ids, &new_pivots,
                          internal_comparator_);
  uint32_t obsolete_child = target_pg_id;
  while (insert_stack->size() > 0 && new_pg_ids.size() > 0) {
    target_pg_id = insert_stack->top();
    insert_stack->pop();
    used_stack.push(target_pg_id);
    s = TryInsertInternal(target_pg_id, &new_pivots,
                          &new_pg_ids, obsolete_child);
    if (!s.ok()) {
      InsertAndSplitInternalPages(target_pg_id, obsolete_child,
                                  &new_pg_ids, &new_pivots);
    } else {
      new_pivots.clear();
      new_pg_ids.clear();
    }
    obsolete_child = target_pg_id;
  }
  if (new_pg_ids.size() > 0) {
    // root node has split
    SlicePageMap result;
    for (int i = 0; i < new_pg_ids.size(); i++) {
      result[new_pivots[i]] = new_pg_ids[i];
    }
    UpdateRoot(result);
  }
  // WriteUnlock(root_pg_id_);
  new_pivots.clear();
  new_pg_ids.clear();
  UnlockParents(insert_stack);
  UnlockParents(&used_stack);
  return Status::OK();
}

void BplusTree::InsertAndSplitLeafPages(uint32_t pg_id, const Slice& key,
                              const Slice& val, std::vector<uint32_t>* new_pg_ids,
                              std::vector<Slice>* new_pivots,
                              InternalKeyComparator icmp) {
  Page page = buffer_manager_->Pin(pg_id);
  assert(((TreePageHeader) page)->is_leaf_ == true);
  assert(((TreePageHeader) page)->item_num_ > 0);
  
  int item_num = ((TreePageHeader) page)->item_num_;
  // Find the middle pivot and compare with the inserted one
  // const Slice& mid_pivot = buffer_manager_->GetPivotAtOffset(pg_id, item_num / 2);
  const Slice& mid_pivot = PageReadPivotAtOffset(page, item_num / 2);
  // bool belong_low = icmp.user_comparator()->Compare(key, mid_pivot) < 0;
  int belong_low = icmp.user_comparator()->Compare(ExtractUserKey(key), ExtractUserKey(mid_pivot));
  // Reuse the current node and allocate only one more node
  Page temp = (Page) malloc(PAGE_SIZE);
  memcpy(temp, page, PAGE_SIZE);
  memset(page, 0, PAGE_SIZE);
  PageInit(page, pg_id); // reset current page

  uint32_t child_id = buffer_manager_->Allocate();
  assert(child_id != 0);
  // buffer_manager_->InsertLockTable(child_id);
  lock_manager_->WriteLock(child_id);
  Page child = buffer_manager_->Pin(child_id);
  assert(((TreePageHeader) child)->left_ == 0 &&
              ((TreePageHeader) child)->right_ == 0);
  ((TreePageHeader) child)->is_leaf_ = true;
  ((TreePageHeader) child)->is_dirty_ = true;
  ((TreePageHeader) page)->is_leaf_ = true;
  ((TreePageHeader) page)->is_dirty_ = true;
  new_pg_ids->push_back(pg_id);
  new_pg_ids->push_back(((TreePageHeader) child)->page_id_);

  PageMoveLeafEntries(temp, page, 0, item_num / 2);
  PageMoveLeafEntries(temp, child, item_num / 2, item_num);
  if (belong_low < 0) {
    PageInsertLeafEntryWithSeq(page, key, val);
  } else if (belong_low >= 0) {
    PageInsertLeafEntryWithSeq(child, key, val);
  }
  auto key1 = PageLowkey(page);
  auto key2 = PageLowkey(child);
  char* buf1 = arena_wrapper_.Allocate(key1.size());
  std::memcpy(buf1, key1.data(), key1.size());
  char* buf2 = arena_wrapper_.Allocate(key2.size());
  std::memcpy(buf2, key2.data(), key2.size());

  new_pivots->push_back(ExtractUserKey(Slice(buf1, key1.size())));
  new_pivots->push_back(ExtractUserKey(Slice(buf2, key2.size())));
  ((TreePageHeader) child)->right_ = ((TreePageHeader) temp)->right_;
  ((TreePageHeader) page)->right_ = child_id;
  // BuildSiblingConn(pg_id, (*new_pg_ids)[1]);
  // BuildSiblingConn((*new_pg_ids)[1], ((TreePageHeader) temp)->right_);
  free(temp);

  buffer_manager_->Unpin(pg_id);
  buffer_manager_->Unpin((*new_pg_ids)[1]);
  lock_manager_->WriteUnlock((*new_pg_ids)[1]);  
}

void BplusTree::BuildSiblingConn(uint32_t left, uint32_t right) {
  // These pages are already pinned by the caller
  if (left == 0) {
    Page right_p = buffer_manager_->Lookup(right);
    ((TreePageHeader) right_p)->left_ = 0;
    return;
  }
  if (right == 0) {
    Page left_p = buffer_manager_->Lookup(left);
    ((TreePageHeader) left_p)->right_ = 0;
    return;
  }
  Page left_p = buffer_manager_->Lookup(left);
  Page right_p = buffer_manager_->Lookup(right);
  ((TreePageHeader) left_p)->right_ = right;
  ((TreePageHeader) right_p)->left_ = left;
}

Status BplusTree::TryInsertInternal(uint32_t pg_id,
                                std::vector<Slice>* new_pivots,
                                std::vector<uint32_t>* new_pg_ids,
                                uint32_t old) {
  assert(new_pivots->size() == new_pg_ids->size());
  size_t space = 0;
  // One pivot is removed and two are added, so we need space for one
  for (int i = 0; i < 1; i++) {
    space += new_pivots->at(i).size() + 4 * ITEMID_SIZE;
  }
  Page page = buffer_manager_->Pin(pg_id);
  if (PageGetFreeSpace(page) < space) {
    buffer_manager_->Unpin(pg_id);
    return Status::NotFound("Internal node overflow");
  }
  // Search for the offset for the old child id;
  int offset = PageGetOffsetByChild(page, old);
  if (offset == -1) {
    std::cerr << "Error: cannot find this child\n";
    std::abort();
  }
  PageRemoveOffset(page, offset);
  for (int i = 0; i < new_pivots->size(); i++) {
    PageInsertIndexEntry(page, new_pivots->at(i), new_pg_ids->at(i));
  }
  buffer_manager_->Unpin(pg_id);
  return Status::OK();
}

// We need to also split the buffers if any
void BplusTree::InsertAndSplitInternalPages(
                          uint32_t pg_id, uint32_t old,
                          std::vector<uint32_t>* new_pg_ids,
                          std::vector<Slice>* new_pivots) {
  Page page = buffer_manager_->Pin(pg_id);
  Node* node = nullptr;
  ReadTbb a;
  node_table_.find(a, pg_id);
  node = a->second;
  a.release();
  if (node != nullptr) {
    std::vector<std::pair<Slice, uint32_t>> old_pivots;
    Slice old_pivot = PageReadPivotAtOffset(page, PageGetOffsetByChild(page, old));
    old_pivots.push_back(std::make_pair(old_pivot, old));

    std::vector<SlicePageMap> new_slices;
    SlicePageMap tmp_map;
    for (int i = 0; i < new_pivots->size(); i++) {
      tmp_map[new_pivots->at(i)] = new_pg_ids->at(i);
    }
    new_slices.push_back(tmp_map);
    SlicePageMap result = node->SplitNodeWithBuffer(&new_slices, &old_pivots);
    new_pg_ids->clear();
    new_pivots->clear();
    for (auto it = result.begin(); it != result.end(); it++) {
      new_pivots->push_back(it->first);
      new_pg_ids->push_back(it->second);
    }
  } else {
    fprintf(stderr, "Error: Internal %d should have buffer\n", pg_id);
    std::abort();
  }
  buffer_manager_->Unpin(pg_id);
}

// This node is already write-locked and pinned
SlicePageMap Node::SplitNodeWithBuffer(std::vector<SlicePageMap>* new_pivots,
                          std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  SlicePageMap result;
  Page page = tree_->buffer_manager_->Lookup(pg_id_);
  int split_num = 2;

  std::vector<uint32_t> new_pages;
  for (int i = 0; i < split_num - 1; i++) {
    uint32_t p_id = tree_->buffer_manager_->Allocate();
    Page p = tree_->buffer_manager_->Pin(p_id);
    ((TreePageHeader) p)->is_dirty_ = true;
    new_pages.push_back(((TreePageHeader) p)->page_id_);
    tree_->buffer_manager_->Unpin(p_id);
  }

  std::vector<Slice> guards;
  std::vector<std::shared_ptr<FileMetaData>> out_file_names;
  assert(extra_page_ == nullptr);
  extra_page_ = SplitInternalPages(tree_->buffer_manager_,
                                tree_->arena_wrapper_, pg_id_, new_pivots,
                                old_pivots, &new_pages, &guards);
  Status s;
  if (node_lsmt_ != nullptr) {
    s = BufferCompactTree(&guards, &out_file_names);
    BufferClear();
  }

  for (auto const & new_page : new_pages) {
    LSMTStatus lsmt_status = LSMTStatus::kBuffer;
    auto lsmt_level_lim = tree_->node_lsmt_level_limit_;
    if (node_lsmt_ != nullptr && node_lsmt_->GetLSMTStatus() == LSMTStatus::kSmallLeaf) {
      lsmt_status = LSMTStatus::kSmallLeaf;
      lsmt_level_lim = 1;
    }
    Node* node = new Node(tree_, false, lsmt_level_lim,
                          tree_->env_, tree_->root_lsmt_path_,
                          flush_id_, &tree_->leveldb_options_,
                          tree_->internal_comparator_, tree_->table_cache_,
                          lsmt_status, tree_->leaf_limit_, true);
    if (node_lsmt_ != nullptr && node_lsmt_->GetLSMTStatus() == LSMTStatus::kSmallLeaf) {
      node->node_lsmt_->output_file_size = node_lsmt_->output_file_size / tree_->scale_in_;
    }
    WriteTbb a;
    if (tree_->node_table_.find(a, new_page)) {
      delete a->second;
    }
    tree_->node_table_.insert(a, new_page);
    a->second = node;
    a.release();
    // tree_->node_table_[new_page] = node;
    node->pg_id_ = new_page;
  }

  if (out_file_names.size() > 0) {
    DistributeFilesToNodes(&out_file_names, &new_pages, &guards,
                          nullptr, nullptr);
  }
  assert(guards.size() == new_pages.size() + 1);
  for (int i = 0 ; i < guards.size(); i++) {
    Slice guard = Slice(NewPivot(tree_->arena_wrapper_, guards[i]), guards[i].size());
    if (i == 0) {
      result[guard] = pg_id_;
    } else {
      result[guard] = new_pages[i - 1];
    }
  }

  out_file_names.clear();
  new_pages.clear();
  guards.clear();

  InstallNewBuffer();
  return result;
}

} // namespace WOT_NAMESPACE