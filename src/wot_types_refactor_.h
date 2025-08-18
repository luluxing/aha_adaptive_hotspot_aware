#ifndef WOT_TYPES_REFACTOR_H
#define WOT_TYPES_REFACTOR_H

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "leveldb/include/slice.h"
#include "leveldb/include/status.h"
#include "leveldb/dbformat.h"
#include "wot_buf_mgr/buffer_manager.h"

namespace WOT_NAMESPACE {

using leveldb::Slice;
using leveldb::Status;
using leveldb::InternalKeyComparator;

// Forward declarations
class Node;
class BplusTree;
class TreeIterator;
class BufferManager;
class TreeLockManager;

// Type aliases
typedef std::map<Slice, uint32_t> SlicePageMap;
typedef oneapi::tbb::concurrent_hash_map<uint32_t, Node*> TbbTable;
typedef TbbTable::const_accessor ReadTbb;
typedef TbbTable::accessor WriteTbb;

// Enums
enum class BtState : uint8_t {
    kBtOverflow,
    kBtUnderflow,
    kBtNOrmal,
};

enum class IterState : uint8_t {
    kMem,
    kImm,
    kLSMT,
    kTree,
    kMixed,
    kInvalid
};

enum class OpState : uint8_t {
    kSuccess,
    kOverflow,
    kUnderflow,
    kError
};

// Statistics structures
struct ReaderStats {
    std::atomic<uint64_t> wait_for_lock_time{0};
    std::atomic<uint64_t> wait_lsmt_time{0};
    std::atomic<uint64_t> wait_root_time{0};
    std::atomic<uint64_t> traverse_time{0};
    std::atomic<uint64_t> reader_cnt{0};
    std::atomic<uint64_t> reader_time{0};

    void Reset();
    void PrintStats();
};

struct WriterStats {
    std::atomic<uint64_t> mem_time{0};
    std::atomic<uint64_t> imm_time{0};
    std::atomic<uint64_t> lsmt_ltime{0};
    std::atomic<uint64_t> tree_time{0};
    std::atomic<uint64_t> mem_cnt{0};
    std::atomic<uint64_t> imm_cnt{0};
    std::atomic<uint64_t> lsmt_cnt{0};
    std::atomic<uint64_t> tree_cnt{0};
    std::atomic<uint64_t> initial_check_time{0};
    std::atomic<uint64_t> adapt_leaf_time{0};
    std::atomic<uint64_t> search_path_time{0};
    std::atomic<uint64_t> compact_ltime{0};
    std::atomic<uint64_t> flush_ltime{0};
    std::atomic<uint64_t> split_ltime{0};
    std::atomic<uint64_t> split_small_leaf_ltime{0};
    std::atomic<uint64_t> update_pivot_ltime{0};
    std::atomic<uint64_t> installed_buffer{0};
    std::atomic<uint64_t> real_work_time{0};

    void Reset();
    void PrintStats();
};

// Configuration structure
struct TreeConfig {
    uint32_t node_lsmt_level_limit = 3;
    uint32_t lsmt_level_limit = 7;
    int leaf_limit = 64;
    size_t write_buffer_size = 4 << 20; // 4MB
    int flush_file_num = 4;
    int adapt_strategy = 1;
    int buffer_shrink_ratio = 2;
    int buffer_manager_num = 1024;
    int first_page_split_policy = 1;
    int second_page_split_policy = 1;
    bool proactive_validation = false;
};

} // namespace WOT_NAMESPACE

#endif // WOT_TYPES_REFACTOR_H