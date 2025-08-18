#include "wot_utilities_refactor_.h"
#include "wot_btree_refactor_.h"
#include "wot_tree_node_refactor_.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"
#include "leveldb/table/merger.h"
#include "wot_buf_mgr/buffer_pool_helper.h"
#include "util/coding.h"
#include <iostream>
#include <fstream>

#ifdef BREAKDOWN
#include <x86intrin.h>
#define _rdtsc __rdtsc
#endif

namespace WOT_NAMESPACE {

// ConfigUtils implementation
Options ConfigUtils::SanitizeOptions(const Options& src, 
                                     const InternalKeyComparator* icmp,
                                     const InternalFilterPolicy* ipolicy) {
    Options result = src;
    result.comparator = icmp;
    result.filter_policy = ipolicy;

    if (result.block_cache == nullptr) {
        result.block_cache = NewLRUCache(8 << 20);
        fprintf(stdout, "Use 8MB block cache\n");
    }
    return result;
}

TreeConfig ConfigUtils::CreateDefaultConfig() {
    TreeConfig config;
    config.node_lsmt_level_limit = 3;
    config.lsmt_level_limit = 7;
    config.leaf_limit = 64;
    config.write_buffer_size = 4 << 20; // 4MB
    config.flush_file_num = 4;
    config.adapt_strategy = 1;
    config.buffer_shrink_ratio = 2;
    config.buffer_manager_num = 1024;
    config.first_page_split_policy = 1;
    config.second_page_split_policy = 1;
    config.proactive_validation = false;
    
    ValidateConfig(config);
    return config;
}

TreeConfig ConfigUtils::LoadFromOptions(const Options& opts) {
    TreeConfig config = CreateDefaultConfig();
    
    // Map options to TreeConfig
    if (opts.write_buffer_size > 0) {
        config.write_buffer_size = opts.write_buffer_size;
    }
    
    // Add other option mappings as needed
    ValidateConfig(config);
    return config;
}

int ConfigUtils::TableCacheSize(const Options& sanitized_options) {
    // Reserve ten files or so for other uses and give the rest to TableCache.
    return sanitized_options.max_open_files - 10;
}

void ConfigUtils::ValidateConfig(TreeConfig& config) {
    if (config.node_lsmt_level_limit < 1) config.node_lsmt_level_limit = 1;
    if (config.lsmt_level_limit < 1) config.lsmt_level_limit = 1;
    if (config.leaf_limit < 1) config.leaf_limit = 1;
    if (config.write_buffer_size < 1024) config.write_buffer_size = 1024;
    if (config.flush_file_num < 1) config.flush_file_num = 1;
    if (config.buffer_manager_num < 64) config.buffer_manager_num = 64;
}

// MemoryUtils implementation
size_t MemoryUtils::CalculateMemoryUsage(BplusTree* tree) {
    size_t total_size = 0;
    
    // Memtable
    if (tree->GetMem()) {
        total_size += tree->GetMem()->ApproximateMemoryUsage();
    }
    
    // Immutable memtable
    if (tree->GetImm()) {
        total_size += tree->GetImm()->ApproximateMemoryUsage();
    }
    
    // Buffer manager
    total_size += CalculateBufferManagerMemoryUsage(tree->GetBufferManager());
    
    // Table cache (optional - can be expensive to calculate)
    // total_size += CalculateTableCacheMemoryUsage(tree->GetTableCache());
    
    return total_size;
}

size_t MemoryUtils::CalculateNodeMemoryUsage(Node* node) {
    size_t size = sizeof(Node);
    
    if (node->GetNodeLSMT()) {
        // Add LSMT memory usage - this would need to be implemented in LSMT
        size += 1024; // Placeholder estimate
    }
    
    return size;
}

size_t MemoryUtils::CalculateBufferManagerMemoryUsage(BufferManager* buffer_mgr) {
    // This would need to be implemented in BufferManager
    return 0; // Placeholder
}

size_t MemoryUtils::CalculateTableCacheMemoryUsage(TableCache* table_cache) {
    // This would need to be implemented in TableCache
    return 0; // Placeholder
}

size_t MemoryUtils::CalculatePageOverhead() {
    return sizeof(TreePageHeaderData);
}

size_t MemoryUtils::CalculateEntrySize(const Slice& key) {
    return key.size() + 3 * ITEMID_SIZE;
}

size_t MemoryUtils::CalculateAvailablePageSpace() {
    return PAGE_SIZE - CalculatePageOverhead();
}

// KeyUtils implementation
std::string KeyUtils::CreateInternalKey(const std::string& user_key, 
                                        uint64_t sequence, 
                                        ValueType type) {
    std::string result;
    AppendInternalKey(&result, ParsedInternalKey(user_key, sequence, type));
    return result;
}

bool KeyUtils::ParseInternalKey(const Slice& key, ParsedInternalKey* parsed) {
    return leveldb::ParseInternalKey(key, parsed);
}

Slice KeyUtils::ExtractUserKey(const Slice& internal_key) {
    return leveldb::ExtractUserKey(internal_key);
}

void KeyUtils::AppendInternalKey(std::string* result, const ParsedInternalKey& key) {
    leveldb::AppendInternalKey(result, key);
}

int KeyUtils::CompareUserKeys(const Slice& a, const Slice& b, 
                             const Comparator* comparator) {
    return comparator->Compare(a, b);
}

Slice KeyUtils::NewPivot(ArenaWrapper& arena, const Slice& key) {
    return Slice(arena.Allocate(key.size()), key.size());
}

std::vector<Slice> KeyUtils::GenerateGuards(const std::vector<std::shared_ptr<FileMetaData>>& files,
                                           int split_count) {
    std::vector<Slice> guards;
    
    if (files.empty() || split_count <= 1) {
        return guards;
    }
    
    // Generate evenly distributed guard keys
    int files_per_split = files.size() / split_count;
    for (int i = 0; i < split_count - 1; i++) {
        int file_index = (i + 1) * files_per_split;
        if (file_index < files.size()) {
            guards.push_back(files[file_index]->smallest.user_key());
        }
    }
    
    return guards;
}

// FileUtils implementation
bool FileUtils::FileAcrossPivots(BufferManager* buffer_manager, uint32_t page_id,
                                std::vector<std::shared_ptr<FileMetaData>>* files,
                                const Comparator* comparator) {
    if (files->empty()) return false;
    
    Page page = buffer_manager->Pin(page_id);
    if (!page) return false;
    
    uint32_t item_num = ((TreePageHeader) page)->item_num_;
    bool crosses = false;
    
    // Check if any file spans across pivot boundaries
    for (auto const& file : *files) {
        for (int i = 0; i < item_num - 1; i++) {
            Slice pivot = PageReadPivotAtOffset(page, i);
            Slice next_pivot = PageReadPivotAtOffset(page, i + 1);
            
            if (comparator->Compare(file->smallest.user_key(), pivot) < 0 &&
                comparator->Compare(file->largest.user_key(), next_pivot) >= 0) {
                crosses = true;
                break;
            }
        }
        if (crosses) break;
    }
    
    buffer_manager->Unpin(page_id);
    return crosses;
}

void FileUtils::SortFilesByKey(std::vector<std::shared_ptr<FileMetaData>>* files,
                              const Comparator* comparator) {
    std::sort(files->begin(), files->end(),
             [comparator](const std::shared_ptr<FileMetaData>& a, 
                         const std::shared_ptr<FileMetaData>& b) {
                 return comparator->Compare(a->smallest.user_key(), 
                                           b->smallest.user_key()) < 0;
             });
}

void FileUtils::RemoveFiles(Env* env, const std::string& path, 
                           const std::set<uint64_t>& file_numbers) {
    for (uint64_t file_num : file_numbers) {
        std::string filename = TableFileName(path, file_num);
        env->DeleteFile(filename);
    }
}

uint64_t FileUtils::TotalFileSize(const std::vector<std::shared_ptr<FileMetaData>>& files) {
    uint64_t total = 0;
    for (const auto& file : files) {
        total += file->file_size;
    }
    return total;
}

uint64_t FileUtils::TotalEntryCount(const std::vector<std::shared_ptr<FileMetaData>>& files) {
    uint64_t total = 0;
    for (const auto& file : files) {
        total += file->file_entry;
    }
    return total;
}

void FileUtils::GetFileRange(const std::vector<std::shared_ptr<FileMetaData>>& files,
                            Slice* smallest, Slice* largest) {
    if (files.empty()) {
        *smallest = Slice();
        *largest = Slice();
        return;
    }
    
    *smallest = files.front()->smallest.user_key();
    *largest = files.back()->largest.user_key();
    
    for (const auto& file : files) {
        if (smallest->compare(file->smallest.user_key()) > 0) {
            *smallest = file->smallest.user_key();
        }
        if (largest->compare(file->largest.user_key()) < 0) {
            *largest = file->largest.user_key();
        }
    }
}

// IteratorUtils implementation
Iterator* IteratorUtils::NewMergingIterator(const InternalKeyComparator* comparator,
                                           Iterator** children, int n) {
    return leveldb::NewMergingIterator(comparator, children, n);
}

std::vector<Iterator*> IteratorUtils::CreateIteratorSet(BplusTree* tree,
                                                       const Slice& low, const Slice& up) {
    std::vector<Iterator*> iters;
    
    // Add memtable iterator
    if (tree->GetMem()) {
        iters.push_back(tree->GetMem()->NewIterator());
    }
    
    // Add immutable memtable iterator
    if (tree->GetImm()) {
        iters.push_back(tree->GetImm()->NewIterator());
    }
    
    // Add LSMT iterator
    if (tree->GetLSMT()) {
        ReadOptions options;
        options.fill_cache = false;
        iters.push_back(tree->GetLSMT()->NewMergedIterator(options));
    }
    
    return iters;
}

void IteratorUtils::CleanupIterators(std::vector<Iterator*>& iterators) {
    for (auto iter : iterators) {
        delete iter;
    }
    iterators.clear();
}

bool IteratorUtils::IsIteratorValid(Iterator* iter) {
    return iter && iter->Valid();
}

void IteratorUtils::SeekIteratorToRange(Iterator* iter, const Slice& start, const Slice& end) {
    if (iter) {
        iter->Seek(start);
        // Additional range validation could be added here
    }
}

// PageUtils implementation
void PageUtils::PageInit(Page page, uint32_t page_id) {
    memset(page, 0, PAGE_SIZE);
    TreePageHeader header = (TreePageHeader)page;
    header->page_id_ = page_id;
    header->item_num_ = 0;
    header->is_leaf_ = false;
    header->is_dirty_ = false;
    header->left_ = 0;
    header->right_ = 0;
}

void PageUtils::PagePrint(Page page) {
    TreePageHeader header = (TreePageHeader)page;
    std::cout << "Page " << header->page_id_ << " (leaf=" << header->is_leaf_ 
              << ", items=" << header->item_num_ << ")";
}

void PageUtils::PagePrintStat(Page page) {
    TreePageHeader header = (TreePageHeader)page;
    std::cout << "Page " << header->page_id_ << ": " << header->item_num_ << " items";
}

size_t PageUtils::PageGetFreeSpace(Page page) {
    TreePageHeader header = (TreePageHeader)page;
    size_t used = sizeof(TreePageHeaderData) + header->item_num_ * sizeof(ItemData);
    return PAGE_SIZE - used;
}

uint32_t PageUtils::PageGetItemCount(Page page) {
    return ((TreePageHeader)page)->item_num_;
}

void PageUtils::PageInsertIndexEntry(Page page, const Slice& key, uint32_t child) {
    // Implementation would depend on page format
    // This is a simplified stub
    TreePageHeader header = (TreePageHeader)page;
    header->item_num_++;
    header->is_dirty_ = true;
}

void PageUtils::PageUpdateMinPivot(Page page, const Slice& key) {
    // Implementation would update the minimum pivot in the page
    // This is a simplified stub
    TreePageHeader header = (TreePageHeader)page;
    header->is_dirty_ = true;
}

Slice PageUtils::PageReadPivotAtOffset(Page page, int offset) {
    // Implementation would read the pivot at the given offset
    // This is a simplified stub
    return Slice("pivot"); // Placeholder
}

uint32_t PageUtils::PageReadChildAtOffset(Page page, int offset) {
    // Implementation would read the child page ID at the given offset
    // This is a simplified stub
    return 0; // Placeholder
}

int PageUtils::PageGetOffsetByChild(Page page, uint32_t child_id) {
    // Implementation would find the offset for a given child ID
    // This is a simplified stub
    return -1; // Placeholder
}

void PageUtils::GetChildPageIds(BufferManager* buffer_manager, uint32_t page_id,
                               std::vector<uint32_t>* child_ids) {
    Page page = buffer_manager->Pin(page_id);
    if (!page) return;
    
    uint32_t item_num = ((TreePageHeader)page)->item_num_;
    for (int i = 0; i < item_num; i++) {
        child_ids->push_back(PageReadChildAtOffset(page, i));
    }
    
    buffer_manager->Unpin(page_id);
}

bool PageUtils::IsLeafPage(BufferManager* buffer_manager, uint32_t page_id) {
    Page page = buffer_manager->Lookup(page_id);
    if (!page) return false;
    return ((TreePageHeader)page)->is_leaf_;
}

void PageUtils::UpdateChildByPivot(BufferManager* buffer_manager, uint32_t page_id,
                                  const Slice& pivot, uint32_t new_child) {
    // Implementation would update child pointer for a given pivot
    // This is a simplified stub
}

void PageUtils::AddNewPivots(BufferManager* buffer_manager, uint32_t page_id,
                            SlicePageMap* new_pivots) {
    // Implementation would add new pivots to the page
    // This is a simplified stub
}

Slice PageUtils::GetPivotAtOffset(BufferManager* buffer_manager, uint32_t page_id,
                                 ArenaWrapper& arena, int offset) {
    // Implementation would get pivot at offset, allocated in arena
    // This is a simplified stub
    return Slice("pivot"); // Placeholder
}

SlicePageMap PageUtils::WriteToPage(BplusTree* tree, const InternalKeyComparator* icmp,
                                   Iterator* iter, uint64_t entry_num) {
    SlicePageMap result;
    
    if (!iter || !iter->Valid()) {
        return result;
    }
    
    // Implementation would write iterator contents to pages
    // This is a complex operation that would need full page format knowledge
    
    return result;
}

// RandomUtils implementation
uint32_t RandomUtils::GenerateSeed() {
    return static_cast<uint32_t>(time(nullptr));
}

uint32_t RandomUtils::NextRandom(uint32_t seed) {
    return seed * 1103515245 + 12345;
}

size_t RandomUtils::RandomCompactionPeriod(Random& rnd) {
    return rnd.Uniform(2 * config::kReadBytesPeriod);
}

bool RandomUtils::ShouldSample(Random& rnd, size_t bytes_read, size_t& bytes_until_sampling) {
    while (bytes_until_sampling < bytes_read) {
        bytes_until_sampling += RandomCompactionPeriod(rnd);
        return true;
    }
    bytes_until_sampling -= bytes_read;
    return false;
}

// TimingUtils implementation
uint64_t TimingUtils::GetCurrentTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

double TimingUtils::CalculateElapsedSeconds(uint64_t start, uint64_t end) {
    return (end - start) / 1e9; // Convert nanoseconds to seconds
}

uint64_t TimingUtils::GetCycleCount() {
#ifdef BREAKDOWN
    return _rdtsc();
#else
    return GetCurrentTimestamp();
#endif
}

void TimingUtils::RecordLatency(std::atomic<uint64_t>& counter, uint64_t start_time) {
    uint64_t end_time = GetCycleCount();
    counter.fetch_add(end_time - start_time);
}

void TimingUtils::UpdateMovingAverage(std::atomic<uint64_t>& avg, uint64_t new_value, int count) {
    uint64_t current = avg.load();
    uint64_t new_avg = ((current * count) + new_value) / (count + 1);
    avg.store(new_avg);
}

// DebugUtils implementation
void DebugUtils::PrintNodeStructure(Node* node, int level, bool print_lsmt) {
    if (!node) {
        std::cout << "NULL Node\n";
        return;
    }
    
    for (int i = 0; i < level; i++) std::cout << "  ";
    std::cout << "Node " << node->pg_id_ << ": ";
    
    if (print_lsmt && node->GetNodeLSMT()) {
        node->GetNodeLSMT()->Print();
    }
    std::cout << "\n";
}

void DebugUtils::PrintTreeStructure(BplusTree* tree) {
    std::cout << "=== Tree Structure ===\n";
    std::cout << "Height: " << tree->TreeHeight() << "\n";
    
    if (tree->GetLSMT()) {
        std::cout << "Root LSMT:\n";
        tree->GetLSMT()->Print();
    }
    
    tree->Print();
}

void DebugUtils::PrintPageContents(Page page, const std::string& prefix) {
    std::cout << prefix;
    PagePrint(page);
    std::cout << "\n";
}

void DebugUtils::DumpTreeToFile(BplusTree* tree, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file for tree dump: " << filename << "\n";
        return;
    }
    
    file << "Tree Height: " << tree->TreeHeight() << "\n";
    file << "Node Table Size: " << tree->GetNodeTable().size() << "\n";
    
    // Add more detailed tree information
    file.close();
}

void DebugUtils::ValidateTreeStructure(BplusTree* tree) {
    // Implementation would perform comprehensive tree validation
    // This is a complex operation that would check:
    // - Tree height consistency
    // - Node connectivity
    // - Key ordering
    // - Page integrity
    std::cout << "Tree validation not yet implemented\n";
}

void DebugUtils::CheckMemoryLeaks() {
    // Implementation would check for memory leaks
    // This might use custom allocator tracking or external tools
}

void DebugUtils::PrintMemoryUsage(BplusTree* tree) {
    size_t total = MemoryUtils::CalculateMemoryUsage(tree);
    std::cout << "Total memory usage: " << total << " bytes (" 
              << (total / (1024*1024)) << " MB)\n";
}

void DebugUtils::PrintPerformanceStats(const ReaderStats& reader_stats,
                                       const WriterStats& writer_stats) {
    std::cout << "=== Performance Statistics ===\n";
    
    std::cout << "Reader Stats:\n";
    reader_stats.PrintStats();
    
    std::cout << "Writer Stats:\n";
    writer_stats.PrintStats();
}

// ConcurrencyUtils implementation
void ConcurrencyUtils::AcquireReadLocks(TreeLockManager* lock_mgr, 
                                       const std::vector<uint32_t>& page_ids) {
    for (uint32_t page_id : page_ids) {
        lock_mgr->ReadLock(page_id);
    }
}

void ConcurrencyUtils::ReleaseReadLocks(TreeLockManager* lock_mgr,
                                       const std::vector<uint32_t>& page_ids) {
    for (uint32_t page_id : page_ids) {
        lock_mgr->ReadUnlock(page_id);
    }
}

void ConcurrencyUtils::AcquireWriteLocks(TreeLockManager* lock_mgr,
                                        const std::vector<uint32_t>& page_ids) {
    for (uint32_t page_id : page_ids) {
        lock_mgr->WriteLock(page_id);
    }
}

void ConcurrencyUtils::ReleaseWriteLocks(TreeLockManager* lock_mgr,
                                        const std::vector<uint32_t>& page_ids) {
    for (uint32_t page_id : page_ids) {
        lock_mgr->WriteUnlock(page_id);
    }
}

Node* ConcurrencyUtils::GetNodeSafe(const TbbTable& node_table, uint32_t page_id) {
    ReadTbb accessor;
    if (node_table.find(accessor, page_id)) {
        Node* node = accessor->second;
        accessor.release();
        return node;
    }
    return nullptr;
}

bool ConcurrencyUtils::InsertNodeSafe(TbbTable& node_table, uint32_t page_id, Node* node) {
    WriteTbb accessor;
    bool inserted = node_table.insert(accessor, page_id);
    if (inserted) {
        accessor->second = node;
    }
    accessor.release();
    return inserted;
}

bool ConcurrencyUtils::RemoveNodeSafe(TbbTable& node_table, uint32_t page_id) {
    return node_table.erase(page_id);
}

// ErrorUtils implementation
Status ErrorUtils::CreateErrorStatus(const std::string& message) {
    return Status::IOError(message);
}

Status ErrorUtils::CreateCorruptionStatus(const std::string& message) {
    return Status::Corruption(message);
}

Status ErrorUtils::CreateIOErrorStatus(const std::string& message) {
    return Status::IOError(message);
}

bool ErrorUtils::IsRetryableError(const Status& status) {
    // Define which errors are retryable
    return status.IsIOError() && !status.IsCorruption();
}

void ErrorUtils::HandleFatalError(const std::string& message) {
    std::cerr << "FATAL ERROR: " << message << std::endl;
    std::abort();
}

bool ErrorUtils::ValidatePageId(uint32_t page_id) {
    return page_id != 0; // Assuming 0 is invalid
}

bool ErrorUtils::ValidateNodePointer(Node* node) {
    return node != nullptr;
}

bool ErrorUtils::ValidateSlice(const Slice& slice) {
    return slice.data() != nullptr || slice.size() == 0;
}

} // namespace WOT_NAMESPACE