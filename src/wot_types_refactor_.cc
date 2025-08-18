#include "wot_types_refactor_.h"
#include <iostream>

namespace WOT_NAMESPACE {

void ReaderStats::Reset() {
    wait_for_lock_time.store(0);
    wait_lsmt_time.store(0);
    wait_root_time.store(0);
    traverse_time.store(0);
    reader_cnt.store(0);
    reader_time.store(0);
}

void ReaderStats::PrintStats() {
    fprintf(stdout, "Total_read: %f, wait_for_lock: %f: lsmt %f, root %f, traverse: %f, reader_cnt: %lu\n",
        1.0*reader_time.load() / reader_cnt.load(), 
        1.0*wait_for_lock_time.load() / reader_cnt.load(), 
        1.0*wait_lsmt_time / reader_cnt.load(), 
        1.0*wait_root_time / reader_cnt.load(), 
        1.0*traverse_time.load() / reader_cnt.load(), 
        reader_cnt.load());
}

void WriterStats::Reset() {
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

void WriterStats::PrintStats() {
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

} // namespace WOT_NAMESPACE