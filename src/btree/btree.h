#ifndef BTREE_WOT_NAMESPACE_H_
#define BTREE_WOT_NAMESPACE_H_

#include <atomic>
#include <map>
#include <stack>

#include "leveldb/include/iterator.h"
#include "leveldb/include/status.h"
#include "leveldb/port/port_stdcxx.h"
#include "leveldb/util/mutexlock.h"
#include "leveldb/dbformat.h"
#include "wot_buf_mgr/arena_wrapper.h"
#include "wot_buf_mgr/buffer_manager.h"
#include "wot_lock_mgr/lock_manager.h"

namespace WOT_NAMESPACE {

// This follows Blink-tree where we have k pivots with k pointers.
// The last pivot is the high key. The subtree pointed by the pointer
// is smaller than the pivot
class Btree {
 public:
  Btree(std::map<std::string, std::string>* props);

  ~Btree();

  // If key exists, the old value is replaced with the new
  Status Insert(std::string key, std::string val);
  Status Insert(const Slice& key, const Slice& val);

  // If key does not exist, insert the pair.
  // Otherwise replace the old value with the new one
  Status Update(std::string key, std::string val);

  // Since there will be not many deletes, we delete
  // and compact the page but NOT merge tree nodes.
  Status Delete(std::string key);

  Status Query(std::string key, std::string* val);

  void Print();

  void PrintStat();

  Iterator* NewBtreeIterator();

  uint32_t RootPageId() { return root_page_id_; }

  uint32_t TreeHeight() { return tree_height_; }

  bool SafeNode(Page page, const Slice& key, const Slice& value);

  // bool TryReadLock(uint32_t pg_id);
  // void ReadLock(uint32_t pg_id);
  // void ReadUnlock(uint32_t pg_id);
  // void WriteLock(uint32_t pg_id);
  // void WriteUnlock(uint32_t pg_id);

  char* NewPivot(Slice key);

  void SetPrintPage() { print_page_ = true; }
  void UnsetPrintPage() { print_page_ = false; }

 private:
  class TreeIterator;

  // Insertion goes down until the leaf node and stores all node along the path
  Status SearchDown(std::stack<uint32_t>* insert_stack,
                  const Slice& key, const Slice& value, bool ins=false);

  // The node stores (pivot, child/val) pair and the very first pivot should
  // be the minimum among all.
  void MayUpdateMinKey(Page cur_page, const Slice& key);

  void MayUpdateHighkey(Page cur_page, const Slice& key);

  Status TryInsertLeaf(uint32_t pg_id, const Slice& key,
                       const Slice& val);

  void UnlockParents(std::stack<uint32_t>* stack);

  void InsertAndSplitLeafPages(uint32_t pg_id, const Slice& key,
                              const Slice& val, std::vector<uint32_t>& new_pg_ids,
                              std::vector<Slice>& new_pivots,
                              InternalKeyComparator icmp);

  Status HandleOverflow(std::stack<uint32_t>*insert_stack,
                        const Slice& key, const Slice& val);

  Status TryInsertInternal(uint32_t pg_id, std::vector<Slice>& new_pivots,
                            std::vector<uint32_t>& new_pg_ids, uint32_t old);
                            
  void BuildSiblingConn(uint32_t left, uint32_t right);

  void InsertAndSplitInternalPages(uint32_t pg_id, uint32_t old,
                              std::vector<uint32_t>& new_pg_ids,
                              std::vector<Slice>& new_pivots);
                              
  void PrintNode(uint32_t page_id, int level);

  void PrintNodeStat(uint32_t page_id, int level);

  uint32_t root_page_id_;

  uint32_t tree_height_;

  // We can use bytewise comparator here directly however in order to
  // be compatible with tree of LSMTs in the future.
  const InternalKeyComparator internal_comparator_;

  BufferManager* buf_mgr_;
  TreeLockManager* lock_mgr_;

  port::Mutex mutex_;
  // std::map<uint32_t, NodeLock*> lock_table_ GUARDED_BY(mutex_);
  ArenaWrapper arena_wrapper_;

  bool print_page_ = false;

};

} // namespace WOT_NAMESPACE

#endif // BTREE_WOT_NAMESPACE_H_