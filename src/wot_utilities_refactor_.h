#ifndef WOT_UTILITIES_REFACTOR_H
#define WOT_UTILITIES_REFACTOR_H

#include <vector>
#include <memory>
#include <string>

#include "wot_types_refactor_.h"
#include "leveldb/include/options.h"
#include "leveldb/include/comparator.h"
#include "leveldb/dbformat.h"

namespace WOT_NAMESPACE {

using leveldb::Options;
using leveldb::Comparator;
using leveldb::InternalKeyComparator;
using leveldb::InternalFilterPolicy;
using leveldb::FileMetaData;

// Configuration and options utilities
class ConfigUtils {
public:
    static Options SanitizeOptions(const Options& src, 
                                  const InternalKeyComparator* icmp,
                                  const InternalFilterPolicy* ipolicy);
    
    static TreeConfig CreateDefaultConfig();
    static TreeConfig LoadFromOptions(const Options& opts);
    
    static int TableCacheSize(const Options& sanitized_options);
    
private:
    static void ValidateConfig(TreeConfig& config);
};

// Memory and size calculation utilities
class MemoryUtils {
public:
    static size_t CalculateMemoryUsage(BplusTree* tree);
    static size_t CalculateNodeMemoryUsage(Node* node);
    static size_t CalculateBufferManagerMemoryUsage(BufferManager* buffer_mgr);
    static size_t CalculateTableCacheMemoryUsage(TableCache* table_cache);
    
    // Page size calculations
    static size_t CalculatePageOverhead();
    static size_t CalculateEntrySize(const Slice& key);
    static size_t CalculateAvailablePageSpace();
};

// Key and data manipulation utilities
class KeyUtils {
public:
    static std::string CreateInternalKey(const std::string& user_key, 
                                        uint64_t sequence, 
                                        ValueType type);
    
    static bool ParseInternalKey(const Slice& key, ParsedInternalKey* parsed);
    static Slice ExtractUserKey(const Slice& internal_key);
    
    static void AppendInternalKey(std::string* result, const ParsedInternalKey& key);
    
    // Key comparison utilities
    static int CompareUserKeys(const Slice& a, const Slice& b, 
                              const Comparator* comparator);
    
    // Pivot and guard key management
    static Slice NewPivot(ArenaWrapper& arena, const Slice& key);
    static std::vector<Slice> GenerateGuards(const std::vector<std::shared_ptr<FileMetaData>>& files,
                                            int split_count);
};

// File and data organization utilities
class FileUtils {
public:
    // File organization
    static bool FileAcrossPivots(BufferManager* buffer_manager, uint32_t page_id,
                                std::vector<std::shared_ptr<FileMetaData>>* files,
                                const Comparator* comparator);
    
    static void SortFilesByKey(std::vector<std::shared_ptr<FileMetaData>>* files,
                              const Comparator* comparator);
    
    static void RemoveFiles(Env* env, const std::string& path, 
                           const std::set<uint64_t>& file_numbers);
    
    // File metadata management
    static uint64_t TotalFileSize(const std::vector<std::shared_ptr<FileMetaData>>& files);
    static uint64_t TotalEntryCount(const std::vector<std::shared_ptr<FileMetaData>>& files);
    
    static void GetFileRange(const std::vector<std::shared_ptr<FileMetaData>>& files,
                            Slice* smallest, Slice* largest);
};

// Iterator utilities and helpers
class IteratorUtils {
public:
    static Iterator* NewMergingIterator(const InternalKeyComparator* comparator,
                                      Iterator** children, int n);
    
    static std::vector<Iterator*> CreateIteratorSet(BplusTree* tree,
                                                   const Slice& low, const Slice& up);
    
    static void CleanupIterators(std::vector<Iterator*>& iterators);
    
    // Iterator state management
    static bool IsIteratorValid(Iterator* iter);
    static void SeekIteratorToRange(Iterator* iter, const Slice& start, const Slice& end);
};

// Page management utilities
class PageUtils {
public:
    // Page operations
    static void PageInit(Page page, uint32_t page_id);
    static void PagePrint(Page page);
    static void PagePrintStat(Page page);
    
    static size_t PageGetFreeSpace(Page page);
    static uint32_t PageGetItemCount(Page page);
    
    // Page entry management
    static void PageInsertIndexEntry(Page page, const Slice& key, uint32_t child);
    static void PageUpdateMinPivot(Page page, const Slice& key);
    
    static Slice PageReadPivotAtOffset(Page page, int offset);
    static uint32_t PageReadChildAtOffset(Page page, int offset);
    
    static int PageGetOffsetByChild(Page page, uint32_t child_id);
    
    // Page navigation
    static void GetChildPageIds(BufferManager* buffer_manager, uint32_t page_id,
                               std::vector<uint32_t>* child_ids);
    
    static bool IsLeafPage(BufferManager* buffer_manager, uint32_t page_id);
    
    // Pivot management
    static void UpdateChildByPivot(BufferManager* buffer_manager, uint32_t page_id,
                                  const Slice& pivot, uint32_t new_child);
    
    static void AddNewPivots(BufferManager* buffer_manager, uint32_t page_id,
                            SlicePageMap* new_pivots);
    
    static Slice GetPivotAtOffset(BufferManager* buffer_manager, uint32_t page_id,
                                 ArenaWrapper& arena, int offset);
    
    // Page writing and serialization
    static SlicePageMap WriteToPage(BplusTree* tree, const InternalKeyComparator* icmp,
                                   Iterator* iter, uint64_t entry_num);
};

// Random number utilities
class RandomUtils {
public:
    static uint32_t GenerateSeed();
    static uint32_t NextRandom(uint32_t seed);
    
    // Random sampling for compaction
    static size_t RandomCompactionPeriod(Random& rnd);
    static bool ShouldSample(Random& rnd, size_t bytes_read, size_t& bytes_until_sampling);
};

// Performance and timing utilities
class TimingUtils {
public:
    // High-resolution timing
    static uint64_t GetCurrentTimestamp();
    static double CalculateElapsedSeconds(uint64_t start, uint64_t end);
    static uint64_t GetCycleCount(); // For cycle-accurate timing
    
    // Performance monitoring
    static void RecordLatency(std::atomic<uint64_t>& counter, uint64_t start_time);
    static void UpdateMovingAverage(std::atomic<uint64_t>& avg, uint64_t new_value, int count);
};

// Debug and logging utilities
class DebugUtils {
public:
    static void PrintNodeStructure(Node* node, int level, bool print_lsmt = true);
    static void PrintTreeStructure(BplusTree* tree);
    static void PrintPageContents(Page page, const std::string& prefix = "");
    
    static void DumpTreeToFile(BplusTree* tree, const std::string& filename);
    static void ValidateTreeStructure(BplusTree* tree);
    
    // Memory debugging
    static void CheckMemoryLeaks();
    static void PrintMemoryUsage(BplusTree* tree);
    
    // Performance debugging
    static void PrintPerformanceStats(const ReaderStats& reader_stats,
                                     const WriterStats& writer_stats);
};

// Concurrency utilities
class ConcurrencyUtils {
public:
    // Lock management helpers
    static void AcquireReadLocks(TreeLockManager* lock_mgr, 
                                const std::vector<uint32_t>& page_ids);
    
    static void ReleaseReadLocks(TreeLockManager* lock_mgr,
                                const std::vector<uint32_t>& page_ids);
    
    static void AcquireWriteLocks(TreeLockManager* lock_mgr,
                                 const std::vector<uint32_t>& page_ids);
    
    static void ReleaseWriteLocks(TreeLockManager* lock_mgr,
                                 const std::vector<uint32_t>& page_ids);
    
    // Safe node access
    static Node* GetNodeSafe(const TbbTable& node_table, uint32_t page_id);
    static bool InsertNodeSafe(TbbTable& node_table, uint32_t page_id, Node* node);
    static bool RemoveNodeSafe(TbbTable& node_table, uint32_t page_id);
};

// Error handling utilities
class ErrorUtils {
public:
    static Status CreateErrorStatus(const std::string& message);
    static Status CreateCorruptionStatus(const std::string& message);
    static Status CreateIOErrorStatus(const std::string& message);
    
    static bool IsRetryableError(const Status& status);
    static void HandleFatalError(const std::string& message);
    
    // Validation utilities
    static bool ValidatePageId(uint32_t page_id);
    static bool ValidateNodePointer(Node* node);
    static bool ValidateSlice(const Slice& slice);
};

} // namespace WOT_NAMESPACE

#endif // WOT_UTILITIES_REFACTOR_H