#include "wot_buf_mgr/buffer_pool_helper.h"

#include "aha_tree.h"


namespace WOT_NAMESPACE {

// Constructor for pure LSMT and WOT
AhaTree::AhaTree(Options options, const std::string& dbname,
                     std::atomic<uint64_t>& flush_id,
                     bool is_lsmt, bool is_buffer_tree)
  : BplusTree(options, dbname, flush_id, is_lsmt, is_buffer_tree) {
    assert(leveldb_lsmt_ != nullptr);
    assert(root_->GetNodeLSMT() == nullptr);
  }

Status AhaTree::MakeRoomForWrite(bool force) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  bool allow_delay = !force;
  // Status s;
  while (true) {
    if (allow_delay && leveldb_lsmt_->NumLevelFiles(0) >=
                        config::kL0_SlowdownWritesTrigger) {
      mutex_.Unlock();
      env_->SleepForMicroseconds(1000);
      allow_delay = false;  // Do not delay a single write more than once
      mutex_.Lock();
    } else if (!force &&
        (mem_->ApproximateMemoryUsage() <= memtable_size_)) {
      // There is room in current memtable
      break;
    } else if (imm_ != nullptr) {
      // We have filled up the current memtable, but the previous
      // one is still being compacted, so we wait.
      // Log(options_.info_log, "Current memtable full; waiting...\n");
      // write_active_.store(false);
      background_work_finished_signal_.Wait();
      // write_active_.store(true);
    }  else if (background_compaction_scheduled_ &&
                leveldb_lsmt_->NumLevelFiles(0) >= config::kL0_StopWritesTrigger) {
      // There are too many level-0 files.
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

void AhaTree::MaybeScheduleCompaction() {
  mutex_.AssertHeld();
  if (background_compaction_scheduled_) {
    // Already scheduled
  } else if (shutting_down_.load(std::memory_order_acquire)) {
    // DB is being deleted; no more background compactions
  } else if (imm_ == nullptr && !leveldb_lsmt_->NeedsCompaction()) {
    // No work to be done
  } else {
    background_compaction_scheduled_ = true;
    scheduled_compaction_num_.fetch_add(1);
    env_->Schedule(&BplusTree::BGWork, this);
  }
}

void AhaTree::BackgroundCall() {
  lock_manager_->WriteLock(-1);
  MutexLock l(&mutex_);

  assert(background_compaction_scheduled_);
  if (shutting_down_.load(std::memory_order_acquire)) {
    // No more background work when shutting down.
  } else {
    BackgroundCompaction();
  }

  background_compaction_scheduled_ = false;

  // Previous compaction may have produced too many files in a level,
  // so reschedule another compaction if needed.
  MaybeScheduleCompaction();
  background_work_finished_signal_.SignalAll();
  scheduled_compaction_num_.fetch_sub(1);
}

void AhaTree::BackgroundCompaction() {
  mutex_.AssertHeld();

  if (imm_ != nullptr) {
    CompactMemTable();
    return;
  }
  assert(leveldb_lsmt_ != nullptr && leveldb_lsmt_->NeedsCompaction());
  lock_manager_->ReadLock(root_pg_id_);
  Page rt = buffer_manager_->Lookup(root_pg_id_);

  // lock_manager_->WriteLock(-1);
  lock_manager_->ReadUnlock(root_pg_id_);
  // mutex_ will be unlocked while compaction is in progress then locked again.
  OpState s = leveldb_lsmt_->DoCompact(rt, &mutex_, lock_manager_, -1);
  std::set<uint64_t> dead_files;
  leveldb_lsmt_->GetObsoleteFiles(dead_files);
  // leveldb_lsmt_->RemoveObsoleteFiles();
  if (leveldb_lsmt_->GetBottomLevel() == 0) {
    fprintf(stderr, "Error: Root lsmt should have more than 1 level\n");
    std::abort();
  }
  lock_manager_->WriteUnlock(-1);
  LevelDBLSMT::RemoveFiles(env_, root_lsmt_path_, dead_files);
  if (!background_flush_scheduled_.load() && s == OpState::kOverflow) {
    background_flush_scheduled_.store(true, std::memory_order_release);
    AddLSMTFlushWork();
  }
  // mutex_.Lock();
}

void AhaTree::CompactMemTable() {
  mutex_.AssertHeld();
  assert(imm_ != nullptr);
  // Status s = WriteLevel0Table(imm_, &edit, base);
  FileMetaData meta;
  meta.number = flush_id_.fetch_add(1) + 1;
  meta.file_entry = imm_->GetEntryNum();
  pending_outputs_.insert(meta.number);
  Iterator* iter = imm_->NewIterator();
  Options opt;
  opt.comparator = &internal_comparator_;
  Status s;
  {
    mutex_.Unlock();
    lock_manager_->WriteUnlock(-1);
    s = BuildTable(root_lsmt_path_, env_, opt, table_cache_, iter, &meta);
    lock_manager_->WriteLock(-1);
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

  // mutex_.Unlock();

  if (leveldb_lsmt_ != nullptr) {
    int level = 0;
    if (s.ok() && meta.file_size > 0) {
      const Slice min_user_key = meta.smallest.user_key();
      const Slice max_user_key = meta.largest.user_key();
      level = leveldb_lsmt_->PickLevelForMemTableOutput(min_user_key, max_user_key);
      leveldb_lsmt_->AddFile(level, meta, nullptr);
      if (has_hotspot && !leveldb_lsmt_->HasHotspot(low_hot_key_, up_hot_key_)) {
        leveldb_lsmt_->SetHotspot(low_hot_key_, up_hot_key_, true);
      }
    }
    lock_manager_->WriteUnlock(-1);
  }
}

// Root is write-locked
void AhaTree::UpdateRoot(SlicePageMap result) {
  // buffer_manager_->WriteLock(root_pg_id_);
  root_->InstallNewPage();
  uint32_t new_id = buffer_manager_->AllocateRoot(root_pg_id_);
  lock_manager_->WriteLock(new_id);
  delete root_;
  Node* new_node = new Node(this, true, node_lsmt_level_limit_,
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
  root_ = new Node(this, false, flush_id_, internal_comparator_);
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
  }
  nodes_in_progress.clear();
  // buffer_manager_->WriteUnlock(root_pg_id_);
}

void AhaTree::AdaptMem() {
#ifdef BREAKDOWN
  writer_stat_.mem_cnt.fetch_add(1);
  uint64_t start = _rdtsc(), end;
#endif
  lock_manager_->WriteLock(-1);
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_flush_num_.fetch_sub(1);
    lock_manager_->WriteUnlock(-1);
#ifdef BREAKDOWN
    end = _rdtsc();
    writer_stat_.mem_time.fetch_add(end - start);
#endif
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  if (has_imm_.load()) {
    bg_working_.store(false, std::memory_order_release);
    pushdown_mem_scheduled.store(false, std::memory_order_release);
    write_finished_signal_.SignalAll();
    scheduled_flush_num_.fetch_sub(1);
    lock_manager_->WriteUnlock(-1);
#ifdef BREAKDOWN
    end = _rdtsc();
    writer_stat_.mem_time.fetch_add(end - start);
#endif
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
  } else {
    lock_manager_->WriteUnlock(-1);
  }
  pushdown_mem_scheduled.store(false, std::memory_order_release);

  bg_working_.store(false, std::memory_order_release);
  // mutex_.Unlock();
  write_finished_signal_.SignalAll();
  scheduled_flush_num_.fetch_sub(1);
#ifdef BREAKDOWN
  end = _rdtsc();
  writer_stat_.mem_time.fetch_add(end - start);
#endif
}

// TODO: call MaybeScheduleCompaction()
void AhaTree::AdaptImm() {
#ifdef BREAKDOWN
  writer_stat_.imm_cnt.fetch_add(1);
  uint64_t start = _rdtsc(), end;
#endif
  lock_manager_->WriteLock(-1);
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_flush_num_.fetch_sub(1);
    lock_manager_->WriteUnlock(-1);
#ifdef BREAKDOWN
    end = _rdtsc();
    writer_stat_.imm_time.fetch_add(end - start);
#endif
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  if (imm_ == nullptr) {
    pushdown_imm_scheduled.store(false, std::memory_order_release);
    bg_working_.store(false, std::memory_order_release);
    write_finished_signal_.SignalAll();
    scheduled_flush_num_.fetch_sub(1);
    lock_manager_->WriteUnlock(-1);
#ifdef BREAKDOWN
    end = _rdtsc();
    writer_stat_.imm_time.fetch_add(end - start);
#endif    
    return;
  }
  // mutex_.Lock();
  if (!background_compaction_scheduled_) {
    CompactMemTable();
  } else {
    lock_manager_->WriteUnlock(-1);
  }
  pushdown_imm_scheduled.store(false, std::memory_order_release);
  bg_working_.store(false, std::memory_order_release);
  // mutex_.Unlock();
  write_finished_signal_.SignalAll();
  scheduled_flush_num_.fetch_sub(1);
#ifdef BREAKDOWN
  end = _rdtsc();
  writer_stat_.imm_time.fetch_add(end - start);
#endif
}

void AhaTree::AdaptWOT() {
#ifdef BREAKDOWN
  writer_stat_.tree_cnt.fetch_add(1);
  uint64_t start = _rdtsc(), end;
#endif 
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_flush_num_.fetch_sub(1);
#ifdef BREAKDOWN
    end = _rdtsc();
    writer_stat_.tree_time.fetch_add(end - start);
#endif 
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
  scheduled_flush_num_.fetch_sub(1);
#ifdef BREAKDOWN
  end = _rdtsc();
  writer_stat_.tree_time.fetch_add(end - start);
#endif 
}

} // namespace WOT_NAMESPACE