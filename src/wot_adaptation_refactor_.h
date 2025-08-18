#ifndef WOT_ADAPTATION_REFACTOR_H
#define WOT_ADAPTATION_REFACTOR_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include "wot_types_refactor_.h"
#include "leveldb/include/status.h"
#include "leveldb/include/slice.h"

namespace WOT_NAMESPACE {

using leveldb::Status;
using leveldb::Slice;

// Forward declarations
class BplusTree;
class DefaultAdapt;

// Base adaptation strategy interface
class AdaptationStrategy {
public:
    AdaptationStrategy(BplusTree* tree) : tree_(tree) {}
    virtual ~AdaptationStrategy() = default;

    virtual void AdaptMem() = 0;
    virtual void AdaptImm() = 0;
    virtual void AdaptLSMT() = 0;
    virtual void AdaptWOT() = 0;

    virtual void SetHotKeys(const std::string& low, const std::string& high) = 0;
    virtual bool ShouldScheduleAdaptation() = 0;

protected:
    BplusTree* tree_;
};

// Background work management
class BackgroundWorkManager {
public:
    explicit BackgroundWorkManager(BplusTree* tree);
    ~BackgroundWorkManager();

    // Background work scheduling
    void ScheduleMemCompaction();
    void ScheduleImmCompaction();  
    void ScheduleLSMTCompaction();
    void ScheduleWOTCompaction();
    void ScheduleLSMTFlush();

    // Background work execution
    static void BGAdaptMem(void* db);
    static void BGAdaptImm(void* db);
    static void BGAdaptLSMT(void* db);
    static void BGAdaptWOT(void* db);
    static void BGFlushLSMT(void* db);

    // State management
    bool IsMemScheduled() const { return pushdown_mem_scheduled_.load(); }
    bool IsImmScheduled() const { return pushdown_imm_scheduled_.load(); }
    bool IsLSMTScheduled() const { return pushdown_lsmt_scheduled_.load(); }
    bool IsFlushScheduled() const { return background_flush_scheduled_.load(); }

    void SetMemScheduled(bool scheduled) { pushdown_mem_scheduled_.store(scheduled); }
    void SetImmScheduled(bool scheduled) { pushdown_imm_scheduled_.store(scheduled); }
    void SetLSMTScheduled(bool scheduled) { pushdown_lsmt_scheduled_.store(scheduled); }
    void SetFlushScheduled(bool scheduled) { background_flush_scheduled_.store(scheduled); }

    // Work counters
    int GetScheduledFlushCount() const { return scheduled_flush_num_.load(); }
    int GetScheduledCompactionCount() const { return scheduled_compaction_num_.load(); }
    
    void IncrementFlushCount() { scheduled_flush_num_.fetch_add(1); }
    void DecrementFlushCount() { scheduled_flush_num_.fetch_sub(1); }
    void IncrementCompactionCount() { scheduled_compaction_num_.fetch_add(1); }
    void DecrementCompactionCount() { scheduled_compaction_num_.fetch_sub(1); }

    // Idle state checking
    bool BGFlushIdle();
    bool BGCompactionIdle();

private:
    BplusTree* tree_;
    
    std::atomic<bool> pushdown_mem_scheduled_{false};
    std::atomic<bool> pushdown_imm_scheduled_{false};
    std::atomic<bool> pushdown_lsmt_scheduled_{false};
    std::atomic<bool> background_flush_scheduled_{false};
    
    std::atomic<int> scheduled_flush_num_{0};
    std::atomic<int> scheduled_compaction_num_{0};
    
    std::chrono::time_point<std::chrono::system_clock> exam_flush_time_;
    std::chrono::time_point<std::chrono::system_clock> exam_compaction_time_;
};

// Tree state management
class BplusTreeState {
public:
    virtual ~BplusTreeState() = default;
    
    virtual void NewIterator(BplusTree* tree, std::vector<Iterator*>* iters,
                           const Slice& low, const Slice& up,
                           MemTable** mem, MemTable** imm,
                           std::vector<int>* locked_nodes,
                           const Comparator* user_comparator,
                           int* non_page_num) = 0;
    
    bool has_buffer_page = false;
    bool proactive_validation = false;
};

class BplusTreeWriteOptState : public BplusTreeState {
public:
    BplusTreeWriteOptState() = default;
    virtual ~BplusTreeWriteOptState() = default;
    
    void NewIterator(BplusTree* tree, std::vector<Iterator*>* iters,
                    const Slice& low, const Slice& up,
                    MemTable** mem, MemTable** imm,
                    std::vector<int>* locked_nodes,
                    const Comparator* user_comparator,
                    int* non_page_num) override;
};

class BplusTreeReadOptState : public BplusTreeState {
public:
    explicit BplusTreeReadOptState(bool proactive_val = false) {
        proactive_validation = proactive_val;
    }
    virtual ~BplusTreeReadOptState() = default;
    
    void NewIterator(BplusTree* tree, std::vector<Iterator*>* iters,
                    const Slice& low, const Slice& up,
                    MemTable** mem, MemTable** imm,
                    std::vector<int>* locked_nodes,
                    const Comparator* user_comparator,
                    int* non_page_num) override;
};

// Hotspot management
class HotspotManager {
public:
    explicit HotspotManager(BplusTree* tree);
    ~HotspotManager() = default;

    void SetHotspotRange(const std::string& low, const std::string& high);
    bool RangeWithinHotspot(const Slice& low, const Slice& up = Slice()) const;
    
    const std::string& GetLowHotKey() const { return low_hot_key_; }
    const std::string& GetHighHotKey() const { return up_hot_key_; }
    
    void RecordReadSample(const Slice& key);

private:
    BplusTree* tree_;
    std::string low_hot_key_;
    std::string up_hot_key_;
};

// Tree operations for adaptation
class TreeAdaptationOperations {
public:
    explicit TreeAdaptationOperations(BplusTree* tree);
    ~TreeAdaptationOperations() = default;

    // LSMT operations
    void InstallLSMTFilesToBol();
    void FlushLSMTFilesToBol(int level_of_files,
                            std::vector<std::shared_ptr<FileMetaData>>* files,
                            bool one_level_only = false);
    void BulkloadLSMTFiles();
    
    // File management
    int BulkloadFiles(const std::vector<std::shared_ptr<FileMetaData>>* bottom_files,
                     std::vector<uint32_t>* top_level, bool small_leaf);
    
    void AddNewNode(uint32_t pg_id, bool leaf_node,
                   std::shared_ptr<FileMetaData> meta, bool small_leaf = false,
                   size_t file_size = 0);

    // State transitions
    void AdaptToRead();
    void StopAdaptToRead();

private:
    BplusTree* tree_;
};

// Write batch processing
class WriteBatchProcessor {
public:
    explicit WriteBatchProcessor(BplusTree* tree);
    ~WriteBatchProcessor() = default;

    Status Insert(WriteBatch* updates);
    Status MakeRoomForWrite(bool force);
    WriteBatch* BuildBatchGroup(Writer** last_writer);

private:
    BplusTree* tree_;
    
    // Writer management
    struct Writer {
        explicit Writer(port::Mutex* mu)
            : batch(nullptr), sync(false), done(false), cv(mu) {}

        Status status;
        WriteBatch* batch;
        bool sync;
        bool done;
        port::CondVar cv;
    };

    void ProcessWriterQueue();
    void MaybeScheduleCompaction();
    
    friend class BplusTree;
};

} // namespace WOT_NAMESPACE

#endif // WOT_ADAPTATION_REFACTOR_H