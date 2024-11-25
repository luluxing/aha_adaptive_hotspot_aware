#include "leveldb/table/merger.h"

#include "buffer_tree.h"
#include "../util/utils.h"


namespace WOT_NAMESPACE {

#ifdef INCLUDE_BUFFERTREE
BufferTree::BufferTree(Options options, const std::string& dbname,
                     std::atomic<uint64_t>& flush_id,
                     bool is_lsmt, bool is_buffer_tree)
  : BplusTree(options, dbname, flush_id, is_lsmt, is_buffer_tree),
    block_num_(options.block_num) {
    assert(leveldb_lsmt_ == nullptr);
    assert(root_->GetNodeLSMT() != nullptr);
    delete btree_state_;
    btree_state_ = new BufferTreeState();
  }

Status BufferTree::MakeRoomForWrite(bool force) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  bool allow_delay = !force;
  // Status s;
  while (true) {
    if (!force &&
        (mem_->ApproximateMemoryUsage() <= memtable_size_)) {
      // There is room in current memtable
      break;
    } else if (imm_ != nullptr) {
      background_work_finished_signal_.Wait();
    } else {
      // Attempt to switch to a new memtable and trigger compaction of old
      imm_ = mem_;
      has_imm_.store(true, std::memory_order_release);
      mem_ = new MemTable(internal_comparator_);
      mem_->Ref();
      mem_empty_.store(true, std::memory_order_release);
      imm_cold_only_.store(mem_cold_only_.load());
      mem_cold_only_.store(true, std::memory_order_release);
      force = false;  // Do not force another compaction if have room
      MaybeScheduleCompaction();
    }
  }
  return Status::OK();
}

void BufferTree::MaybeScheduleCompaction() {
  mutex_.AssertHeld();
  if (background_compaction_scheduled_) {
    // Already scheduled
  } else if (shutting_down_.load(std::memory_order_acquire)) {
    // DB is being deleted; no more background compactions
  } else if (imm_ == nullptr &&
             root_->GetNodeLSMT()->GetLevel0FileNum() < block_num_) {
    // No work to be done
  } else {
    background_compaction_scheduled_ = true;
    scheduled_compaction_num_.fetch_add(1);
    env_->Schedule(&BplusTree::BGWork, this);
  }
}

void BufferTree::BackgroundCall() {
  MutexLock l(&mutex_);

  assert(background_compaction_scheduled_);
  if (shutting_down_.load(std::memory_order_acquire)) {
    // No more background work when shutting down.
  } else {
    assert(imm_ != nullptr);
    CompactMemTable();
  }

  background_compaction_scheduled_ = false;

  // Previous compaction may have produced too many files in a level,
  // so reschedule another compaction if needed.
  MaybeScheduleCompaction();
  background_work_finished_signal_.SignalAll();
  scheduled_compaction_num_.fetch_sub(1);
}

bool BufferTree::FinishUsingImm() {
  bool has_hotspot = !imm_cold_only_.load();
  // Commit to the new state
  imm_->Unref();
  imm_ = nullptr;
  has_imm_.store(false, std::memory_order_release);
  imm_cold_only_.store(true, std::memory_order_release);
  return has_hotspot;
}

void BufferTree::BufferTreeConstructRoot(Iterator* iter) {
  iter->SeekToFirst();
  auto k = iter->key();
  auto res = WriteToPage(this, &internal_comparator_, iter, 0);
  tree_height_ += 1;

  int new_entry_num = res.size();
  int cur_entry_num = 0;
  size_t raw = PAGE_SIZE - sizeof(TreePageHeaderData);
  size_t entry_size = k.size() + 3 * ITEMID_SIZE;
  int capacity = raw / entry_size;
  int split_num = (new_entry_num + capacity - 1) / capacity;
  int page_item = new_entry_num / split_num + 1 > capacity ?
                  capacity : new_entry_num / split_num + 1;
  int page_id = 0;
  Page new_page = nullptr;
  SlicePageMap result;
  for (auto it = res.begin(); it != res.end(); it++) {
    if (page_id == 0 || ((TreePageHeader) new_page)->item_num_ >= page_item) {
      if (page_id != 0) {
        buffer_manager_->UnpinAndRelease(page_id);
      }
      // Create new node
      page_id = buffer_manager_->Allocate();
      Node* node = new Node(this, false, 1,
                            env_, root_lsmt_path_,
                            flush_id_, &leveldb_options_,
                            internal_comparator_, table_cache_,
                            LSMTStatus::kBuffer, 1, true);
      WriteTbb a;
      if (node_table_.find(a, page_id)) {
        delete a->second;
      }
      node_table_.insert(a, page_id);
      a->second = node;
      a.release();
      node->pg_id_ = page_id;

      new_page = buffer_manager_->Pin(page_id);
      ((TreePageHeader) new_page)->is_leaf_ = false;
      Slice piv = Slice(NewPivot(this->arena_wrapper_, it->first), it->first.size());
      result[piv] = page_id;
    }
    PageInsertIndexEntry(new_page, it->first, it->second);
  }
  tree_height_ += 1;
  buffer_manager_->UnpinAndRelease(page_id);
  lock_manager_->WriteLock(root_pg_id_);
  Page rt = buffer_manager_->Lookup(root_pg_id_);
  assert(((TreePageHeader) rt)->item_num_ == 0);
  ((TreePageHeader) rt)->is_leaf_ = false;
  for (auto it = result.begin(); it != result.end(); it++) {
    PageInsertIndexEntry(rt, it->first, it->second);
  }
  lock_manager_->WriteUnlock(root_pg_id_);
}

void BufferTree::CompactMemTable() {
  mutex_.AssertHeld();
  assert(imm_ != nullptr);
  Iterator* iter = imm_->NewIterator();

  // If the index is buffer tree and this is the first file
  // we need to use this imm to construct pages
  if (flush_id_.load() == 1) {
    fprintf(stdout, "Buffer tree first install pages\n");
    mutex_.Unlock();
    BufferTreeConstructRoot(iter);
    mutex_.Lock();
    FinishUsingImm();
    flush_id_.fetch_add(1);
    return;
  }
  FileMetaData meta;
  meta.number = flush_id_.fetch_add(1) + 1;
  meta.file_entry = imm_->GetEntryNum();
  pending_outputs_.insert(meta.number);
  Options opt;
  opt.comparator = &internal_comparator_;
  Status s;
  {
    mutex_.Unlock();
    s = BuildTable(root_lsmt_path_, env_, opt, table_cache_, iter, &meta);
    mutex_.Lock();
  }
  if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
    s = Status::IOError("Deleting DB during memtable compaction");
  }
  if (!s.ok()) {
    fprintf(stderr, "Error: flush table: %s", s.ToString().c_str());
    // std::abort();
  }
  delete iter;
  pending_outputs_.erase(meta.number);

  // lock_manager_->WriteLock(-1);
  bool has_hotspot = !imm_cold_only_.load();
  if (s.ok()) {
    // Commit to the new state
    imm_->Unref();
    imm_ = nullptr;
    has_imm_.store(false, std::memory_order_release);
    imm_cold_only_.store(true, std::memory_order_release);
  }

  AddFileToRoot(meta);
}

// Add file to root LSMT and empty buffer if it overflows
void BufferTree::AddFileToRoot(FileMetaData meta) {
  mutex_.AssertHeld();
  lock_manager_->ReadLock(root_pg_id_);
  Page rt = buffer_manager_->Lookup(root_pg_id_);
  
  {
    mutex_.Unlock();
    lock_manager_->EscalateLock(root_pg_id_);
    if (root_->GetNodeLSMT()->AppendFile(0, meta) >= block_num_) {
      // Trigger buffer emptying process
      std::vector<std::shared_ptr<FileMetaData>> files;
      root_->GetNodeLSMT()->CompactBottomLevel(rt);
      root_->GetNodeLSMT()->RemoveObsoleteFiles();
      int bottom_level = root_->GetNodeLSMT()->GetAllBottomLevelFiles(&files);
      root_->MayUpdateMinPivot(rt, files[0]->smallest.user_key());
      lock_manager_->AlleviateLock(root_pg_id_);
      assert(bottom_level == 0 && files.size() > 0);

      // root node will be unlocked during the process
      FlushLSMTfilesToBol(bottom_level, &files);
      files.clear();
    } else {
      lock_manager_->AlleviateLock(root_pg_id_);
      lock_manager_->ReadUnlock(root_pg_id_);
    }
    mutex_.Lock();
  }
}

// Root is write-locked
void BufferTree::UpdateRoot(SlicePageMap result) {
  // buffer_manager_->WriteLock(root_pg_id_);
  root_->InstallNewPage();
  uint32_t new_id = buffer_manager_->AllocateRoot(root_pg_id_);
  lock_manager_->WriteLock(new_id);
  delete root_;
  Node* new_node = new Node(this, false, /*level_limit*/1,
                          env_, root_lsmt_path_, flush_id_,
                          &leveldb_options_, internal_comparator_, table_cache_,
                          LSMTStatus::kBuffer, leaf_limit_, true);
  new_node->pg_id_ = new_id;
  WriteTbb wt;
  node_table_.insert(wt, new_id);
  wt->second = new_node;
  wt.release();
  Page root_pg = buffer_manager_->Pin(root_pg_id_);
  for (auto it = result.begin(); it != result.end(); it++) {
    uint32_t id = it == result.begin() ? new_id : it->second;
    PageInsertIndexEntry(root_pg, it->first, id);
  }
  lock_manager_->WriteUnlock(new_id);
  root_ = new Node(this, false, /*level_limit*/1,
                    env_, root_lsmt_path_, flush_id_,
                    &leveldb_options_, internal_comparator_, table_cache_,
                    LSMTStatus::kRootBuffer, leaf_limit_, true);
  root_->pg_id_ = root_pg_id_;
  node_table_.erase(root_pg_id_);
  WriteTbb a;
  node_table_.insert(a, root_pg_id_);
  a->second = root_;
  a.release();
  tree_height_.fetch_add(1);
  for (int j = 0; j < nodes_in_progress.size(); j++) {
    ReadTbb a;
    node_table_.find(a, nodes_in_progress[j]);
    a->second->InstallNewBuffer();
    a->second->node_overflow_ = false;
    a->second->node_above_leaf = false;
  }
  nodes_in_progress.clear();
  // buffer_manager_->WriteUnlock(root_pg_id_);
}

// Node is already read-locked and pinned
bool BufferTree::AllChildrenLeaf(uint32_t pg_id) {
  Page parent_pg = buffer_manager_->Lookup(pg_id);
  if (((TreePageHeader) parent_pg)->is_leaf_ || ((TreePageHeader) parent_pg)->item_num_ == 0) {
    return false;
  }
  int item_num = ((TreePageHeader) parent_pg)->item_num_;
  for (int i = 0; i < item_num; i++) {
    uint32_t child_id = PageReadChildAtOffset(parent_pg, i);
    Page child_pg = buffer_manager_->Pin(child_id);
    if (!((TreePageHeader) child_pg)->is_leaf_) {
      buffer_manager_->Unpin(child_id);
      if (i > 0) {
        fprintf(stderr, "Error: first child is leaf but %d-th child is not\n", i);
        std::abort();
      }
      return false;
    }
    buffer_manager_->Unpin(child_id);
  }
  ReadTbb a;
  Node* parent_node = nullptr;
  if (node_table_.find(a, pg_id)) {
    parent_node = a->second;
  }
  a.release();
  parent_node->node_above_leaf = true;
  return true;
}

void BufferTree::MergeWithAllChildrenLeaf(
    uint32_t pg_id,
    int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  // Put all files and all children leaf into one vector
  std::vector<Iterator*> iter_vec;
  int entry_num = 0;
  for (auto const& f : *files) {
    iter_vec.push_back(table_cache_->NewIterator(ReadOptions(),
                                          f->number, f->file_size));
    entry_num += f->file_entry;
  }
  AddPageIterators(pg_id, &iter_vec);
  ReadTbb a;
  Node* parent_node = nullptr;
  if (node_table_.find(a, pg_id)) {
    parent_node = a->second;
  }
  a.release();

  Iterator* leaf_iter = NewMergingIterator(
                &internal_comparator_, &iter_vec[0], iter_vec.size());
  SlicePageMap result = WriteToPage(this, &internal_comparator_,leaf_iter, 0);
  
  parent_node->BufferFinalizeMergeLeaf(level_of_files, files);
  assert(result.size() > 1);
  
  if (old_pivots != nullptr && new_pivots != nullptr) {
    new_pivots->push_back(result);
  }

  Page parent_pg = buffer_manager_->Lookup(pg_id);
  int item_num = ((TreePageHeader) parent_pg)->item_num_;
  for (int i = 0; i < item_num; i++) {
    lock_manager_->ReadUnlock(PageReadChildAtOffset(parent_pg, i));
  }
  
  nodes_in_progress.push_back(pg_id);
}

void BufferTree::AddPageIterators(uint32_t pg_id, std::vector<Iterator*>* iters) {
  auto pi = NewPageIterator();
  Page parent_pg = buffer_manager_->Lookup(pg_id);
  int item_num = ((TreePageHeader) parent_pg)->item_num_;
  int total = 0;
  for (int i = 0; i < item_num; i++) {
    uint32_t child_id = PageReadChildAtOffset(parent_pg, i);
    lock_manager_->ReadLock(child_id);
    ((PageIterator*) pi)->AddPage(child_id);
    Page p = buffer_manager_->Pin(child_id);
    total += ((TreePageHeader) p)->item_num_;
    buffer_manager_->Unpin(child_id);
  }
  iters->push_back(pi);
}

int BufferTree::EqualSplitInternalPage(
    Page p, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots) {
  ReadTbb a;
  Node* parent_node = nullptr;
  if (node_table_.find(a, ((TreePageHeader) p)->page_id_)) {
    parent_node = a->second;
  }
  a.release();
  if (!parent_node->node_above_leaf) {
    return BplusTree::EqualSplitInternalPage(p, new_pivots, old_pivots);
  }
  assert(old_pivots->size() == 0);

  int split_num;
  int new_entry_num = 0;
  for (auto it = new_pivots->begin(); it != new_pivots->end(); it++) {
    new_entry_num += it->size();
  }
  // int cur_entry_num = ((TreePageHeader) p)->item_num_;
  size_t raw = PAGE_SIZE - sizeof(TreePageHeaderData);
  size_t entry_size = new_pivots->at(0).begin()->first.size() + 3 * ITEMID_SIZE;
  int capacity = raw / entry_size;
  int total_entry_num = new_entry_num ;
  split_num = (total_entry_num + capacity - 1) / capacity;
  return split_num;
}

// Page is already pinned and is already write-locked
void BufferTree::UpdateOrRewritePivots(
    uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  ReadTbb a;
  Node* parent_node = nullptr;
  if (node_table_.find(a, pg_id)) {
    parent_node = a->second;
  }
  a.release();
  if (!parent_node->node_above_leaf) {
    BufferTreeUpdatePivots(pg_id, new_pivots, old_pivots);
    return;
  }
  if (!(parent_node->node_above_leaf && old_pivots->size() == 0)) {
    fprintf(stdout, "%ld\n", old_pivots->size());
    parent_node->NodePrint(1);
  }
  assert(parent_node->node_above_leaf && old_pivots->size() == 0);
  // Buffer tree specific: clear the page and rewrite the pivots with new_pivots
  Page p = buffer_manager_->Lookup(pg_id);
  PageClearContent(p);

  for (auto it = new_pivots->begin(); it != new_pivots->end(); it++) {
    for (auto it2 = it->begin(); it2 != it->end(); it2++) {
      PageInsertIndexEntry(p, it2->first, it2->second);
    }
  }
}

void BufferTree::BufferTreeUpdatePivots(
    uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  SlicePageMap tmp;
  Page p = buffer_manager_->Lookup(pg_id);
  for (size_t i = 0; i < old_pivots->size(); i++) {
    auto it = new_pivots->at(i).begin();
    uint32_t old_child = old_pivots->at(i).second;
    if (old_child != it->second) {
      fprintf(stdout, "%s vs %s\n",
                      old_pivots->at(i).first.ToString().c_str(),
                      it->first.ToString().c_str());
      fprintf(stdout, "Child: %u vs %u\n", old_pivots->at(i).second, it->second);
      fflush(stdout);
    }
    assert(old_child == it->second);
    int off = PageGetOffsetByChild(p, old_child);
    PageUpdateKeyAt(p, off, it->first);
    it++;
    for (; it != new_pivots->at(i).end(); it++) {
      tmp[it->first] = it->second;
    }
  }
  for (auto it = tmp.begin(); it != tmp.end(); it++) {
    PageInsertIndexEntry(p, it->first, it->second);
  }
}

Page BufferTree::TreeSplitInternalPage(
    uint32_t pg_id,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots,
    std::vector<uint32_t>* new_pages,
    std::vector<Slice>* guards,
    std::vector<std::shared_ptr<FileMetaData>>* out_file_names) {
  ReadTbb a;
  Node* node = nullptr;
  if (node_table_.find(a, pg_id)) {
    node = a->second;
  }
  a.release();
  if (!node->node_above_leaf) {
    return BplusTree::TreeSplitInternalPage(pg_id, new_pivots,
                                  old_pivots, new_pages, guards, out_file_names);
  }
  assert(old_pivots->size() == 0);
  // Buffer is already empty and this node has been put in nodes_in_progress
  // so we can just split the page and return the new page
  // This extra page will stay in memory until it can be installed
  SlicePageMap temp_pivots;
  for (auto it = new_pivots->begin(); it != new_pivots->end(); it++) {
    for (auto it2 = it->begin(); it2 != it->end(); it2++) {
      temp_pivots[it2->first] = it2->second;
    }
  }
  Page extra_page = DistributePivots(pg_id, &temp_pivots, new_pages, guards);
  temp_pivots.clear();

  return extra_page;
}

void BufferTree::AdaptMem() {
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_compaction_num_.fetch_sub(1);
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  if (has_imm_.load()) {
    bg_working_.store(false, std::memory_order_release);
    pushdown_mem_scheduled.store(false, std::memory_order_release);
    write_finished_signal_.SignalAll();
    scheduled_compaction_num_.fetch_sub(1);
    return;
  }
  // mutex_.Lock();
  imm_ = mem_;
  has_imm_.store(true, std::memory_order_release);
  mem_ = new MemTable(internal_comparator_);
  mem_->Ref();
  mem_empty_.store(true, std::memory_order_release);
  imm_cold_only_.store(mem_cold_only_.load());
  mem_cold_only_.store(true, std::memory_order_release);
  if (!background_compaction_scheduled_) {
    CompactMemTable();
  }
  pushdown_mem_scheduled.store(false, std::memory_order_release);

  bg_working_.store(false, std::memory_order_release);
  // mutex_.Unlock();
  write_finished_signal_.SignalAll();
  scheduled_compaction_num_.fetch_sub(1);
}

void BufferTree::AdaptImm() {
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_compaction_num_.fetch_sub(1);
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  if (imm_ == nullptr) {
    pushdown_imm_scheduled.store(false, std::memory_order_release);
    bg_working_.store(false, std::memory_order_release);
    write_finished_signal_.SignalAll();
    scheduled_compaction_num_.fetch_sub(1);
    return;
  }
  // mutex_.Lock();
  if (!background_compaction_scheduled_) {
    CompactMemTable();
  }
  pushdown_imm_scheduled.store(false, std::memory_order_release);
  bg_working_.store(false, std::memory_order_release);
  // mutex_.Unlock();
  write_finished_signal_.SignalAll();
  scheduled_compaction_num_.fetch_sub(1);
}

void BufferTree::AdaptWOT() {
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_compaction_num_.fetch_sub(1);
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  {
    mutex_.Unlock();
    assert(adapt_strategy_ != nullptr);
    adapt_strategy_->AdaptWOT();
    mutex_.Lock();
  }
  bg_working_.store(false, std::memory_order_release);
  write_finished_signal_.SignalAll();
  scheduled_compaction_num_.fetch_sub(1);
}

#endif

} // namespace WOT_NAMESPACE