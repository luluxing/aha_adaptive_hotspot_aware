#include "btree.h"
#include <random>
#include "wot_buf_mgr/buffer_pool_helper.h"

namespace WOT_NAMESPACE {

Btree::Btree(std::map<std::string, std::string>* props)
: root_page_id_(0),
  tree_height_(1),
  internal_comparator_(InternalKeyComparator(BytewiseComparator())),
  buf_mgr_(NewBufferManager(std::stoi((*props)["buffer_manager_num"]),
                                 (*props)["buffer_file"])),
  lock_mgr_(new TreeLockManager(std::stoi((*props)["buffer_manager_num"]))){
  // assert(buf_mgr_->GetRootPageId() == root_page_id_);
  Page root = buf_mgr_->Pin(root_page_id_);
  // buf_mgr_->InsertLockTable(root_page_id_);
  ((TreePageHeader) root)->is_leaf_ = true;
}

Btree::~Btree() {
  delete buf_mgr_;
}

char* Btree::NewPivot(Slice key) {
  char* buf = arena_wrapper_.Allocate(key.size());
  std::memcpy(buf, key.data(), key.size());
  return buf;
}

Status Btree::Insert(std::string key, std::string val) {
  return Insert(Slice(key), Slice(val));
}

Status Btree::Insert(const Slice& key, const Slice& val) {
  size_t needed = key.size() + val.size() + 3 * ITEMID_SIZE;
  std::stack<uint32_t> insert_stack;
  insert_stack.push(root_page_id_);
  Status s = SearchDown(&insert_stack, key, val, true);
  
  if (!s.ok()) {
    return Status::NotFound("Cannot insert");
  }
  // Nodes are locked but not pinned
  uint32_t target_pg_id = insert_stack.top();
  s = TryInsertLeaf(target_pg_id, key, val);
  if (!s.ok()) {
    s = HandleOverflow(&insert_stack, key, val);
    if (!s.ok()) {
      fprintf(stderr, "Error: tree nodes split incorrect\n");
      return Status::Corruption("Error: tree nodes split incorrect");
    }  
  } else {
    // Unlock all nodes in the stack, including the leaf
    UnlockParents(&insert_stack);
  }
  std::stack<uint32_t>().swap(insert_stack);
  return Status::OK();
}

Status Btree::TryInsertLeaf(uint32_t pg_id, const Slice& key,
                            const Slice& val) {
  Page page = buf_mgr_->Pin(pg_id);
  size_t space = key.size() + val.size() + 3 * ITEMID_SIZE;
  if (PageGetFreeSpace(page) < space) {
    buf_mgr_->Unpin(pg_id);
    return Status::NotFound("Leaf node overflow");
  }
  PageInsertLeafEntry(page, key, val);
  buf_mgr_->Unpin(pg_id);
  return Status::OK();
}

Status Btree::SearchDown(std::stack<uint32_t>* insert_stack,
                       const Slice& key, const Slice& value, bool ins) {
  uint32_t cur_pgid, old_pgid;
  cur_pgid = insert_stack->top();
  lock_mgr_->WriteLock(cur_pgid);
  
  Page cur_page = buf_mgr_->Pin(cur_pgid);
  bool reach_bottom = ((TreePageHeader) cur_page)->is_leaf_;
  int offset;
  while (!reach_bottom) {
    if (ins) {
      MayUpdateHighkey(cur_page, key);
    }
    old_pgid = cur_pgid;
    offset = PageFindOffsetHighkey(cur_page, key);
    assert(offset >= 0);
    cur_pgid = PageReadChildAtOffset(cur_page, offset);
    lock_mgr_->WriteLock(cur_pgid);
    buf_mgr_->Unpin(old_pgid);
    cur_page = buf_mgr_->Pin(cur_pgid);
    if (SafeNode(cur_page, key, value)) {
      UnlockParents(insert_stack);
    }
    insert_stack->push(cur_pgid);
    reach_bottom = ((TreePageHeader) cur_page)->is_leaf_;
  }
  buf_mgr_->Unpin(cur_pgid);
  return Status::OK();
}

bool Btree::SafeNode(Page page, const Slice& key, const Slice& value) {
  size_t needed = 0;
  if (((TreePageHeader) page)->is_leaf_) {
    needed = key.size() + value.size() + 3 * ITEMID_SIZE;
  } else {
    needed = key.size() + 4 * ITEMID_SIZE;
  }
  if (PageGetFreeSpace(page) < needed) {
    return false;
  }
  return true;
}

void Btree::MayUpdateHighkey(Page cur_page, const Slice& key) {
  assert(((TreePageHeader) cur_page)->is_leaf_ == false);
  const Slice& hk = PageHighkey(cur_page);
  if (internal_comparator_.user_comparator()->Compare(hk, key) < 0) {
    PageUpdateHighkey(cur_page, key);
  }
}

void Btree::MayUpdateMinKey(Page cur_page, const Slice& key) {
  assert(((TreePageHeader) cur_page)->is_leaf_ == false);
  Slice mk = PageReadPivotAtOffset(cur_page, 0);
  if (internal_comparator_.user_comparator()->Compare(key, mk) < 0) {
    PageUpdateMinPivot(cur_page, key);
  }
}

// Stack only contains unsafe nodes, not all nodes in the path.
Status Btree::HandleOverflow(std::stack<uint32_t>* insert_stack,
                            const Slice& key, const Slice& val) {
  uint32_t target_pg_id = insert_stack->top();
  std::stack<uint32_t> used_stack;
  Status s;
  insert_stack->pop();
  used_stack.push(target_pg_id);
  std::vector<uint32_t> new_pg_ids;
  std::vector<Slice> new_pivots;
  InsertAndSplitLeafPages(target_pg_id, key, val,
                          new_pg_ids, new_pivots,
                          internal_comparator_);
  uint32_t obsolete_child = target_pg_id;
  while (insert_stack->size() > 0 && new_pg_ids.size() > 0) {
    target_pg_id = insert_stack->top();
    insert_stack->pop();
    used_stack.push(target_pg_id);
    s = TryInsertInternal(target_pg_id, new_pivots,
                          new_pg_ids, obsolete_child);
    if (!s.ok()) {
      InsertAndSplitInternalPages(target_pg_id, obsolete_child,
                                  new_pg_ids, new_pivots);
    } else {
      new_pivots.clear();
      new_pg_ids.clear();
    }
    obsolete_child = target_pg_id;
  }
  if (new_pg_ids.size() > 0) {
    // root node has split
    uint32_t new_id = buf_mgr_->AllocateRoot(root_page_id_);
    lock_mgr_->WriteLock(new_id);
    Page root = buf_mgr_->Pin(root_page_id_);
    Page l = buf_mgr_->Pin(new_id);
    Page r = buf_mgr_->Pin(new_pg_ids[1]);

    BuildSiblingConn(new_id, new_pg_ids[1]);
    buf_mgr_->Unpin(new_id);
    buf_mgr_->Unpin(new_pg_ids[1]);
    PageInsertIndexEntry(root, new_pivots[0], new_id);
    PageInsertIndexEntry(root, new_pivots[1], new_pg_ids[1]);

    // buf_mgr_->InsertLockTable(new_id);
    lock_mgr_->WriteUnlock(new_id);
    tree_height_++;
  }
  // WriteUnlock(root_page_id_);
  new_pivots.clear();
  new_pg_ids.clear();
  UnlockParents(insert_stack);
  UnlockParents(&used_stack);
  return Status::OK();
}

void Btree::InsertAndSplitLeafPages(uint32_t pg_id, const Slice& key,
                              const Slice& val, std::vector<uint32_t>& new_pg_ids,
                              std::vector<Slice>& new_pivots,
                              InternalKeyComparator icmp) {
  Page page = buf_mgr_->Pin(pg_id);
  assert(((TreePageHeader) page)->is_leaf_ == true);
  assert(((TreePageHeader) page)->item_num_ > 0);
  
  int item_num = ((TreePageHeader) page)->item_num_;
  // Find the middle pivot and compare with the inserted one
  const Slice& mid_pivot = PageReadPivotAtOffset(page, item_num / 2);
  int belong_low = icmp.user_comparator()->Compare(key, mid_pivot);
  // Reuse the current node and allocate only one more node
  Page temp = (Page) malloc(PAGE_SIZE);
  memcpy(temp, page, PAGE_SIZE);
  memset(page, 0, PAGE_SIZE);
  PageInit(page, pg_id); // reset current page

  uint32_t child_id = buf_mgr_->Allocate();
  assert(child_id != 0);
  // buf_mgr_->InsertLockTable(child_id);
  lock_mgr_->WriteLock(child_id);
  Page child = buf_mgr_->Pin(child_id);
  assert(((TreePageHeader) child)->left_ == 0 &&
              ((TreePageHeader) child)->right_ == 0);
  ((TreePageHeader) child)->is_leaf_ = true;
  ((TreePageHeader) child)->is_dirty_ = true;
  ((TreePageHeader) page)->is_leaf_ = true;
  ((TreePageHeader) page)->is_dirty_ = true;
  new_pg_ids.push_back(pg_id);
  new_pg_ids.push_back(((TreePageHeader) child)->page_id_);

  PageMoveLeafEntries(temp, page, 0, item_num / 2);
  PageMoveLeafEntries(temp, child, item_num / 2, item_num);
  if (belong_low < 0) {
    PageInsertLeafEntry(page, key, val);
  } else if (belong_low >= 0) {
    PageInsertLeafEntry(child, key, val);
  }
  auto s1 = PageHighkey(page);
  Slice s11 = Slice(NewPivot(s1), s1.size());
  auto s2 = PageHighkey(child);
  Slice s22 = Slice(NewPivot(s2), s2.size());
  new_pivots.push_back(s11);
  new_pivots.push_back(s22);
  ((TreePageHeader) child)->right_ = ((TreePageHeader) temp)->right_;
  ((TreePageHeader) page)->right_ = child_id;
  // BuildSiblingConn(pg_id, (*new_pg_ids)[1]);
  // BuildSiblingConn((*new_pg_ids)[1], ((TreePageHeader) temp)->right_);
  free(temp);

  buf_mgr_->Unpin(pg_id);
  buf_mgr_->Unpin(new_pg_ids[1]);
  lock_mgr_->WriteUnlock(new_pg_ids[1]);  
}

void Btree::BuildSiblingConn(uint32_t left, uint32_t right) {
  // These pages are already pinned by the caller
  if (left == 0) {
    Page right_p = buf_mgr_->Lookup(right);
    ((TreePageHeader) right_p)->left_ = 0;
    return;
  }
  if (right == 0) {
    Page left_p = buf_mgr_->Lookup(left);
    ((TreePageHeader) left_p)->right_ = 0;
    return;
  }
  Page left_p = buf_mgr_->Lookup(left);
  Page right_p = buf_mgr_->Lookup(right);
  ((TreePageHeader) left_p)->right_ = right;
  ((TreePageHeader) right_p)->left_ = left;
}

Status Btree::TryInsertInternal(uint32_t pg_id,
                                std::vector<Slice>& new_pivots,
                                std::vector<uint32_t>& new_pg_ids,
                                uint32_t old) {
  assert(new_pivots.size() == new_pg_ids.size());
  size_t space = 0;
  // One pivot is removed and two are added, so we need space for one
  for (int i = 0; i < 1; i++) {
    space += new_pivots.at(i).size() + 4 * ITEMID_SIZE;
  }
  Page page = buf_mgr_->Pin(pg_id);
  if (PageGetFreeSpace(page) < space) {
    buf_mgr_->Unpin(pg_id);
    return Status::NotFound("Internal node overflow");
  }
  // Search for the offset for the old child id;
  int offset = PageGetOffsetByChild(page, old);
  if (offset == -1) {
    fprintf(stderr, "Error: cannot find this child\n");
    std::abort();
  }
  PageRemoveOffset(page, offset);
  for (int i = 0; i < new_pivots.size(); i++) {
    PageInsertIndexEntry(page, new_pivots.at(i), new_pg_ids.at(i));
  }
  buf_mgr_->Unpin(pg_id);
  return Status::OK();
}

void Btree::InsertAndSplitInternalPages(
                          uint32_t pg_id, uint32_t old,
                          std::vector<uint32_t>& new_pg_ids,
                          std::vector<Slice>& new_pivots) {
  Page page = buf_mgr_->Pin(pg_id);
  assert(((TreePageHeader) page)->is_leaf_ == false);
  assert(((TreePageHeader) page)->item_num_ > 0);

  // Search for the offset for the old child id;
  int offset = PageGetOffsetByChild(page, old);
  if (offset == -1) {
    fprintf(stderr, "Error: cannot find this child\n");
    std::abort();
  }
  PageRemoveOffset(page, offset);
  int item_num = ((TreePageHeader) page)->item_num_;
  bool belong_low = offset < item_num / 2;

  // Only split to two new nodes
  std::vector<uint32_t> child_pg_ids;
  std::vector<Slice> child_pivots;
  // Reuse the current node and allocate only one more node
  Page temp = (Page) malloc(PAGE_SIZE);
  memcpy(temp, page, PAGE_SIZE);
  memset(page, 0, PAGE_SIZE);
  PageInit(page, pg_id); // reset current page

  uint32_t child_id = buf_mgr_->Allocate();
  Page child = buf_mgr_->Pin(child_id);
  ((TreePageHeader) child)->is_dirty_ = true;
  ((TreePageHeader) page)->is_dirty_ = true;
  child_pg_ids.push_back(pg_id);
  child_pg_ids.push_back(((TreePageHeader) child)->page_id_);

  PageMoveIndexEntries(temp, page, 0, item_num / 2);
  PageMoveIndexEntries(temp, child, item_num / 2, item_num);
  if (belong_low) {
    PageInsertIndexEntry(page, new_pivots.at(0), new_pg_ids.at(0));
    PageInsertIndexEntry(page, new_pivots.at(1), new_pg_ids.at(1));
  } else {
    PageInsertIndexEntry(child, new_pivots.at(0), new_pg_ids.at(0));
    PageInsertIndexEntry(child, new_pivots.at(1), new_pg_ids.at(1));
  }
  auto s1 = PageHighkey(page);
  Slice s11 = Slice(NewPivot(s1), s1.size());
  auto s2 = PageHighkey(child);
  Slice s22 = Slice(NewPivot(s2), s2.size());
  child_pivots.push_back(s11);
  child_pivots.push_back(s22);
  free(temp);

  new_pg_ids.clear();
  new_pivots.clear();
  for (int i = 0; i < child_pg_ids.size(); i++) {
    new_pg_ids.push_back(child_pg_ids[i]);
    new_pivots.push_back(child_pivots[i]);
  }
  child_pg_ids.clear();
  child_pivots.clear();

  buf_mgr_->Unpin(pg_id);
  buf_mgr_->Unpin(new_pg_ids.at(1));
  // WriteUnlock(pg_id);
  // buf_mgr_->InsertLockTable(new_pg_ids->at(1));
}

void Btree::UnlockParents(std::stack<uint32_t>* stack) {
  uint32_t n;
  while (!stack->empty()) {
    n = stack->top();
    stack->pop();
    lock_mgr_->WriteUnlock(n);
  }
}

Status Btree::Query(std::string key, std::string* val) {
  std::stack<uint32_t> query_stack;
  query_stack.push(root_page_id_);
  Status s = SearchDown(&query_stack, key, Slice(), false);
  if (!s.ok()) {
    return Status::NotFound("Key not found");
  }

  uint32_t pg_id = query_stack.top();
  const Slice& ret = GetValueWithKey(buf_mgr_, pg_id, arena_wrapper_, key);
  if (ret.size() == 0) {
    return Status::NotFound("Key not found");
  }
  *val = ret.ToString();
  return Status::OK();
}

Status Btree::Update(std::string key, std::string val) {
  return Insert(key, val);
}

Status Btree::Delete(std::string key) {
  std::stack<uint32_t> query_stack;
  query_stack.push(root_page_id_);
  SearchDown(&query_stack, key, Slice(), false);

  uint32_t pg_id = query_stack.top();
  return DeleteEntry(buf_mgr_, pg_id, key);
}


class Btree::TreeIterator : public Iterator {
 public:
  TreeIterator(BufferManager* mgr, uint32_t root,
               TreeLockManager* lock_mgr, bool print_page)
  : tree_buf_mgr_(mgr),
    tree_lock_mgr_(lock_mgr),
    cur_pg_id_(root),
    start_node_(root),
    cur_page_(nullptr),
    cur_offset_(0),
    valid_(false),
    print_page_(print_page),
    status_(Status::OK()) {}

  ~TreeIterator() {
    // if (((double) rand() / (RAND_MAX)) < 0.00001) {
    //   fprintf(stdout, "Page num %d, %d\n", page_num_, ((TreePageHeader) cur_page_)->item_num_);
    // }
    if (status_.ok()) {
      tree_buf_mgr_->UnpinAndRelease(cur_pg_id_);
      tree_lock_mgr_->ReadUnlock(cur_pg_id_);
    }
  }

  void Seek(const Slice& target) {
    status_ = Status::OK();
    cur_pg_id_ = start_node_;

    std::stack<uint32_t> query_stack;
    query_stack.push(cur_pg_id_);
    SearchDown(&query_stack, target);

    cur_pg_id_ = query_stack.top();
    std::stack<uint32_t>().swap(query_stack);

    // it is already locked and pinned
    if (SearchInCurNode(target)) {
      return;
    }
    MoveRight();
  }

  void SeekToFirst() {}
  void SeekToLast() {}
  void Prev() {}

  void Next() {
    // node is still locked and pinned
    cur_offset_++;
    if (cur_offset_ >= ((TreePageHeader) cur_page_)->item_num_) {
      MoveRight();
    }
  }

  bool Valid() const { return valid_; }

  Slice key() const {
    assert(valid_);
    return PageReadPivotAtOffset(cur_page_, cur_offset_);
  }

  Slice value() const {
    assert(valid_);
    return PageReadValueAtOffset(cur_page_, cur_offset_);
    // return tree_buf_mgr_->GetValueAtOffset(cur_pg_id_, cur_offset_);
  }

  Status status() const { return status_; }

 private:
  // cur_page_ should already be locked and pinned
  bool SearchInCurNode(const Slice& key) {
    // tree_->ReadLock(cur_pg_id_);
    // cur_page_ = tree_buf_mgr_->Pin(cur_pg_id_);
    assert(((TreePageHeader) cur_page_)->is_leaf_ == true);
    if (key.empty()) {
      cur_offset_ = 0;
    } else {
      cur_offset_ = PageFindOffsetForScanHighkey(cur_page_, key);
    }
    const Slice& c = PageReadPivotAtOffset(cur_page_, cur_offset_);
    if (key.size() > 0 && c.compare(key) < 0 /*&& c.compare(up_) <= 0*/) {
      valid_ = false;
      return false;
    }
    if (print_page_) {
      fprintf(stdout, "leaf-%d-%d\n",
              ((TreePageHeader) cur_page_)->page_id_, ((TreePageHeader) cur_page_)->item_num_);
    }
    valid_ = true;
    page_num_++;
    return true;
  }

  void SearchDown(std::stack<uint32_t>* insert_stack, const Slice& key) {
    uint32_t cur_pgid, old_pgid;
    cur_pgid = insert_stack->top();
    tree_lock_mgr_->ReadLock(cur_pgid);
    Page cur_page = tree_buf_mgr_->Pin(cur_pgid);
    bool reach_bottom = ((TreePageHeader) cur_page)->is_leaf_;
    int offset;
    // Slice key = queried_key;
    while (!reach_bottom) {
      old_pgid = cur_pgid;
      offset = PageFindOffsetHighkey(cur_page, key);
      assert(offset >= 0);
      cur_pgid = PageReadChildAtOffset(cur_page, offset);

      tree_lock_mgr_->ReadLock(cur_pgid);
      tree_buf_mgr_->Unpin(old_pgid);
      tree_lock_mgr_->ReadUnlock(old_pgid);
      cur_page = tree_buf_mgr_->Pin(cur_pgid);

      insert_stack->push(cur_pgid);
      reach_bottom = ((TreePageHeader) cur_page)->is_leaf_;
    }
    // tree_buf_mgr_->Unpin(cur_pgid);
    cur_page_ = tree_buf_mgr_->Lookup(cur_pgid);
  }

  void MoveRight() {
    bool rightmost = PageIsRightMost(cur_page_);
    bool finish_early = false;
    if (!rightmost) {
      // Lock the right sibling and release own lock.
      // However if failed at lock right sibling, abort and restart
      int old_id = cur_pg_id_;
      cur_pg_id_ = ((TreePageHeader) cur_page_)->right_;
      // In case there is a write on the right sibling
      if (!tree_lock_mgr_->TryReadLock(cur_pg_id_)) {
        tree_buf_mgr_->UnpinAndRelease(old_id);
        tree_lock_mgr_->ReadUnlock(old_id);
        valid_ = false;
        status_ = Status::NotFound("Cannot lock");
        return;
      }
      tree_lock_mgr_->ReadUnlock(old_id);
      tree_buf_mgr_->UnpinAndRelease(old_id);
      cur_page_ = tree_buf_mgr_->Pin(cur_pg_id_);
      assert(((TreePageHeader) cur_page_)->is_leaf_);
      if (SearchInCurNode(Slice())) {
        return;
      }
    }
    valid_ = false;
  }

  // Btree* tree_;
  BufferManager* tree_buf_mgr_;
  TreeLockManager* tree_lock_mgr_;

  uint32_t cur_pg_id_;
  uint32_t start_node_;
  Page cur_page_;
  uint32_t cur_offset_;
  
  bool valid_;
  Status status_;

  int page_num_ = 0;
  bool print_page_;
};

void Btree::PrintNode(uint32_t page_id, int level) {
  Page page = buf_mgr_->Lookup(page_id);
  if (level == 1) {
    PagePrint(page);
  } else {
    std::vector<uint32_t> child_ids;
    GetChildPageId(buf_mgr_, page_id, &child_ids);
    for (auto const& child_id: child_ids) {
      PrintNode(child_id, level - 1);
    }
  }
}

void Btree::PrintNodeStat(uint32_t page_id, int level) {
  Page page = buf_mgr_->Lookup(page_id);
  if (level == 1) {
    PagePrintStat(page);
  } else {
    std::vector<uint32_t> child_ids;
    GetChildPageId(buf_mgr_, page_id, &child_ids);
    for (auto const& child_id: child_ids) {
      PrintNodeStat(child_id, level - 1);
    }
  }
}

void Btree::Print() {
  int h = tree_height_;
  fprintf(stdout, "Tree height %d\n", h);
  // for (int i = 1; i <= h; i++) {
  //   std::cout << "Height-" << i << std::endl;
  //   PrintNode(root_page_id_, i);
  // }

  size_t total_size = 0;
  // Btree nodes (buffer manager)
  // total_size += buf_mgr_->TotalMemoryUsage();
  // std::cout << "After adding buffer manager: " << total_size << "\n";
  // total_size += buf_mgr_->TotalLockTableUsage();
  // std::cout << "After adding lock table: " << total_size << "\n";
}

void Btree::PrintStat() {
  int h = tree_height_;
  fprintf(stdout, "Tree height %d\n", h);
  // for (int i = 1; i <= h; i++) {
  //   std::cout << "Height-" << i << std::endl;
  //   PrintNodeStat(root_page_id_, i);
  // }
  size_t total_size = 0;
  // Btree nodes (buffer manager)
  // total_size += buf_mgr_->TotalMemoryUsage();
  // std::cout << "After adding buffer manager: " << total_size << "\n";
  // total_size += buf_mgr_->TotalLockTableUsage();
  // std::cout << "After adding lock table: " << total_size << "\n";
  // buf_mgr_->PrintStat();
  // lock_mgr_->PrintStat();
}

Iterator* Btree::NewBtreeIterator() {
  return new TreeIterator(buf_mgr_, root_page_id_, lock_mgr_, print_page_);
}



} // namespace WOT_NAMESPACE