#include "lock_manager.h"

namespace WOT_NAMESPACE {

void TreeLockManager::InsertLockTable(int pg_id) {
  mutex_.AssertHeld();
  NodeLock* node_lock = new NodeLock(pg_id);
  assert(lock_table_.find(pg_id) == lock_table_.end());
  lock_table_[pg_id] = node_lock;
  // WriteLockTbb lock;
  // bool inserted = lock_table_.insert(lock, pg_id);
  // if (inserted) {
  //   lock->second = node_lock;
  // } else {
  //   delete node_lock;
  // }
}

bool TreeLockManager::TryReadLock(int pg_id) {
  mutex_.Lock();
  if (lock_table_.find(pg_id) == lock_table_.end()) {
    InsertLockTable(pg_id);
  }
  auto node_lock = lock_table_[pg_id];
  node_lock->read_wait_.fetch_add(1);
  assert(node_lock->node_id_ == pg_id);
  mutex_.Unlock();
  return node_lock->TryReadLock();
}

void TreeLockManager::ReadLock(int pg_id) {
  mutex_.Lock();
  if (lock_table_.find(pg_id) == lock_table_.end()) {
    InsertLockTable(pg_id);
  }
  auto node_lock = lock_table_[pg_id];
  node_lock->read_wait_.fetch_add(1);
  mutex_.Unlock();
  node_lock->ReadLock();
  assert(node_lock->node_id_ == pg_id);
}


void TreeLockManager::AlleviateLock(int pg_id) {
  mutex_.Lock();
  assert(lock_table_.find(pg_id) != lock_table_.end());
  auto node_lock = lock_table_[pg_id];
  mutex_.Unlock();
  node_lock->AlleviateLock();
  assert(node_lock->node_id_ == pg_id);
}

void TreeLockManager::EscalateLock(int pg_id) {
  mutex_.Lock();
  assert(lock_table_.find(pg_id) != lock_table_.end());
  auto node_lock = lock_table_[pg_id];
  node_lock->write_wait_.fetch_add(1);
  mutex_.Unlock();
  node_lock->EscalateLock();
  assert(node_lock->node_id_ == pg_id);
}

bool TreeLockManager::ReadUnlock(int pg_id) {
  mutex_.Lock();
  if (lock_table_.find(pg_id) == lock_table_.end()) {
    mutex_.Unlock();
    return false;
  }
  auto node_lock = lock_table_[pg_id];
  mutex_.Unlock();
  node_lock->ReadUnlock();
  
  {
    mutex_.Lock();
    if (lock_table_.find(pg_id) != lock_table_.end()&&
          lock_table_[pg_id] == node_lock && 
            node_lock->node_id_ == pg_id &&
            !lock_table_[pg_id]->InUse()) {
      delete node_lock;
      // lock_table_[pg_id] = nullptr;
      lock_table_.erase(pg_id);
    }
    mutex_.Unlock();
  }
  return true;
}

void TreeLockManager::WriteLock(int pg_id) {
  mutex_.Lock();
  if (lock_table_.find(pg_id) == lock_table_.end()) {
    InsertLockTable(pg_id);
  }
  auto node_lock = lock_table_[pg_id];
  node_lock->write_wait_.fetch_add(1);
  mutex_.Unlock();
  node_lock->WriteLock();
  assert(node_lock->node_id_ == pg_id);
}

void TreeLockManager::WriteUnlock(int pg_id) {
  mutex_.Lock();
  assert (lock_table_.find(pg_id) != lock_table_.end());
  auto node_lock = lock_table_[pg_id];
  mutex_.Unlock();
  node_lock->WriteUnlock();
  {
    mutex_.Lock();
    if (lock_table_.find(pg_id) != lock_table_.end()&&
          lock_table_[pg_id] == node_lock && 
            node_lock->node_id_ == pg_id &&
            !lock_table_[pg_id]->InUse()) {
      delete node_lock;
      // lock_table_[pg_id] = nullptr;
      lock_table_.erase(pg_id);
    }
    mutex_.Unlock();
  }
}

void TreeLockManager::PrintStat() {
  fprintf(stdout, "Lock table size: %lu\n", lock_table_.size());
}

} // namespace WOT_NAMESPACE