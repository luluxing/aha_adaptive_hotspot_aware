#ifndef WOT_LOCK_MGR_LOCK_MANAGER_H_
#define WOT_LOCK_MGR_LOCK_MANAGER_H_

#include <unordered_map>
#include <oneapi/tbb/concurrent_hash_map.h>
#include "leveldb/port/port_stdcxx.h"
#include "leveldb/util/mutexlock.h"

namespace WOT_NAMESPACE {

class NodeLock;

typedef oneapi::tbb::concurrent_hash_map<uint32_t, NodeLock*> LockTable;
typedef LockTable::const_accessor ReadLockTbb;
typedef LockTable::accessor WriteLockTbb;

class NodeLock{
 public:
  NodeLock(int id)
  : node_id_(id),
    cv_(&mutex_),
    read_count_(0),
    read_wait_(0),
    write_wait_(0),
    write_active_(false) {}
  
  ~NodeLock() {}
  
  bool TryReadLock() {
    if (mutex_.Trylock()) {
      bool has_write = write_active_.load() || write_wait_.load() > 0;
      if (!has_write) {
        read_count_.fetch_add(1);
      }
      mutex_.Unlock();
      read_wait_.fetch_sub(1);
      return !has_write;
    } else {
      read_wait_.fetch_sub(1);
      return false;
    }
  }

  void ReadLock() {
    MutexLock l(&mutex_);
    while (write_active_.load() || write_wait_.load() > 0) {
      cv_.Wait();
    }
    assert(read_wait_.load() > 0);
    read_wait_.fetch_sub(1);
    read_count_.fetch_add(1);
  }

  void EscalateLock() {
    MutexLock l(&mutex_);
    // write_wait_++;
    while (read_count_.load() > 1 || write_active_.load()) {
      cv_.Wait();
    }
    assert(write_wait_.load() > 0);
    assert(read_count_.load() == 1);
    write_wait_.fetch_sub(1);
    write_active_.store(true, std::memory_order_release);
  }

  void ReadUnlock() {
    MutexLock l(&mutex_);
    assert(read_count_.load() > 0);
    read_count_.fetch_sub(1);
    cv_.SignalAll();
  }

  void WriteLock() {
    MutexLock l(&mutex_);
    // write_wait_++;
    while (read_count_.load() > 0 || write_active_.load()) {
      cv_.Wait();
    }
    assert(write_wait_.load() > 0);
    write_wait_.fetch_sub(1);
    write_active_.store(true, std::memory_order_release);
  }

  void AlleviateLock() {
    MutexLock l(&mutex_);
    assert(write_active_.load());
    write_active_.store(false, std::memory_order_release);
    if (read_count_.load() != 1) {
      fprintf(stdout, "Read count: %d\n", read_count_.load());
      fflush(stdout);
    }
    assert(read_count_.load() == 1);
    cv_.SignalAll();
  }

  void WriteUnlock() {
    MutexLock l(&mutex_);
    assert(write_active_.load());
    write_active_.store(false, std::memory_order_release);
    cv_.SignalAll();
  }

  bool InUse() {
    MutexLock l(&mutex_);
    return write_active_ || write_wait_ > 0 || read_count_ > 0 || read_wait_ > 0;
  }

  int node_id_;
  port::Mutex mutex_;
  port::CondVar cv_;
  std::atomic<uint32_t> read_count_;
  std::atomic<uint32_t> read_wait_;
  std::atomic<uint32_t> write_wait_;
  std::atomic<bool> write_active_;
};

class TreeLockManager {
 public: 
  TreeLockManager(uint64_t max_num)
  : max_num_(max_num) {}

  ~TreeLockManager() {
    for (auto it = lock_table_.begin(); it != lock_table_.end(); ++it) {
      delete it->second;
    }
  }

  void SetCapacity(uint64_t max_num) {
    max_num_ = max_num;
  }

  void InsertLockTable(int pg_id);

  bool TryReadLock(int pg_id);
  void ReadLock(int pg_id);
  bool ReadUnlock(int pg_id);
  void WriteLock(int pg_id);
  void WriteUnlock(int pg_id);
  void EscalateLock(int pg_id);
  void AlleviateLock(int pg_id);

  void PrintStat();

 private:
  // LockTable lock_table_;
  port::Mutex mutex_;
  std::unordered_map<int, NodeLock*> lock_table_;

  uint64_t max_num_; // same as buffer pool size
};
  
} // namespace WOT_NAMESPACE

#endif // WOT_LOCK_MGR_LOCK_MANAGER_H_