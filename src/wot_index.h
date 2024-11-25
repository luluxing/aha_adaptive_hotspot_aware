#ifndef WOT_NAMESPACE_BPLUS_TREE_H
#define WOT_NAMESPACE_BPLUS_TREE_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <inttypes.h>
#include <queue>
#include <stack>


#include "lsmt/lsmt.h"
#include "leveldb/include/env.h"
#include "leveldb/include/options.h"
#include "leveldb/include/status.h"
#include "leveldb/include/write_batch.h"
#include "leveldb/dbformat.h"
#include "leveldb/table/two_level_iterator.h"
#include "leveldb/table_cache.h"
#include "wot_buf_mgr/buffer_manager.h"
#include "wot_buf_mgr/wot_page_iter.h"
#include "wot_buf_mgr/arena_wrapper.h"
#include "wot_adapt_strategy/wot_state.h"
#include "wot_adapt_strategy/default_strategy.h"
#include "wot_lock_mgr/lock_manager.h"
#include "util/state.h"
#include "wot_page_split_policy.h"

namespace WOT_NAMESPACE {

class Node;
class BplusTree;
class SortedTreeIterator;
class PageIterator;
class PageSplitPolicy;
// class BplusTreeState;

typedef oneapi::tbb::concurrent_hash_map<uint32_t, Node*> TbbTable;
typedef TbbTable::const_accessor ReadTbb;
typedef TbbTable::accessor WriteTbb;

class Node {
 public:
  uint32_t pg_id_;

  // A node associated with LevelDB LSMT
  Node(BplusTree* tree, bool is_leaf, int level, Env* env, const std::string& dbname,
        std::atomic<uint64_t>& flush_id, const Options* opt, const InternalKeyComparator& icmp,
        TableCache* table_cache,  LSMTStatus stat, int leaf_limit, bool allow_single=false)
  : tree_(tree),
    icmp_(&icmp),
    flush_id_(flush_id),
    node_lsmt_(new LevelDBLSMT(env, dbname, flush_id, opt,
                               icmp, table_cache, level, stat,
                               leaf_limit)),
    lsmt4compact_(nullptr),
    extra_page_(nullptr) {}

  // Node has no associated LSMT
  Node(BplusTree* tree, bool is_leaf, std::atomic<uint64_t>& flush_id,
      const InternalKeyComparator& icmp)
  : tree_(tree),
    icmp_(&icmp),
    flush_id_(flush_id),
    node_lsmt_(nullptr),
    lsmt4compact_(nullptr),
    extra_page_(nullptr) {}

  ~Node() {

    if (node_lsmt_ != nullptr)
      delete node_lsmt_;
    if (lsmt4compact_ != nullptr)
      delete lsmt4compact_;
    if (extra_page_ != nullptr)
      free(extra_page_);
  };

  // No copying allowed
  Node(const Node&) = delete;
  void operator=(const Node&) = delete;

  // OpState Query(std::string key, std::string* value);
  void NodePrint(int level);
  void NodePrintStat(int level);

  // Below is used by LevelDBLSMT
  // OpState AddFile(FileMetaData f);
  // OpState AddFiles(std::vector<std::shared_ptr<FileMetaData>>* files);
  SlicePageMap MaybeSplitOrFlush();
  void UpdatePivots(std::vector<SlicePageMap>* new_pivots,
                    std::vector<std::pair<Slice, uint32_t>>* old_pivots);

  OpState BufferAddFiles(std::vector<std::shared_ptr<FileMetaData>>* files,
                             Page p);
  OpState BufferAppendFiles(std::vector<std::shared_ptr<FileMetaData>>* files);
  // OpState BufferAddFile(FileMetaData meta, Page p);
  void GetObsoleteFilesAndInstallNewBuffer(std::set<uint64_t>& files);
  void InstallNewBuffer();
  void InstallNewPage();

  void MayUpdateMinPivot(Page p, const Slice& k);
  Status BufferCompactTree(std::vector<Slice>*, std::vector<std::shared_ptr<FileMetaData>>*);
  Status BufferCompactTree(int*, std::vector<std::shared_ptr<FileMetaData>>*,
                           Iterator* iter=nullptr);
  void BufferCompactBottomLevel(Page page);
  void BufferCompactFilesInRange(int level,
                                   std::vector<std::shared_ptr<FileMetaData>>* files,
                                   const Page page);
  const std::vector<std::shared_ptr<FileMetaData>>* 
      BufferGetAndCompactBottomLevel(Page page);
  void BufferClear();
  void BufferClearAll();
  void SetBufferStatus(LSMTStatus status);
  void SetBufferLevelLimit(int level);
  void BufferClearBottomLevelFiles();
  void BufferFinalizeMergeLeaf(int level, std::vector<std::shared_ptr<FileMetaData>>*);

  LevelDBLSMT* GetNodeLSMT() { return node_lsmt_; }

  void FlushFilesToChildren(int, std::vector<std::shared_ptr<FileMetaData>>* files,
                            std::vector<SlicePageMap>*,
                            std::vector<std::pair<Slice, uint32_t>>*,
                            bool one_level_only=false);
  SlicePageMap SplitSmallLeaf();
  SlicePageMap SplitNodeAndLSMT(std::vector<SlicePageMap>*,
                            std::vector<std::pair<Slice,uint32_t>>*);
  SlicePageMap SplitLeafAndLSMT();

  bool node_overflow_ = false;
  bool node_above_leaf = false;

  // Apply new pivots and remove old ones during split.
  SlicePageMap SplitNodeWithBuffer(std::vector<SlicePageMap>*,
                            std::vector<std::pair<Slice,uint32_t>>*);

  OpState BufferCompactTopLevel(Page page);
  int GetNodeLSMTFileSizeInLevel(int level);
  bool BufferTopLevelOverflows();
  void CompactChildren(SlicePageMap* children_need_compaction,
                           SlicePageMap* overflow_children);
  void FinalizeSubtree(Page this_page, int level_of_files,
                      std::vector<Node*>* tmp_nodes,
            std::vector<std::shared_ptr<FileMetaData>>* flushed, int id);
  void AppendFilesToChild(uint32_t child_pg_id, std::vector<Node*>* tmp_nodes,
                std::vector<std::shared_ptr<FileMetaData>>* remains, 
                std::vector<std::shared_ptr<FileMetaData>>* flushed,
                const Slice& pivot, SlicePageMap* overflow_children,
                SlicePageMap* children_need_compaction);

  // LevelDBLSMT is a leveled version. Only compact the bottom level.
  void CompactAndFlushToChildren(std::vector<SlicePageMap>*,
                                std::vector<std::pair<Slice, uint32_t>>*);
  // void CompactAndFlushToSomeChildren(std::vector<SlicePageMap>*,
  //                               std::vector<std::pair<Slice, uint32_t>>*);
  // void CompactWithChildren(std::vector<SlicePageMap>*,
  //                           std::vector<std::pair<Slice, uint32_t>>*);
  // void CompactWithOneChild(std::vector<SlicePageMap>*,
  //                           std::vector<std::pair<Slice, uint32_t>>*);
  void DistributeFilesToNodes(std::vector<std::shared_ptr<FileMetaData>>* files,
                            std::vector<uint32_t>* new_pages,
                            std::vector<Slice>* guards,
                            std::vector<SlicePageMap>*,
                            std::vector<std::pair<Slice, uint32_t>>*);
  SlicePageMap FlushFilesToChildrenNoSplit(int, std::vector<std::shared_ptr<FileMetaData>>* files);
  SlicePageMap FlushAndMergeSmallLeaf(uint32_t child, int,
                            std::vector<std::shared_ptr<FileMetaData>>* files);

  BplusTree* tree_;
  const InternalKeyComparator* icmp_;
  std::atomic<uint64_t>& flush_id_;
  
  LevelDBLSMT* node_lsmt_;
  LevelDBLSMT* lsmt4compact_;

  Page extra_page_;

  std::string tmp_min_;

  friend class BplusTree;
  friend class UnsortedTreeIterator;
  friend class SortedTreeIterator;
};

// Leaf node is special that it does not have pivots and its data is sorted
// Leaf node buffer can be configured to a one-level lsmt of 2 tiers
class BplusTree {
 public:
  enum class BtState : uint8_t {
    kBtOverflow,
    kBtUnderflow,
    kBtNOrmal,
  };

  // General constructor for WOT and LSMT
  BplusTree(Options options, const std::string& dbname,
            std::atomic<uint64_t>& flush_id, bool is_lsmt=false,
            bool is_buffer_tree=false);

  // No copying allowed
  BplusTree(const BplusTree&) = delete;
  void operator=(const BplusTree&) = delete;
  
  ~BplusTree();

  uint32_t TreeHeight() { return tree_height_.load(); }

  uint64_t NextNodeId() { return max_node_id_.fetch_add(1); }

  void Print();
  void PrintStat();
  void SetPrintPage() { print_page_ = true; }
  void UnsetPrintPage() { print_page_ = false; }

  Status Insert(std::string key, std::string value);

  Status Insert(WriteBatch* updates);

  Status Update(std::string key, std::string value) {
    return Insert(key, value);
  }

  Status Delete(std::string key);

  Status Query(std::string key, std::string* value);

  size_t MemoryUsage();

  SortedTreeIterator* NewSortedTreeIterator();

  Iterator* NewPageIterator();

  virtual int EqualSplitInternalPage(
    Page p,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots);

  virtual int GetLockedRoot() = 0;
  virtual bool NodeTopLevelOverflows(Node* node) = 0;
  virtual bool AllChildrenLeaf(uint32_t pg_id) = 0;
  
  virtual void MergeWithAllChildrenLeaf(
    uint32_t pg_id,
    int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) = 0;
  
  virtual void UpdateOrRewritePivots(
    uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots);
  
  virtual Page TreeSplitInternalPage(
    uint32_t pg_id,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots,
    std::vector<uint32_t>* new_pages,
    std::vector<Slice>* guards,
    std::vector<std::shared_ptr<FileMetaData>>* out_file_names);
  
  Page DistributePivots(uint32_t pg_id, SlicePageMap*,
    std::vector<uint32_t>* new_pages, std::vector<Slice>* guards);

  virtual PageSplitPolicy* GetPageSplitPolicy(size_t, size_t) = 0;

  // static void BGTreeInsert(void* db);
  void TreeInsert(std::pair<std::string, std::string> item);
  bool SafeNode(Page page, const Slice& key, const Slice& value);
  Status SearchDown(std::stack<uint32_t>* insert_stack,
                  const Slice& key, const Slice& value);
  void MayUpdateMinKey(Page cur_page, const Slice& key);
  Status TryInsertLeaf(uint32_t pg_id, const Slice& key,
                       const Slice& val);
  void UnlockParents(std::stack<uint32_t>* stack);
  void InsertAndSplitLeafPages(uint32_t pg_id, const Slice& key,
                              const Slice& val, std::vector<uint32_t>* new_pg_ids,
                              std::vector<Slice>* new_pivots,
                              InternalKeyComparator icmp);
  Status HandleOverflow(std::stack<uint32_t>*insert_stack,
                        const Slice& key, const Slice& val);
  Status TryInsertInternal(uint32_t pg_id, std::vector<Slice>* new_pivots,
                            std::vector<uint32_t>* new_pg_ids, uint32_t old);
  void BuildSiblingConn(uint32_t left, uint32_t right);
  void InsertAndSplitInternalPages(uint32_t pg_id, uint32_t old,
                              std::vector<uint32_t>* new_pg_ids,
                              std::vector<Slice>* new_pivots);

  const Comparator* user_comparator() const {
    return internal_comparator_.user_comparator();
  }
  // Following functions are related to adaptation
  virtual void AdaptToRead();
  void SetHotspotRange(std::string low, std::string up);

  virtual void StopAdaptToRead();

  // MemTable* mem_;
  Options leveldb_options_;
  std::string root_lsmt_path_;
  uint32_t node_lsmt_level_limit_;
  uint32_t lsmt_level_limit_;
  int leaf_limit_;

  static void BGAdaptMem(void* db);
  static void BGAdaptImm(void* db);
  static void BGAdaptLSMT(void* db);
  static void BGFlushLSMT(void* db);
  static void BGAdaptWOT(void* db);
  void AddLSMTFlushWork();
  void FlushLSMT();
  void AddLSMTCompactionWork();
  void AdaptLSMT();
  virtual void AddMemCompactionWork();
  virtual void AdaptMem() = 0;
  virtual void AddImmCompactionWork();
  virtual void AdaptImm() = 0;
  void BulkloadLSMTFiles();
  
  void InstallLSMTfilesToBol();
  void FlushLSMTfilesToBol(int, std::vector<std::shared_ptr<FileMetaData>>* files,
                           bool one_level_only=false);
  int BulkloadFiles(const std::vector<std::shared_ptr<FileMetaData>>* bottom_files,
                    std::vector<uint32_t>* top_level, bool small_leaf);
  void AddNewNode(uint32_t pg_id, bool leaf_node,
                  std::shared_ptr<FileMetaData> meta, bool smallleaf=false,
                  size_t file_size=0);
  virtual void AddWOTCompactionWork();
  virtual void AdaptWOT() = 0;

  virtual void UpdateRoot(SlicePageMap result) = 0;
  void RecordReadSample(Slice key);

  Env* const env_;

 protected:
  friend class BplusTreeState;
  void ChangeState(BplusTreeState* state);

  uint64_t node_size_; // threshold for pivot map

  friend class Node;
  friend class UnsortedTreeIterator;
  friend class SortedTreeIterator;
  friend class PageIterator;

  // Information kept for every waiting writer
  struct Writer {
    explicit Writer(port::Mutex* mu)
        : batch(nullptr), sync(false), done(false), cv(mu) {}

    Status status;
    WriteBatch* batch;
    bool sync;
    bool done;
    port::CondVar cv;
  };

  // Protected by root lock
  std::atomic<uint32_t> tree_height_;

  // size in terms of bytes
  uint64_t memtable_size_;
  // Part of lsmt prefix
  std::atomic<uint64_t> max_node_id_;

  // All Leveldb LSMT share this number. Only the pointer to this number
  // is passed to each LSMT. When they want a  new file number, this
  // number is incremented.
  std::atomic<uint64_t>& flush_id_;


  int scale_in_;

  void DeleteNode(uint32_t page_id);
  
  Options SanitizeOptions(const Options& src, const InternalKeyComparator* icmp,
                          const InternalFilterPolicy* ipolicy);

  const InternalFilterPolicy internal_filter_policy_;

  // Table_cache is shared among all LSMTs. We need to specify which LSMT file
  // we are probing each time.
  TableCache* const table_cache_;
  // int table_cache_size_ = /*max_open_files*/50 - /*kNumNonTableCacheFiles*/10;

  // std::deque<uint32_t> adapt_work_queue_;

  oneapi::tbb::concurrent_bounded_queue<std::pair<std::string, std::string>> hot_item_queue_;

  virtual Status MakeRoomForWrite(bool force /* compact even if there is room? */)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;
  WriteBatch* BuildBatchGroup(Writer** last_writer)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  virtual void MaybeScheduleCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;
  static void BGWork(void* db);
  virtual void BackgroundCall() = 0;
  // void BackgroundCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  virtual void CompactMemTable() EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

  // State below is protected by mutex_
  port::Mutex mutex_;
  
  port::CondVar background_work_finished_signal_ GUARDED_BY(mutex_);
  MemTable* mem_;
  MemTable* imm_ GUARDED_BY(mutex_);  // Memtable being compacted
  
  // std::atomic<uint32_t> read_counter_;
  std::atomic<bool> write_active_;
  std::atomic<bool> pushdown_active_;
  std::atomic<bool> compact_active_;
  std::atomic<uint32_t> write_wait_;
  std::atomic<bool> bg_working_;
  port::CondVar write_finished_signal_ GUARDED_BY(mutex_);
  
  // void BeforePushdown() {
  //   // write_wait_.store(true);
  //   write_wait_.fetch_add(1);
  //   while (read_counter_.load() > 0 || write_active_) {
  //     write_finished_signal_.Wait();
  //   }
  //   // write_active_.store(true);
  //   pushdown_active_.store(true, std::memory_order_release);
  //   write_wait_.fetch_sub(1);
  //   // write_wait_.store(false);
  // }

  // Queue of writers.
  std::deque<Writer*> writers_ GUARDED_BY(mutex_);
  WriteBatch* tmp_batch_ GUARDED_BY(mutex_);

  // Set of table files to protect from deletion because they are
  // part of ongoing compactions.
  std::set<uint64_t> pending_outputs_ GUARDED_BY(mutex_);

  // Has a background compaction been scheduled or is running?
  bool background_compaction_scheduled_ GUARDED_BY(mutex_);

  // std::atomic<bool> tree_insertion_scheduled_;

  LevelDBLSMT* leveldb_lsmt_ GUARDED_BY(mutex_);

  DefaultAdapt* adapt_strategy_ = nullptr;

  ArenaWrapper arena_wrapper_;
  
  std::atomic<int> scheduled_flush_num_;
  std::atomic<int> scheduled_compaction_num_;
  std::chrono::time_point<std::chrono::system_clock> exam_flush_time_;
  std::chrono::time_point<std::chrono::system_clock> exam_compaction_time_;

  std::string low_hot_key_;
  std::string up_hot_key_;

 public:
  
  // WOT inserts through root_ but pure LSMT inserts directly to LSMT
  // This helps in later adaptation from LSMT to WOT
  Node* root_;
  // Root page id is reserved in the buffer manager
  const uint32_t root_pg_id_;
  std::atomic<uint32_t> seed_;
  // VanillaTree* wot_lsmt_ = nullptr;
  // LevelDBLSMT* leveldb_lsmt_;
  BufferManager* buffer_manager_;
  TreeLockManager* lock_manager_;
  const InternalKeyComparator internal_comparator_;

  // Since we are building a buffer tree, we need to store the entry point for the
  // buffer, which seems odd to be serialized. So we keep this entry point (pointer
  // to the buffer LSMT) as a pointer and we keep a tree-wise hash table to map from
  // a page_id to the node pointer, where it stores auxilliary data for the buffer.
  // std::map<uint32_t, Node*> node_table_;
  TbbTable node_table_;

  // The number of files flushed each time when node buffer is full
  // This is different from adaptation process
  int flush_file_num_;

  int adapt_strategy_choice_;

  std::atomic<SequenceNumber> seq_no_;

  std::atomic<bool> pushdown_lsmt_scheduled;
  std::atomic<bool> pushdown_mem_scheduled;
  std::atomic<bool> pushdown_imm_scheduled;
  // Thread-safe because only BG thread can access it
  std::vector<uint32_t> nodes_in_progress;
  
  std::atomic<bool> mem_empty_;
  std::atomic<bool> mem_cold_only_;
  std::atomic<bool> imm_cold_only_;

  virtual DefaultAdapt* GetAdaptStrategy();
  port::Mutex& GetMutex() { return mutex_; }
  MemTable* GetMem() { return mem_; }
  MemTable* GetImm() { return imm_; }
  std::atomic<bool> has_imm_;
  std::atomic<bool> shutting_down_;
  LevelDBLSMT* GetRootLSMT() { return leveldb_lsmt_; }
  const std::string& GetLowHotKey() { return low_hot_key_; }
  const std::string& GetHighHotKey() { return up_hot_key_; }

  ArenaWrapper& GetArenaWrapper() { return arena_wrapper_; }
  BufferManager* GetBufferManager() { return buffer_manager_; }

  BplusTreeState* btree_state_;
  std::atomic<bool> background_flush_scheduled_;
  int page_split_policy_choice_;
  PageSplitPolicy* page_split_policy_ = nullptr;

  bool BGFlushIdle();
  bool BGCompactionIdle();

  bool RangeWithinHotspot(const Slice& low, const Slice& up);

  bool print_page_ = false;

#ifdef BREAKDOWN
  struct ReaderStat {
    std::atomic<uint64_t> wait_for_lock_time;
    std::atomic<uint64_t> wait_lsmt_time;
    std::atomic<uint64_t> wait_root_time;
    std::atomic<uint64_t> traverse_time;
    std::atomic<uint64_t> reader_cnt;
    std::atomic<uint64_t> reader_time;

    void Reset() {
      wait_for_lock_time.store(0);
      wait_lsmt_time.store(0);
      wait_root_time.store(0);
      traverse_time.store(0);
      reader_cnt.store(0);
      reader_time.store(0);
    }

    void PrintStat() {
      fprintf(stdout, "Total_read: %f, wait_for_lock: %f: lsmt %f, root %f, traverse: %f, reader_cnt: %lu\n",
          1.0*reader_time.load() / reader_cnt.load(), 1.0*wait_for_lock_time.load() / reader_cnt.load(), 1.0*wait_lsmt_time / reader_cnt.load(), 1.0*wait_root_time / reader_cnt.load(), 1.0*traverse_time.load() / reader_cnt.load(), reader_cnt.load());
    }
  } reader_stat_;

  struct WriterStat {
    std::atomic<uint64_t> mem_time;
    std::atomic<uint64_t> imm_time;
    std::atomic<uint64_t> lsmt_ltime;// write-lock time
    std::atomic<uint64_t> tree_time;
    std::atomic<uint64_t> mem_cnt;
    std::atomic<uint64_t> imm_cnt;
    std::atomic<uint64_t> lsmt_cnt;
    std::atomic<uint64_t> tree_cnt;
    std::atomic<uint64_t> initial_check_time;
    std::atomic<uint64_t> adapt_leaf_time;
    std::atomic<uint64_t> search_path_time;
    std::atomic<uint64_t> compact_ltime;// write-lock time (including root and node lsm)
    std::atomic<uint64_t> flush_ltime;// write-lock time
    std::atomic<uint64_t> split_ltime;// write-lock time
    std::atomic<uint64_t> split_small_leaf_ltime;// write-lock time
    std::atomic<uint64_t> update_pivot_ltime;// write-lock time
    std::atomic<uint64_t> installed_buffer;
    std::atomic<uint64_t> real_work_time;

    void Reset() {
      mem_time.store(0);
      imm_time.store(0);
      lsmt_ltime.store(0);
      tree_time.store(0);
      mem_cnt.store(0);
      imm_cnt.store(0);
      lsmt_cnt.store(0);
      tree_cnt.store(0);
      initial_check_time.store(0);
      adapt_leaf_time.store(0);
      search_path_time.store(0);
      compact_ltime.store(0);
      flush_ltime.store(0);
      split_ltime.store(0);
      split_small_leaf_ltime.store(0);
      update_pivot_ltime.store(0);
      installed_buffer.store(0);
      real_work_time.store(0);
    }

    void PrintStat() {
      fprintf(stdout, "Mem#%ld: %f, Imm#%ld: %f, LSMT#%ld: %f\n",
          mem_cnt.load(), 1.0*mem_time.load() / mem_cnt.load(),
          imm_cnt.load(), 1.0*imm_time.load() / imm_cnt.load(),
          lsmt_cnt.load(), 1.0*lsmt_ltime.load() / lsmt_cnt.load());
      fprintf(stdout, "Tree#%ld: %f (real_work %f includes compact %f; flush %f; split-node %f; split-small-leaf %f; update-pivot: %f; installed-buffer: %f; search-path: %f + %f | %f)\n",
          tree_cnt.load(), 1.0*tree_time.load() / tree_cnt.load(),
          1.0*real_work_time.load() / 10000,
          1.0*compact_ltime.load() / 10000,
          1.0*flush_ltime.load() / 10000,
          1.0*split_ltime.load() / 10000,
          1.0*split_small_leaf_ltime.load() / 10000,
          1.0*update_pivot_ltime.load() / 10000,
          1.0*installed_buffer.load() / tree_cnt.load(),
          1.0*search_path_time.load() / tree_cnt.load(),
          1.0*initial_check_time.load() / tree_cnt.load(),
          1.0*adapt_leaf_time.load() / tree_cnt.load());
    }
  } writer_stat_;
  
#endif
};


class SortedTreeIterator {
 public:
  SortedTreeIterator(BplusTree* tree, uint32_t seed,
                      const Comparator* cmp,
                      const InternalKeyComparator& internal_comparator)
  : iter_state_(IterState::kInvalid),
    tree_(tree),
    user_comparator_(cmp),
    valid_(false),
    rnd_(seed),
    bytes_until_read_sampling_(RandomCompactionPeriod()),
    internal_comparator_(internal_comparator),
    inner_iter_(nullptr) {
#ifdef BREAKDOWN
    life_starts_ = _rdtsc();
#endif
    }

  SortedTreeIterator(const SortedTreeIterator&) = delete;
  SortedTreeIterator& operator=(const SortedTreeIterator&) = delete;

  ~SortedTreeIterator() {
    // if (iter_state_ != IterState::kMem) delete mem_iter_;
#ifdef BREAKDOWN
    uint64_t life_ends = _rdtsc();
    tree_->reader_stat_.reader_time.fetch_add(life_ends - life_starts_);
    tree_->reader_stat_.reader_cnt.fetch_add(1);
#endif
    for (auto const& pg_id : locked_nodes_) {
      tree_->lock_manager_->ReadUnlock(pg_id);
    }
    if (inner_iter_ != nullptr) {
      delete inner_iter_;
    }
  }

  void Seek(const Slice& target);
  void Next();
  bool Valid();
  std::string key();
  std::string value();

  void SeekBothEnds(const Slice& low, const Slice& up);

  const Comparator* const user_comparator_;

 private:
  IterState iter_state_;
  BplusTree* tree_;

  bool valid_;
  Random rnd_;
  size_t bytes_until_read_sampling_;

  Iterator* inner_iter_;

  std::string saved_key_;

  std::vector<int> locked_nodes_;

  bool all_page_ = false;

  const InternalKeyComparator internal_comparator_;

  uint64_t life_starts_;

  void InitInnerIter(const Slice& low, const Slice& up);

  void FindNextUserEntry(bool skipping, std::string* skip);

  inline bool ParseKey(ParsedInternalKey* ikey) {
    Slice k = inner_iter_->key();

    size_t bytes_read = k.size() + inner_iter_->value().size();
    while (bytes_until_read_sampling_ < bytes_read) {
      bytes_until_read_sampling_ += RandomCompactionPeriod();
      tree_->RecordReadSample(k);
    }
    assert(bytes_until_read_sampling_ >= bytes_read);
    bytes_until_read_sampling_ -= bytes_read;

    if (!ParseInternalKey(k, ikey)) {
      return false;
    } else {
      return true;
    }
  }

  inline void SaveKey(const Slice& k, std::string* dst) {
    dst->assign(k.data(), k.size());
  }

  size_t RandomCompactionPeriod() {
    return rnd_.Uniform(2 * config::kReadBytesPeriod);
  }

};

} // namespace WOT_NAMESPACE


#endif // WOT_NAMESPACE_BPLUS_TREE_H