#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdio.h>
#include <thread>
#include <queue>
#include "leveldb/util/coding.h"
#include "buffer_manager.h"
#include "util/state.h"
#include "leveldb/port/port_stdcxx.h"
#include "leveldb/util/mutexlock.h"
#include "free_space.h"

namespace WOT_NAMESPACE {


BufferManager::~BufferManager() {}

// This is based on LRUhandle
struct PageHandle {
  uint32_t page_id;
  int page_array_id;
  PageHandle* next;
  PageHandle* prev;
  uint32_t refs;     // References, including cache reference, if present.
};

// TreeBufferManager is responsible to fetch a disk page into memory via
// page_id. All fetched pages, if without further Ref()
class TreeBufferManager {
 public:
  TreeBufferManager() 
  : usage_(0) {
    // Make empty circular linked lists.
    lru_.next = &lru_;
    lru_.prev = &lru_;
    in_use_.next = &in_use_;
    in_use_.prev = &in_use_;
  }

  ~TreeBufferManager() {
    MutexLock l(&mutex_);
    // assert(table_.size() == pages_.size());
    for (auto it = table_.begin(); it != table_.end(); it++) {
      PageHandle* e = it->second;
      if (e->refs > 0) {
        Page p = pages_[e->page_array_id];
        if (((TreePageHeader) p)->is_leaf_)
          fprintf(stdout, "Leaf page and ");
        fprintf(stdout, "Page %d is still pinned %d\n", e->page_id, e->refs);
      }
      delete[] pages_[e->page_array_id];
      // free(e->page);
      // e->page.reset();
      delete e;
    }
    table_.clear();
    std::queue<uint32_t> emp, emp2;
    std::swap(recent_evicted_, emp2);
    delete file_allocator_;
  }

  void SetMaxNum(uint32_t num) {
    max_num_ = num;
    // fprintf(stdout, "Set sharded buffer pool capacity to %d\n", max_num_);
  }

  void SetFileName(const std::string& file_name) {
    file_allocator_ = new TreeFile(file_name);
    // fprintf(stdout, "Set sharded buffer pool file name to %s\n", file_name.c_str());
  }

  void SetShardNum(uint32_t num) {
    shard_num_ = num;
  }

  // Allocate a buffer slot for a newly created page from the free_list
  // or from the file and return the allocated pg_id. 
  // (TODO): If exceeds the file size limit, allocate another file.
  
  // Allocate does not imply we must use it. Page is in lru
  void Allocate(uint32_t pg_id) {
    MutexLock l(&mutex_);
    
    // timer.Resume();
    if (MayOverflow()) {
      std::cerr << "Cannot allocate: Pinned too many buffers\n";
      std::abort();
    }

    uint32_t page_loc = (pg_id - 1) / shard_num_;
    uint32_t cur_offset = file_allocator_->GetPageid();
    while (cur_offset <= page_loc) {
      cur_offset = file_allocator_->GetNewPageid() + 1;
    }

    // uint32_t new_offset = file_allocator_->GetNewPageid();
    // assert(new_offset == (pg_id - 1) / shard_num_);

    PageHandle* e = GetBuffer();
    PageInit(pages_[e->page_array_id], pg_id);
    InsertTable(e, pg_id);
  }


  
  void AllocateRoot(uint32_t new_id, Page old_root) {
    MutexLock l(&mutex_);

    if (MayOverflow()) {
      std::cerr << "Cannot allocate4root: Pinned too many buffers\n";
      std::abort();
    }

    uint32_t page_loc = (new_id - 1) / shard_num_;
    uint32_t cur_offset = file_allocator_->GetPageid();
    while (cur_offset <= page_loc) {
      cur_offset = file_allocator_->GetNewPageid() + 1;
    }

    // int new_offset = file_allocator_->GetNewPageid();
    // assert(new_offset == (new_id - 1) / shard_num_);

    // Get a new buffer and copy the old root to it
    PageHandle* e = GetBuffer();
    Page dst = pages_[e->page_array_id];
    memcpy(dst, old_root, PAGE_SIZE);
    e->page_id = new_id;
    e->refs = 0;
    LRU_Append(&lru_, e);

    // Page old_page = pages_[old_root->page_array_id];
    ((TreePageHeader) dst)->page_id_ = new_id;
    ((TreePageHeader) dst)->ClearLock();
    table_[new_id] = e;
  }

  // Lookup a page in buffer. If not found, bring it from disk.
  // Will be evicted if not pinned.
  // Lookup means page is already in buffer and pinned
  // and we want to access its page
  Page Lookup(const uint32_t pg_id) {
    MutexLock l(&mutex_);
    // PageHandle* e = RetrieveHandle(pg_id);
    if (ExistsInBuffer(pg_id)) {
      return pages_[table_[pg_id]->page_array_id];
    }
    return nullptr;
  }

  // Pin a page in buffer and increment the reference count.
  // Page is put in in_use and will be moved to lru_ if unpinned.
  // Reference count is 1 or 0.
  Page Pin(const uint32_t pg_id) {
    MutexLock l(&mutex_);
    PageHandle* e = RetrieveHandle(pg_id);
    Ref(e);
    return pages_[e->page_array_id];
  }

  void Unpin(const uint32_t pg_id) {
    MutexLock l(&mutex_);
    Release(pg_id);
  }

  void UnpinAndRelease(const uint32_t pg_id) {
    MutexLock l(&mutex_);
    if (ExistsInBuffer(pg_id)) {
      PageHandle* e = table_[pg_id];
      e->refs--;
      if (e->refs == 0) {
        Erase(pg_id);
      }
    }
  }

  // Page is no longer used. Free the allocated memory and put the
  // page_id into free list
  void Delete(const uint32_t pg_id) {
    MutexLock l(&mutex_);
    if (table_.count(pg_id) > 0 ) {
      PageHandle* e = table_[pg_id];
      LRU_Remove(e);
      // free(e->page);
      recent_evicted_.push(e->page_array_id);
      delete e;
      table_.erase(pg_id);
    }
  }

  void PrintStat() {
    MutexLock l(&mutex_);
    fprintf(stdout, "Buffer pool has %lu=?%lu pages\n", table_.size(), pages_.size());
    fprintf(stdout, "recent evicted size %lu\n", recent_evicted_.size());
  }

  // Debugging print
  void PrintExistingPageIds() {
    std::cout << "Table has ";
    for (auto it = table_.begin(); it != table_.end(); it++) {
      std::cout << it->first << ", ";
    }
    std::cout << "\nLRU has ";
    PageHandle* e = lru_.next;
    while (e != &lru_) {
      std::cout << "page #" << e->page_id << " refed " << e->refs << "; ";
      e = e->next;
    }
    std::cout << "\nIn-use has ";
    e = in_use_.next;
    while (e != &in_use_) {
      std::cout << e->page_id << ", ref " << e->refs << "; ";
      e = e->next;
    }
    std::cout << "\n";
  }

 private:
  void LRU_Remove(PageHandle* e) {
    e->next->prev = e->prev;
    e->prev->next = e->next;
  }

  void LRU_Append(PageHandle* list, PageHandle* e) {
    // Make "e" newest entry by inserting just before *list
    e->next = list;
    e->prev = list->prev;
    e->prev->next = e;
    e->next->prev = e;
  }

  // Helper function for Pin()
  void Ref(PageHandle* e) {
    assert(e->refs >= 0);
    if (e->refs == 0) {  // If on lru_ list, move to in_use_ list.
      LRU_Remove(e);
      LRU_Append(&in_use_, e);
    }
    e->refs++;
  }

  void Unref(PageHandle* e) {
    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) {
      // No longer in use; move to lru_ list.
      LRU_Remove(e);
      LRU_Append(&lru_, e);
    }
  }

  // Unpin a page in buffer. It may not be evicted immediately.
  // This does not flush to disk.
  void Release(const uint32_t pg_id) {
    if (ExistsInBuffer(pg_id)) {
      PageHandle* handle = table_[pg_id];
      Unref(handle);
    }
  }

  bool ExistsInBuffer(const uint32_t pg_id) {
    return table_.count(pg_id) > 0;
  }

  PageHandle* GetBuffer() {
    mutex_.AssertHeld();
    PageHandle* e = new PageHandle();
    int new_id;
    if (recent_evicted_.size() == 0) {
      char* buf = new char[PAGE_SIZE];
      pages_.push_back(buf);
      recent_evicted_.push(pages_.size() - 1);
      assert(pages_.size() <= max_num_);
    }
    new_id = recent_evicted_.front();
    recent_evicted_.pop();
    char* buf = pages_[new_id];// malloc(PAGE_SIZE);
    memset((void*) buf, 0, PAGE_SIZE);
    // e->page = (Page) buf;
    e->page_array_id = new_id;
    return e;
  }

  
  // Remove the page from buffer and write to disk if dirty
  // This is the same as evict
  // Evict
  void Erase(const uint32_t pg_id) {
    mutex_.AssertHeld();
    PageHandle* e = table_[pg_id];
    uint32_t offset = (pg_id - 1) / shard_num_;
    file_allocator_->WritePage(offset, pages_[e->page_array_id]);
    LRU_Remove(e);
    // free(e->page);
    recent_evicted_.push(e->page_array_id);
    delete e;
    table_.erase(pg_id);
  }

  bool MayOverflow() {
    mutex_.AssertHeld();
    // TODO: need to guarantee the correctness of usage
    usage_ = table_.size();
    while (usage_ >= max_num_ && lru_.next != &lru_) {
      PageHandle* old = lru_.next;
      assert(old->refs == 0);
      if (table_.count(old->page_id) > 0) {
        Erase(old->page_id);
        usage_--;
      } else {
        LRU_Remove(old);
      }
    }
    if (usage_ >= max_num_) {
      return true;
    }
    return false;
  }

  void InsertTable(PageHandle* e, const uint32_t pg_id) {
    mutex_.AssertHeld();
    if (table_.count(pg_id) > 0) {
      // free(table_[pg_id]->page);
      delete[] pages_[table_[pg_id]->page_array_id];
    }
    // PageHandle* e = new PageHandle();
    e->page_id = pg_id;
    // e->page = buf;
    e->refs = 0;
    table_[pg_id] = e;
    LRU_Append(&lru_, e);
    // usage_++;
    // return e;
  }

  PageHandle* RetrieveHandle(const uint32_t pg_id) {
    mutex_.AssertHeld();
    if (ExistsInBuffer(pg_id)) {
      return table_[pg_id];
    }
    if (MayOverflow()) {
      fprintf(stderr, "Pinned too many buffers\n");
      return nullptr;
    }
    
    PageHandle* e = GetBuffer();
    // file_allocator_->GetPage(pg_id, (void*) e->page);
    uint32_t offset = (pg_id - 1) / shard_num_;
    file_allocator_->GetPage(offset, (void*) pages_[e->page_array_id]);
    InsertTable(e, pg_id);
    return e;
  }


  // Initialized before use.
  // Each page is of 8kB, so we count the number of pages not bytes
  uint32_t max_num_;

  port::Mutex mutex_;
  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==0
  PageHandle lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients, and have refs >= 1
  PageHandle in_use_ GUARDED_BY(mutex_);

  // Dummy head of the free page list.
  // If page is deallocated, it is put here and later node can reuse the 
  // page. If this list is empty, we allocate new page from file.
  PageHandle free_list_ GUARDED_BY(mutex_);

  // std::queue<uint32_t> reusable_offsets_ GUARDED_BY(mutex_);
  
  // Store all caches pages (both in_use and in lru)
  // Mapping: page_id -> PageHandle
  std::map<uint32_t, PageHandle*> table_ GUARDED_BY(mutex_);

  // Mapping: page_id -> offset in the file
  // std::map<uint32_t, uint32_t> offset_map_ GUARDED_BY(mutex_);

  std::vector<Page> pages_ GUARDED_BY(mutex_);

  // The index in the page array that is available for use
  std::queue<uint32_t> recent_evicted_ GUARDED_BY(mutex_);

  TreeFile* file_allocator_ GUARDED_BY(mutex_);

  uint32_t shard_num_;

  size_t usage_ GUARDED_BY(mutex_);
};


// Copied from cache.cc
static const int kNumShardBits = 4;
static const int kNumShards = 1 << kNumShardBits;

class ShardedBufferManager : public BufferManager {
 private:
  TreeBufferManager shard_[kNumShards];
  port::Mutex id_mutex_;
  uint64_t new_id_ ;
  uint64_t root_id_;
  Page root_;

  static inline uint32_t HashPageId(const uint64_t pid) {
    // TODO: transform pid to Slice then call hash()?
    // return Hash(pid);
    return pid;
  }

  static uint32_t Shard(uint32_t hash) {
    // return hash >> (32 - kNumShardBits);
    return hash % kNumShards;
  }

 public:
  explicit ShardedBufferManager(size_t capacity, const std::string& file_name)
  : new_id_(1),
    root_id_(0) {
    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].SetMaxNum(per_shard);
      shard_[s].SetFileName(file_name + std::to_string(s));
      shard_[s].SetShardNum(kNumShards);
    }
    // Root is always kept in memory and is not assigned to any shard
    root_ = new char[PAGE_SIZE];
    PageInit(root_, root_id_);
  }

  ~ShardedBufferManager() override {
    delete root_;
  }

  void PrintStat() override {
    for (int s = 0; s < kNumShards; s++) {
      fprintf(stdout, "Shard %d: ", s);
      shard_[s].PrintStat();
    }
  }

  Page Lookup(uint32_t page_id) override {
    if (page_id == root_id_) return root_;

    const uint32_t hash = HashPageId(page_id);
    return shard_[Shard(hash)].Lookup(page_id);
  }

  Page Pin(uint32_t page_id) override {
    if (page_id == root_id_) return root_;

    const uint32_t hash = HashPageId(page_id);
    return shard_[Shard(hash)].Pin(page_id);
  }

  void Delete(uint32_t page_id) override {
    if (page_id == root_id_) {
      fprintf(stderr, "Cannot delete root page\n");
      std::abort();
    }

    const uint32_t hash = HashPageId(page_id);
    shard_[Shard(hash)].Delete(page_id);
  }

  void Unpin(uint32_t page_id) override {
    if (page_id == root_id_) return;
    const uint32_t hash = HashPageId(page_id);
    return shard_[Shard(hash)].Unpin(page_id);
  }

  void UnpinAndRelease(uint32_t page_id) override {
    if (page_id == root_id_) return;
    const uint32_t hash = HashPageId(page_id);
    return shard_[Shard(hash)].UnpinAndRelease(page_id);
  }

  uint32_t Allocate() override {
    id_mutex_.Lock();
    auto pid = new_id_++;
    id_mutex_.Unlock();

    const uint32_t hash = HashPageId(pid);
    shard_[Shard(hash)].Allocate(pid);
    return pid;
  }

  // Root node is write-locked
  uint32_t AllocateRoot(uint32_t root_id) override {
    id_mutex_.Lock();
    assert(root_id == root_id_);
    auto pid = new_id_++;
    id_mutex_.Unlock();

    const uint32_t hash = HashPageId(pid);
    // Write the original root contents into a new page
    // with page_id as pid in this shard.
    shard_[Shard(hash)].AllocateRoot(pid, root_);

    PageInit(root_, root_id_);
    return pid;
  }
  
};

BufferManager* NewBufferManager(size_t capacity, const std::string& file_name) { 
  return new ShardedBufferManager(capacity, file_name); 
}


} // namespace WOT_NAMESPACE