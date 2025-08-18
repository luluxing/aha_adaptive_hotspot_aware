#ifndef WOT_BTREE_REFACTOR_H
#define WOT_BTREE_REFACTOR_H

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "wot_types_refactor_.h"
#include "wot_tree_node_refactor_.h"
#include "wot_tree_iterator_refactor_.h"
#include "wot_buffer_operations_refactor_.h"
#include "wot_page_split_refactor_.h"
#include "wot_adaptation_refactor_.h"
#include "wot_utilities_refactor_.h"

#include "leveldb/include/env.h"
#include "leveldb/include/options.h"
#include "leveldb/include/status.h"
#include "leveldb/include/write_batch.h"
#include "leveldb/memtable.h"
#include "leveldb/port/port.h"
#include "leveldb/table_cache.h"
#include "lsmt/lsmt.h"
#include "wot_buf_mgr/buffer_manager.h"
#include "wot_lock_mgr/lock_manager.h"
#include "wot_adapt_strategy/default_strategy.h"

namespace WOT_NAMESPACE {

using leveldb::Env;
using leveldb::Options;
using leveldb::Status;
using leveldb::WriteBatch;
using leveldb::MemTable;
using leveldb::TableCache;
using leveldb::Iterator;
using leveldb::port::Mutex;
using leveldb::port::CondVar;

class BplusTree {
public:
    // Constructor
    BplusTree(Options options, const std::string& dbname,
              std::atomic<uint64_t>& flush_id, bool is_lsmt = false,
              bool is_buffer_tree = false);

    // No copying allowed
    BplusTree(const BplusTree&) = delete;
    void operator=(const BplusTree&) = delete;
    
    ~BplusTree();

    // Core operations
    Status Insert(const std::string& key, const std::string& value);
    Status Insert(WriteBatch* updates);
    Status Update(const std::string& key, const std::string& value) {
        return Insert(key, value);
    }
    Status Delete(const std::string& key);
    Status Query(const std::string& key, std::string* value);

    // Iterator creation
    SortedTreeIterator* NewSortedTreeIterator();
    Iterator* NewPageIterator();

    // Tree information
    uint32_t TreeHeight() { return tree_height_.load(); }
    void IncrementTreeHeight(int delta) { tree_height_.fetch_add(delta); }
    size_t MemoryUsage();

    // Display and debugging
    void Print();
    void PrintStat();
    void SetPrintPage() { print_page_ = true; }
    void UnsetPrintPage() { print_page_ = false; }

    // Adaptation operations
    virtual void AdaptToRead();
    virtual void StopAdaptToRead();
    void SetHotspotRange(const std::string& low, const std::string& up);
    void RecordReadSample(const Slice& key);

    // Page split operations
    virtual int EqualSplitInternalPage(Page p,
                                      std::vector<SlicePageMap>* new_pivots,
                                      std::vector<std::pair<Slice,uint32_t>>* old_pivots);
    
    virtual int GetLockedRoot() = 0;
    virtual bool NodeTopLevelOverflows(Node* node) = 0;
    virtual bool AllChildrenLeaf(uint32_t pg_id) = 0;
    
    virtual void MergeWithAllChildrenLeaf(uint32_t pg_id, int level_of_files,
                                         std::vector<std::shared_ptr<FileMetaData>>* files,
                                         std::vector<SlicePageMap>* new_pivots,
                                         std::vector<std::pair<Slice, uint32_t>>* old_pivots) = 0;
    
    virtual void UpdateOrRewritePivots(uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
                                      std::vector<std::pair<Slice, uint32_t>>* old_pivots);
    
    virtual Page TreeSplitInternalPage(uint32_t pg_id,
                                      std::vector<SlicePageMap>* new_pivots,
                                      std::vector<std::pair<Slice,uint32_t>>* old_pivots,
                                      std::vector<uint32_t>* new_pages,
                                      std::vector<Slice>* guards,
                                      std::vector<std::shared_ptr<FileMetaData>>* out_file_names);
    
    Page DistributePivots(uint32_t pg_id, SlicePageMap* temp_pivots,
                         std::vector<uint32_t>* new_pages, std::vector<Slice>* guards);

    virtual PageSplitPolicy* GetPageSplitPolicy(size_t current_size, size_t target_size) = 0;

    // Tree insertion operations
    void TreeInsert(std::pair<std::string, std::string> item);
    bool SafeNode(Page page, const Slice& key, const Slice& value);
    Status SearchDown(std::stack<uint32_t>* insert_stack, const Slice& key, const Slice& value);
    void MayUpdateMinKey(Page cur_page, const Slice& key);
    Status TryInsertLeaf(uint32_t pg_id, const Slice& key, const Slice& val);
    void UnlockParents(std::stack<uint32_t>* stack);
    void InsertAndSplitLeafPages(uint32_t pg_id, const Slice& key, const Slice& val,
                                std::vector<uint32_t>* new_pg_ids, std::vector<Slice>* new_pivots,
                                InternalKeyComparator icmp);
    Status HandleOverflow(std::stack<uint32_t>* insert_stack, const Slice& key, const Slice& val);
    Status TryInsertInternal(uint32_t pg_id, std::vector<Slice>* new_pivots,
                            std::vector<uint32_t>* new_pg_ids, uint32_t old);
    void BuildSiblingConn(uint32_t left, uint32_t right);
    void InsertAndSplitInternalPages(uint32_t pg_id, uint32_t old,
                                    std::vector<uint32_t>* new_pg_ids,
                                    std::vector<Slice>* new_pivots);

    // Adaptation strategy management
    virtual void AddMemCompactionWork();
    virtual void AdaptMem() = 0;
    virtual void AddImmCompactionWork();
    virtual void AdaptImm() = 0;
    void AddLSMTFlushWork();
    void FlushLSMT();
    void AddLSMTCompactionWork();
    void AdaptLSMT();
    virtual void AddWOTCompactionWork();
    virtual void AdaptWOT() = 0;

    virtual void UpdateRoot(SlicePageMap result) = 0;

    // Background work management
    static void BGWork(void* db);
    virtual void BackgroundCall() = 0;
    virtual void CompactMemTable() EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

    // Access methods for components
    Env* GetEnv() const { return env_; }
    BufferManager* GetBufferManager() const { return buffer_manager_; }
    TreeLockManager* GetLockManager() const { return lock_manager_; }
    const InternalKeyComparator& GetInternalComparator() const { return internal_comparator_; }
    const Comparator* GetUserComparator() const { return internal_comparator_.user_comparator(); }
    TableCache* GetTableCache() const { return table_cache_; }
    ArenaWrapper& GetArenaWrapper() { return arena_wrapper_; }
    
    // Node management
    TbbTable& GetNodeTable() { return node_table_; }
    const TbbTable& GetNodeTable() const { return node_table_; }
    Node* GetRoot() const { return root_; }
    uint32_t GetRootPageId() const { return root_pg_id_; }
    
    // LSMT access
    LevelDBLSMT* GetLSMT() const { return leveldb_lsmt_; }
    const std::string& GetRootLSMTPath() const { return root_lsmt_path_; }
    std::atomic<uint64_t>& GetFlushId() { return flush_id_; }
    
    // Memory tables
    MemTable* GetMem() const { return mem_; }
    MemTable* GetImm() const { return imm_; }
    void SetMem(MemTable* mem) { mem_ = mem; }
    void SetImm(MemTable* imm) { imm_ = imm; }
    
    // Configuration
    const TreeConfig& GetConfig() const { return config_; }
    const Options& GetLevelDBOptions() const { return leveldb_options_; }
    
    // State management
    BplusTreeState* GetBTreeState() const { return btree_state_; }
    void ChangeState(BplusTreeState* state) { btree_state_ = state; }
    PageSplitPolicy* GetPageSplitPolicy() const { return page_split_policy_; }
    void SetPageSplitPolicy(PageSplitPolicy* policy) { page_split_policy_ = policy; }
    
    // Statistics
    ReaderStats& GetReaderStats() { return reader_stats_; }
    WriterStats& GetWriterStats() { return writer_stats_; }
    const ReaderStats& GetReaderStats() const { return reader_stats_; }
    const WriterStats& GetWriterStats() const { writer_stats_; }
    
    // Sequence number management
    uint64_t GetSequenceNumber() const { return seq_no_.load(); }
    void IncrementSequenceNumber(uint64_t delta) { seq_no_.fetch_add(delta); }
    
    // State flags
    bool IsMemEmpty() const { return mem_empty_.load(); }
    void SetMemEmpty(bool empty) { mem_empty_.store(empty); }
    bool IsMemColdOnly() const { return mem_cold_only_.load(); }
    void SetMemColdOnly(bool cold_only) { mem_cold_only_.store(cold_only); }
    bool IsImmColdOnly() const { return imm_cold_only_.load(); }
    void SetImmColdOnly(bool cold_only) { imm_cold_only_.store(cold_only); }
    bool HasImm() const { return has_imm_.load(); }
    void SetHasImm(bool has_imm) { has_imm_.store(has_imm); }
    bool IsShuttingDown() const { return shutting_down_.load(); }
    void SetShuttingDown(bool shutting_down) { shutting_down_.store(shutting_down); }
    
    // Background work state
    bool IsBackgroundCompactionScheduled() const { return background_compaction_scheduled_; }
    void SetBackgroundCompactionScheduled(bool scheduled) { background_compaction_scheduled_ = scheduled; }
    bool IsBackgroundFlushScheduled() const { return background_flush_scheduled_.load(); }
    void SetBackgroundFlushScheduled(bool scheduled) { background_flush_scheduled_.store(scheduled); }
    
    // Synchronization
    Mutex& GetMutex() { return mutex_; }
    CondVar& GetBackgroundWorkFinishedSignal() { return background_work_finished_signal_; }
    CondVar& GetWriteFinishedSignal() { return write_finished_signal_; }
    
    // Writer management
    std::deque<Writer*>& GetWriters() { return writers_; }
    WriteBatch* GetTmpBatch() { return tmp_batch_; }
    
    // Adaptation components
    BackgroundWorkManager* GetBackgroundWorkManager() { return background_work_manager_; }
    HotspotManager* GetHotspotManager() { return hotspot_manager_; }
    TreeAdaptationOperations* GetTreeAdaptationOps() { return tree_adaptation_ops_; }
    WriteBatchProcessor* GetWriteBatchProcessor() { return write_batch_processor_; }
    DefaultAdapt* GetAdaptStrategy() { return adapt_strategy_; }
    
    // Working state
    std::vector<uint32_t>& GetNodesInProgress() { return nodes_in_progress_; }
    
    // Hotspot range methods
    const std::string& GetLowHotKey() const;
    const std::string& GetHighHotKey() const;
    bool RangeWithinHotspot(const Slice& low, const Slice& up = Slice()) const;
    
    // Idle state checking
    bool BGFlushIdle();
    bool BGCompactionIdle();
    
    uint64_t NextNodeId() { return max_node_id_.fetch_add(1); }

protected:
    // Core tree data
    std::atomic<uint32_t> tree_height_;
    std::atomic<uint64_t> max_node_id_;
    std::atomic<uint64_t>& flush_id_;
    
    // Configuration
    TreeConfig config_;
    Options leveldb_options_;
    std::string root_lsmt_path_;
    
    // Components
    BufferManager* buffer_manager_;
    TreeLockManager* lock_manager_;
    TableCache* table_cache_;
    const InternalKeyComparator internal_comparator_;
    const InternalFilterPolicy internal_filter_policy_;
    ArenaWrapper arena_wrapper_;
    
    // Tree structure
    Node* root_;
    const uint32_t root_pg_id_;
    TbbTable node_table_;
    
    // LSMT
    LevelDBLSMT* leveldb_lsmt_ GUARDED_BY(mutex_);
    
    // Memory tables
    MemTable* mem_;
    MemTable* imm_ GUARDED_BY(mutex_);
    
    // State management
    BplusTreeState* btree_state_;
    PageSplitPolicy* page_split_policy_ = nullptr;
    
    // Adaptation
    DefaultAdapt* adapt_strategy_ = nullptr;
    BackgroundWorkManager* background_work_manager_;
    HotspotManager* hotspot_manager_;
    TreeAdaptationOperations* tree_adaptation_ops_;
    WriteBatchProcessor* write_batch_processor_;
    
    // Statistics
    ReaderStats reader_stats_;
    WriterStats writer_stats_;
    
    // Sequence management
    std::atomic<SequenceNumber> seq_no_;
    
    // State flags
    std::atomic<bool> mem_empty_;
    std::atomic<bool> mem_cold_only_;
    std::atomic<bool> imm_cold_only_;
    std::atomic<bool> has_imm_;
    std::atomic<bool> shutting_down_;
    std::atomic<bool> write_active_;
    std::atomic<bool> pushdown_active_;
    std::atomic<bool> compact_active_;
    std::atomic<uint32_t> write_wait_;
    std::atomic<bool> bg_working_;
    std::atomic<bool> background_flush_scheduled_;
    
    // Synchronization
    mutable Mutex mutex_;
    CondVar background_work_finished_signal_ GUARDED_BY(mutex_);
    CondVar write_finished_signal_ GUARDED_BY(mutex_);
    
    // Writer management
    std::deque<Writer*> writers_ GUARDED_BY(mutex_);
    WriteBatch* tmp_batch_ GUARDED_BY(mutex_);
    
    // Background work
    bool background_compaction_scheduled_ GUARDED_BY(mutex_);
    
    // Working state
    std::vector<uint32_t> nodes_in_progress_;
    std::set<uint64_t> pending_outputs_ GUARDED_BY(mutex_);
    
    // Environment
    Env* const env_;
    
    // Miscellaneous
    bool print_page_ = false;
    std::atomic<uint32_t> seed_;

private:
    // Helper methods
    void DeleteNode(uint32_t page_id);
    Options SanitizeOptions(const Options& src, const InternalKeyComparator* icmp,
                           const InternalFilterPolicy* ipolicy);
    
    virtual Status MakeRoomForWrite(bool force) EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;
    WriteBatch* BuildBatchGroup(Writer** last_writer) EXCLUSIVE_LOCKS_REQUIRED(mutex_);
    virtual void MaybeScheduleCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

    friend class Node;
    friend class SortedTreeIterator;
    friend class PageIterator;
    friend class BackgroundWorkManager;
    friend class HotspotManager;
    friend class TreeAdaptationOperations;
    friend class WriteBatchProcessor;
};

// Concrete implementations would derive from BplusTree
class ConcreteBplusTree : public BplusTree {
public:
    ConcreteBplusTree(Options options, const std::string& dbname,
                     std::atomic<uint64_t>& flush_id, bool is_lsmt = false,
                     bool is_buffer_tree = false);
    
    virtual ~ConcreteBplusTree();

    // Pure virtual method implementations
    int GetLockedRoot() override;
    bool NodeTopLevelOverflows(Node* node) override;
    bool AllChildrenLeaf(uint32_t pg_id) override;
    
    void MergeWithAllChildrenLeaf(uint32_t pg_id, int level_of_files,
                                 std::vector<std::shared_ptr<FileMetaData>>* files,
                                 std::vector<SlicePageMap>* new_pivots,
                                 std::vector<std::pair<Slice, uint32_t>>* old_pivots) override;
    
    PageSplitPolicy* GetPageSplitPolicy(size_t current_size, size_t target_size) override;
    
    void AdaptMem() override;
    void AdaptImm() override;
    void AdaptWOT() override;
    
    void UpdateRoot(SlicePageMap result) override;
    
    void BackgroundCall() override;
    void CompactMemTable() EXCLUSIVE_LOCKS_REQUIRED(mutex_) override;
    
private:
    Status MakeRoomForWrite(bool force) EXCLUSIVE_LOCKS_REQUIRED(mutex_) override;
    void MaybeScheduleCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_) override;
};

} // namespace WOT_NAMESPACE

#endif // WOT_BTREE_REFACTOR_H