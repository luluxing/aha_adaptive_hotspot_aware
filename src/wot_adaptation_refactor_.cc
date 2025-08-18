#include "wot_adaptation_refactor_.h"
#include "wot_btree_refactor_.h"
#include "leveldb/write_batch_internal.h"
#include "leveldb/util/mutexlock.h"
#include "wot_buf_mgr/buffer_pool_helper.h"

namespace WOT_NAMESPACE {

// BackgroundWorkManager implementation
BackgroundWorkManager::BackgroundWorkManager(BplusTree* tree) : tree_(tree) {
}

BackgroundWorkManager::~BackgroundWorkManager() {
}

void BackgroundWorkManager::ScheduleMemCompaction() {
    if (tree_->HasImm() || pushdown_mem_scheduled_.load()) {
        return;
    }
    pushdown_mem_scheduled_.store(true, std::memory_order_release);
    scheduled_flush_num_.fetch_add(1);
    tree_->GetEnv()->SchedulePushdown(&BackgroundWorkManager::BGAdaptMem, tree_);
}

void BackgroundWorkManager::ScheduleImmCompaction() {
    if (!tree_->HasImm() || pushdown_imm_scheduled_.load()) {
        return;
    }
    pushdown_imm_scheduled_.store(true, std::memory_order_release);
    scheduled_flush_num_.fetch_add(1);
    tree_->GetEnv()->SchedulePushdown(&BackgroundWorkManager::BGAdaptImm, tree_);
}

void BackgroundWorkManager::ScheduleLSMTCompaction() {
    if (!tree_->IsMemEmpty() || tree_->HasImm() || pushdown_lsmt_scheduled_.load()) {
        return;
    }
    pushdown_lsmt_scheduled_.store(true, std::memory_order_release);
    scheduled_flush_num_.fetch_add(1);
    tree_->GetEnv()->SchedulePushdown(&BackgroundWorkManager::BGAdaptLSMT, tree_);
}

void BackgroundWorkManager::ScheduleWOTCompaction() {
    if (!tree_->IsMemEmpty() || tree_->HasImm() || tree_->GetLSMT()->HasHotspot()) {
        return;
    }
    scheduled_flush_num_.fetch_add(1);
    tree_->GetEnv()->SchedulePushdown(&BackgroundWorkManager::BGAdaptWOT, tree_);
}

void BackgroundWorkManager::ScheduleLSMTFlush() {
    tree_->GetEnv()->SchedulePushdown(&BackgroundWorkManager::BGFlushLSMT, tree_);
}

void BackgroundWorkManager::BGAdaptMem(void* db) {
    reinterpret_cast<BplusTree*>(db)->GetAdaptStrategy()->AdaptMem();
}

void BackgroundWorkManager::BGAdaptImm(void* db) {
    reinterpret_cast<BplusTree*>(db)->GetAdaptStrategy()->AdaptImm();
}

void BackgroundWorkManager::BGAdaptLSMT(void* db) {
    reinterpret_cast<BplusTree*>(db)->GetAdaptStrategy()->AdaptLSMT();
}

void BackgroundWorkManager::BGAdaptWOT(void* db) {
    reinterpret_cast<BplusTree*>(db)->GetAdaptStrategy()->AdaptWOT();
}

void BackgroundWorkManager::BGFlushLSMT(void* db) {
    BplusTree* tree = reinterpret_cast<BplusTree*>(db);
    tree->GetTreeAdaptationOps()->InstallLSMTFilesToBol();
}

bool BackgroundWorkManager::BGFlushIdle() {
    MutexLock l(&tree_->GetMutex());
    return !tree_->GetEnv()->PushdownWorkRunning();
}

bool BackgroundWorkManager::BGCompactionIdle() {
    MutexLock l(&tree_->GetMutex());
    return !tree_->GetEnv()->CompactionWorkRunning();
}

// HotspotManager implementation
HotspotManager::HotspotManager(BplusTree* tree) : tree_(tree) {
}

void HotspotManager::SetHotspotRange(const std::string& low, const std::string& high) {
    assert(low < high);
    low_hot_key_ = low;
    up_hot_key_ = high;
    tree_->GetAdaptStrategy()->SetHotKeys(low, high);
}

bool HotspotManager::RangeWithinHotspot(const Slice& low, const Slice& up) const {
    if (low_hot_key_.empty() && up_hot_key_.empty()) {
        // When the entire key space is hotspot
        return true;
    }
    if (up.empty()) {
        // Fixed selectivity where only start key (low key) is given
        if (low.compare(low_hot_key_) >= 0 && low.compare(up_hot_key_) <= 0) {
            return true;
        }
        return false;
    }
    if (low.compare(low_hot_key_) >= 0 && up.compare(up_hot_key_) <= 0) {
        // Hard limit. This hotspot range already includes the query range
        return true;
    }
    return false;
}

void HotspotManager::RecordReadSample(const Slice& key) {
    if (tree_->TreeHeight() == 1) {
        MutexLock l(&tree_->GetMutex());
        // Root should be locked. Potential issue here.
        if (tree_->GetLSMT()->RecordReadSample(key)) {
            tree_->GetBackgroundWorkManager()->ScheduleLSMTCompaction();
        }
    }
}

// TreeAdaptationOperations implementation
TreeAdaptationOperations::TreeAdaptationOperations(BplusTree* tree) : tree_(tree) {
}

void TreeAdaptationOperations::InstallLSMTFilesToBol() {
    if (IsLeafPage(tree_->GetBufferManager(), tree_->GetRootPageId())) {
        BulkloadLSMTFiles();
    } else {
        std::vector<std::shared_ptr<FileMetaData>> files;
        tree_->GetLockManager()->ReadLock(-1);
        tree_->GetLockManager()->ReadLock(tree_->GetRootPageId());
        
        int bottom_level;
        if (tree_->GetConfig().flush_file_num <= 0) {
            bottom_level = tree_->GetLSMT()->GetAllBottomLevelFiles(&files);
        } else {
            bottom_level = tree_->GetLSMT()->GetSomeBottomLevelFiles(&files, 
                                                                    tree_->GetConfig().flush_file_num);
        }
        
        if (bottom_level < 0) {
            tree_->GetLockManager()->ReadUnlock(tree_->GetRootPageId());
            tree_->GetLockManager()->ReadUnlock(-1);
            return;
        }
        
        if (FileAcrossPivots(tree_->GetBufferManager(), tree_->GetRootPageId(), &files,
                           tree_->GetUserComparator())) {
            // If the file ranges do not match, compact again with guards
            files.clear();
            Page rt = tree_->GetBufferManager()->Lookup(tree_->GetRootPageId());
            std::set<uint64_t> dead_files;
            tree_->GetLockManager()->EscalateLock(-1);
            tree_->GetLSMT()->CompactBottomLevel(rt);
            tree_->GetLSMT()->GetObsoleteFiles(dead_files);
            tree_->GetLockManager()->AlleviateLock(-1);
            LevelDBLSMT::RemoveFiles(tree_->GetEnv(), tree_->GetRootLSMTPath(), dead_files);
            bottom_level = tree_->GetLSMT()->GetAllBottomLevelFiles(&files);
        }
        
        if (files.size() > 0) {
            tree_->GetLockManager()->EscalateLock(tree_->GetRootPageId());
            Page rt = tree_->GetBufferManager()->Lookup(tree_->GetRootPageId());
            tree_->GetRoot()->MayUpdateMinPivot(rt, files[0]->smallest.user_key());
            tree_->GetLockManager()->AlleviateLock(tree_->GetRootPageId());
            FlushLSMTFilesToBol(bottom_level, &files);
            files.clear();
        } else {
            tree_->GetLockManager()->ReadUnlock(tree_->GetRootPageId());
            tree_->GetLockManager()->ReadUnlock(-1);
        }
    }
    tree_->GetLockManager()->WriteLock(-1);
    tree_->GetLSMT()->Finalize();
    tree_->GetLockManager()->WriteUnlock(-1);
    tree_->GetBackgroundWorkManager()->ScheduleLSMTCompaction();
}

void TreeAdaptationOperations::FlushLSMTFilesToBol(int level_of_files,
                                                   std::vector<std::shared_ptr<FileMetaData>>* files,
                                                   bool one_level_only) {
    if (FileAcrossPivots(tree_->GetBufferManager(), tree_->GetRootPageId(), files,
                       tree_->GetUserComparator())) {
        Page rt = tree_->GetBufferManager()->Lookup(tree_->GetRootPageId());
        PagePrint(rt);
        fprintf(stderr, "Error: files across pivots\n");
        std::abort();
    }
    
    std::vector<SlicePageMap> new_pivots;
    std::vector<std::pair<Slice, uint32_t>> old_pivots;
    tree_->GetRoot()->FlushFilesToChildren(level_of_files, files, &new_pivots, &old_pivots, one_level_only);
    
    if (new_pivots.size() > 0) {
        tree_->GetRoot()->UpdatePivots(&new_pivots, &old_pivots);
    }
    
    if (tree_->GetRoot()->node_overflow_) {
        auto result = tree_->GetRoot()->SplitNodeAndLSMT(&new_pivots, &old_pivots);
        tree_->GetLockManager()->EscalateLock(tree_->GetRootPageId());
        tree_->UpdateRoot(result);
        tree_->GetLockManager()->AlleviateLock(tree_->GetRootPageId());
    }
    tree_->GetLockManager()->ReadUnlock(tree_->GetRootPageId());
}

void TreeAdaptationOperations::BulkloadLSMTFiles() {
    tree_->GetLockManager()->ReadLock(-1);
    int level = tree_->GetLSMT()->GetBottomLevel();
    assert(level > 0);
    
    const std::vector<std::shared_ptr<FileMetaData>>* bottom_files =
        tree_->GetLSMT()->GetBottomLevelFiles(nullptr);
    assert(bottom_files->size() > 1);
    
    std::vector<uint32_t> top_level;
    int add_height = BulkloadFiles(bottom_files, &top_level, false);

    tree_->GetLockManager()->EscalateLock(-1);
    tree_->GetLockManager()->WriteLock(tree_->GetRootPageId());
    tree_->IncrementTreeHeight(add_height);
    
    ReadTbb a;
    tree_->GetNodeTable().find(a, top_level[0]);
    delete a->second;
    a.release();
    tree_->GetNodeTable().erase(top_level[0]);

    tree_->GetLockManager()->WriteLock(top_level[0]);
    Page new_root = tree_->GetBufferManager()->Pin(top_level[0]);
    Page old_root = tree_->GetBufferManager()->Lookup(tree_->GetRootPageId());
    memcpy(old_root, new_root, PAGE_SIZE);

    ((TreePageHeader) old_root)->page_id_ = tree_->GetRootPageId();
    ((TreePageHeader) old_root)->is_leaf_ = false;
    ((TreePageHeader) old_root)->is_dirty_ = true;
    
    tree_->GetLockManager()->WriteUnlock(top_level[0]);
    tree_->GetBufferManager()->Delete(top_level[0]);
    tree_->GetLSMT()->ClearBottomLevelFiles();
    
    tree_->GetLockManager()->WriteUnlock(tree_->GetRootPageId());
    tree_->GetLockManager()->AlleviateLock(-1);
    tree_->GetLockManager()->ReadUnlock(-1);
}

int TreeAdaptationOperations::BulkloadFiles(
    const std::vector<std::shared_ptr<FileMetaData>>* bottom_files,
    std::vector<uint32_t>* top_level, bool small_leaf) {
    
    auto f = bottom_files->at(0);
    size_t key_size = f->smallest.user_key().size();
    int add_height = 0;
    int top_level_size = bottom_files->size();
    bool leaf_level = true;
    std::vector<uint32_t> new_nodes;
    
    while (top_level_size > 1) {
        uint32_t insert_to_id = tree_->GetBufferManager()->Allocate();
        AddNewNode(insert_to_id, false, nullptr, small_leaf);
        Page insert_to_page = tree_->GetBufferManager()->Pin(insert_to_id);
        new_nodes.push_back(insert_to_id);
        int top_idx = 0;
        
        while (top_idx < top_level_size) {
            Slice min_k;
            uint32_t inserted;
            if (leaf_level) {
                inserted = tree_->GetBufferManager()->Allocate();
                AddNewNode(inserted, true, bottom_files->at(top_idx), small_leaf);
                Page leaf_page = tree_->GetBufferManager()->Pin(inserted);
                ((TreePageHeader) leaf_page)->is_leaf_ = true;
                ((TreePageHeader) leaf_page)->is_dirty_ = true;
                min_k = bottom_files->at(top_idx)->smallest.user_key();
            } else {
                inserted = top_level->at(top_idx);
                Page inserted_page = tree_->GetBufferManager()->Pin(inserted);
                min_k = PageReadPivotAtOffset(inserted_page, 0);       
            }
            
            size_t free_space = PageGetFreeSpace(insert_to_page);
            if (free_space < key_size + 3 * ITEMID_SIZE) {
                tree_->GetBufferManager()->UnpinAndRelease(insert_to_id);
                insert_to_id = tree_->GetBufferManager()->Allocate();
                AddNewNode(insert_to_id, false, nullptr, small_leaf);
                insert_to_page = tree_->GetBufferManager()->Pin(insert_to_id);
                new_nodes.push_back(insert_to_id);
            }
            PageInsertIndexEntry(insert_to_page, min_k, inserted);
            tree_->GetBufferManager()->UnpinAndRelease(inserted);
            top_idx++;
        }
        
        tree_->GetBufferManager()->UnpinAndRelease(insert_to_id);
        if (!leaf_level) {
            top_level->clear();
        }
        leaf_level = false;
        new_nodes.swap(*top_level);
        top_level_size = top_level->size();
        add_height++;
    }
    return add_height;
}

void TreeAdaptationOperations::AddNewNode(uint32_t pg_id, bool leaf_node,
                                         std::shared_ptr<FileMetaData> meta, 
                                         bool small_leaf, size_t file_size) {
    int level_lim = leaf_node ? (small_leaf ? 1 : tree_->GetConfig().node_lsmt_level_limit) : 
                                tree_->GetConfig().node_lsmt_level_limit;
    LSMTStatus lsmt_status = leaf_node ?
                    (small_leaf ? LSMTStatus::kSmallLeaf : LSMTStatus::kLeaf) : LSMTStatus::kBuffer;
    int leaf_lim = tree_->GetConfig().leaf_limit;
    
    Node* node = new Node(tree_, leaf_node, level_lim, tree_->GetEnv(), tree_->GetRootLSMTPath(),
                         tree_->GetFlushId(), &tree_->GetLevelDBOptions(), tree_->GetInternalComparator(),
                         tree_->GetTableCache(), lsmt_status, leaf_lim, true);
    
    if (file_size != 0) {
        node->GetNodeLSMT()->output_file_size = file_size;
    }
    
    WriteTbb a;
    if (tree_->GetNodeTable().find(a, pg_id)) {
        delete a->second;
    }
    tree_->GetNodeTable().insert(a, pg_id);
    a->second = node;
    node->pg_id_ = pg_id;

    if (meta != nullptr) {
        node->GetNodeLSMT()->AddFile(0, *meta.get(), nullptr);
    }
}

void TreeAdaptationOperations::AdaptToRead() {
    MutexLock l(&tree_->GetMutex());
    if (tree_->GetBTreeState() != nullptr) delete tree_->GetBTreeState();
    auto state = new BplusTreeReadOptState(tree_->GetConfig().proactive_validation);
    state->has_buffer_page = true;
    tree_->ChangeState(state);
}

void TreeAdaptationOperations::StopAdaptToRead() {
    MutexLock l(&tree_->GetMutex());
    bool hybrid = tree_->GetBTreeState()->has_buffer_page;
    if (tree_->GetBTreeState() != nullptr) delete tree_->GetBTreeState();
    auto state = new BplusTreeWriteOptState();
    state->has_buffer_page = hybrid;
    tree_->ChangeState(state);
    
    if (tree_->GetPageSplitPolicy() != nullptr) {
        delete tree_->GetPageSplitPolicy();
        tree_->SetPageSplitPolicy(nullptr);
    }
    
    if (hybrid) {
        uint32_t new_level = tree_->GetConfig().lsmt_level_limit > 1 ? 
                            tree_->GetConfig().lsmt_level_limit - 1 : 1;
        tree_->GetLSMT()->SetLevelLimit(new_level);
    }
}

// BplusTreeState implementations
void BplusTreeWriteOptState::NewIterator(BplusTree* tree, std::vector<Iterator*>* iters,
                                        const Slice& low, const Slice& up,
                                        MemTable** mem, MemTable** imm,
                                        std::vector<int>* locked_nodes,
                                        const Comparator* user_comparator,
                                        int* non_page_num) {
    // Implementation for write-optimized iterator creation
    *non_page_num = 0;
    
    // Add memtable iterators if they exist
    if (tree->GetMem()) {
        *mem = tree->GetMem();
        (*mem)->Ref();
        iters->push_back((*mem)->NewIterator());
        (*non_page_num)++;
    }
    
    if (tree->GetImm()) {
        *imm = tree->GetImm();
        (*imm)->Ref();
        iters->push_back((*imm)->NewIterator());
        (*non_page_num)++;
    }
    
    // Add LSMT iterator if exists
    if (tree->GetLSMT()) {
        ReadOptions options;
        options.fill_cache = false;
        iters->push_back(tree->GetLSMT()->NewMergedIterator(options));
        (*non_page_num)++;
    }
    
    // Add page iterators for tree structure
    // This is a simplified implementation
}

void BplusTreeReadOptState::NewIterator(BplusTree* tree, std::vector<Iterator*>* iters,
                                       const Slice& low, const Slice& up,
                                       MemTable** mem, MemTable** imm,
                                       std::vector<int>* locked_nodes,
                                       const Comparator* user_comparator,
                                       int* non_page_num) {
    // Implementation for read-optimized iterator creation
    // This would prioritize page-based iteration over LSMT iteration
    *non_page_num = 0;
    
    // Simplified implementation for now
    NewIterator(tree, iters, low, up, mem, imm, locked_nodes, user_comparator, non_page_num);
}

// WriteBatchProcessor implementation
WriteBatchProcessor::WriteBatchProcessor(BplusTree* tree) : tree_(tree) {
}

Status WriteBatchProcessor::Insert(WriteBatch* updates) {
    Writer w(&tree_->GetMutex());
    w.batch = updates;
    w.sync = false;
    w.done = false;

    MutexLock l(&tree_->GetMutex());
    tree_->GetWriters().push_back(&w);
    
    while (!w.done && &w != tree_->GetWriters().front()) {
        w.cv.Wait();
    }
    
    if (w.done) {
        return w.status;
    }

    Status status = MakeRoomForWrite(updates == nullptr);
    uint64_t last_sequence = tree_->GetSequenceNumber();
    Writer* last_writer = &w;
    
    if (status.ok() && updates != nullptr) {
        WriteBatch* write_batch = BuildBatchGroup(&last_writer);
        WriteBatchInternal::SetSequence(write_batch, last_sequence + 1);
        tree_->IncrementSequenceNumber(WriteBatchInternal::Count(write_batch));
        
        if (tree_->IsMemEmpty()) tree_->SetMemEmpty(false);
        if (tree_->IsMemColdOnly() && updates->HasHotspot()) {
            tree_->SetMemColdOnly(false);
        }

        {
            tree_->GetMutex().Unlock();
            if (status.ok()) {
                status = WriteBatchInternal::InsertInto(write_batch, tree_->GetMem());
            }
            tree_->GetMutex().Lock();
        }
        
        if (write_batch == tree_->GetTmpBatch()) {
            tree_->GetTmpBatch()->Clear();
        }
    }

    ProcessWriterQueue();
    return status;
}

Status WriteBatchProcessor::MakeRoomForWrite(bool force) {
    tree_->GetMutex().AssertHeld();
    Status s;
    
    while (true) {
        if (!tree_->IsShuttingDown() && 
            (!force && tree_->GetMem()->ApproximateMemoryUsage() <= tree_->GetConfig().write_buffer_size)) {
            // There is room in current memtable
            break;
        } else if (tree_->GetImm() != nullptr) {
            // We have filled up the current memtable, but the previous
            // one is still being compacted, so we wait.
            tree_->GetBackgroundWorkFinishedSignal().Wait();
        } else if (tree_->GetLSMT() && tree_->GetLSMT()->NumLevelFiles(tree_->GetConfig().lsmt_level_limit - 1) >= 12) {
            // There are too many level-L files.
            tree_->GetBackgroundWorkFinishedSignal().Wait();
        } else {
            // Attempt to switch to a new memtable and trigger compaction of old
            assert(tree_->GetImm() == nullptr);
            tree_->SetImm(tree_->GetMem());
            tree_->GetImm()->Ref();
            tree_->SetHasImm(true);
            tree_->SetMem(new MemTable(tree_->GetInternalComparator()));
            tree_->GetMem()->Ref();
            tree_->SetMemEmpty(true);
            tree_->SetMemColdOnly(true);
            MaybeScheduleCompaction();
        }
    }
    
    return s;
}

WriteBatch* WriteBatchProcessor::BuildBatchGroup(Writer** last_writer) {
    tree_->GetMutex().AssertHeld();
    assert(!tree_->GetWriters().empty());
    
    Writer* first = tree_->GetWriters().front();
    WriteBatch* result = first->batch;
    assert(result != nullptr);

    size_t size = WriteBatchInternal::ByteSize(first->batch);
    size_t max_size = 1 << 20;
    if (size <= (128 << 10)) {
        max_size = size + (128 << 10);
    }

    *last_writer = first;
    std::deque<Writer*>::iterator iter = tree_->GetWriters().begin();
    ++iter;
    
    for (; iter != tree_->GetWriters().end(); ++iter) {
        Writer* w = *iter;
        if (w->sync && !first->sync) {
            break;
        }

        if (w->batch != nullptr) {
            size += WriteBatchInternal::ByteSize(w->batch);
            if (size > max_size) {
                break;
            }

            if (result == first->batch) {
                result = tree_->GetTmpBatch();
                assert(WriteBatchInternal::Count(result) == 0);
                WriteBatchInternal::Append(result, first->batch);
            }
            WriteBatchInternal::Append(result, w->batch);
        }
        *last_writer = w;
    }
    return result;
}

void WriteBatchProcessor::ProcessWriterQueue() {
    // Implementation for processing the writer queue
    // This would handle writer notifications and cleanup
}

void WriteBatchProcessor::MaybeScheduleCompaction() {
    tree_->GetMutex().AssertHeld();
    if (tree_->IsBackgroundCompactionScheduled()) {
        // Already scheduled
    } else if (tree_->IsShuttingDown()) {
        // DB is being deleted; no more background compactions
    } else {
        tree_->SetBackgroundCompactionScheduled(true);
        tree_->GetEnv()->Schedule(&BplusTree::BGWork, tree_);
    }
}

} // namespace WOT_NAMESPACE