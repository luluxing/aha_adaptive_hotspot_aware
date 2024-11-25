#ifndef WOT_BUF_MGR_ARENA_WRAPPER_H_
#define WOT_BUF_MGR_ARENA_WRAPPER_H_

#include "leveldb/util/arena.h"
#include "leveldb/port/port_stdcxx.h"
#include "leveldb/util/mutexlock.h"
#include "tree_page.h"

namespace WOT_NAMESPACE {

// Responsible for allocating a temporary storage for pivots.
// When the memoryUsage of arena reaches a predefined threshold, we create
// a new arena and deallocate the old one if the second one reaches the
// threshold.
class ArenaWrapper {
 public:
  ArenaWrapper()
  : arena_(new Arena()),
    imm_arena_(nullptr) {}

  ArenaWrapper(const ArenaWrapper&) = delete;
  ArenaWrapper& operator=(const ArenaWrapper&) = delete;

  ~ArenaWrapper() {
    MutexLock l(&mutex_);
    if (arena_ != nullptr) delete arena_;
    if (imm_arena_ != nullptr) delete imm_arena_;
  }

  void SetArenaThreshold(size_t th) { threshold_ = th; }

  size_t MemoryUsage() {
    MutexLock l(&mutex_);
    size_t imm_size = 0;
    if (imm_arena_ != nullptr) imm_size = imm_arena_->MemoryUsage();
    return arena_->MemoryUsage() + imm_size;
  }

  // TODO: need a more robust way to control releasing of temporary
  // memory.
  char* Allocate(size_t bytes) {
    MutexLock l(&mutex_);
    if (arena_->MemoryUsage() >= threshold_) {
      if (imm_arena_ != nullptr) {
        delete imm_arena_;
        // std::cout << "Warning: temporary memory has been erased\n";
      }
      imm_arena_ = arena_;
      arena_ = new Arena();
    }
    return arena_->Allocate(bytes);
  }

 private:
  Arena* arena_;
  Arena* imm_arena_;
  port::Mutex mutex_;
  // TODO: hard-coded the threshold which can be set
  size_t threshold_ = 2000 * 8192;
};

} // namespace WOT_NAMESPACE

#endif // WOT_BUF_MGR_ARENA_WRAPPER_H_