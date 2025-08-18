#include "wot_btree_refactor_.h"
#include "leveldb/util/mutexlock.h"
#include "leveldb/write_batch_internal.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"
#include "util/coding.h"
#include <stack>

namespace WOT_NAMESPACE {

using leveldb::MutexLock;
using leveldb::WriteBatchInternal;
using leveldb::NewBloomFilterPolicy;
using leveldb::NewLRUCache;
using leveldb::ParsedInternalKey;
using leveldb::AppendInternalKey;
using leveldb::kTypeValue;

BplusTree::BplusTree(Options options, const std::string& dbname,
                     std::atomic<uint64_t>& flush_id,
                     bool is_lsmt, bool is_buffer_tree)
    : mem_(nullptr),
      imm_(nullptr),
      shutting_down_(false),
      background_work_finished_signal_(&mutex_),
      background_compaction_scheduled_(false),
      background_flush_scheduled_(false),
      has_imm_(false),
      write_active_(false),
      pushdown_active_(false),
      compact_active_(false),
      write_wait_(0),
      bg_working_(false),
      write_finished_signal_(&mutex_),
      tmp_batch_(new WriteBatch),
      internal_comparator_(InternalKeyComparator(BytewiseComparator())),
      internal_filter_policy_(InternalFilterPolicy(NewBloomFilterPolicy(10))),
      leveldb_options_(SanitizeOptions(options, &internal_comparator_, &internal_filter_policy_)),
      config_(ConfigUtils::LoadFromOptions(options)),
      root_lsmt_path_(dbname),
      tree_height_(1),
      btree_state_(new BplusTreeWriteOptState()),
      max_node_id_(1),
      flush_id_(flush_id),
      env_(Env::Default()),
      table_cache_(new TableCache(root_lsmt_path_, leveldb_options_,
                                 ConfigUtils::TableCacheSize(leveldb_options_))),
      root_(is_buffer_tree ? new Node(this, true, config_.node_lsmt_level_limit,
                                    env_, root_lsmt_path_, flush_id_,
                                    &leveldb_options_, internal_comparator_, table_cache_,
                                    LSMTStatus::kRootBuffer, config_.leaf_limit, true) :
                           new Node(this, true, flush_id_, internal_comparator_)),
      leveldb_lsmt_(is_buffer_tree ? nullptr :
                    new LevelDBLSMT(env_, root_lsmt_path_, flush_id_, &leveldb_options_,
                                  internal_comparator_, table_cache_, config_.lsmt_level_limit,
                                  LSMTStatus::kStandalone, config_.leaf_limit)),
      buffer_manager_(NewBufferManager(config_.buffer_manager_num, dbname + "/index")),
      lock_manager_(new TreeLockManager(config_.buffer_manager_num)),
      root_pg_id_(0),
      seed_(0),
      seq_no_(1),
      mem_empty_(true),
      mem_cold_only_(true),
      imm_cold_only_(true) {
    
    // Initialize adaptation components
    background_work_manager_ = new BackgroundWorkManager(this);
    hotspot_manager_ = new HotspotManager(this);
    tree_adaptation_ops_ = new TreeAdaptationOperations(this);
    write_batch_processor_ = new WriteBatchProcessor(this);
    
    mutex_.Lock();
    mem_ = new MemTable(internal_comparator_);
    mem_->Ref();
    root_->pg_id_ = root_pg_id_;
    Page root_pg = buffer_manager_->Pin(root_pg_id_);
    ((TreePageHeader) root_pg)->is_leaf_ = true;
    ((TreePageHeader) root_pg)->is_dirty_ = true;
    WriteTbb a;
    node_table_.insert(a, root_pg_id_);
    a->second = root_;
    a.release();
    mutex_.Unlock();
}

BplusTree::~BplusTree() {
    delete background_work_manager_;
    delete hotspot_manager_;
    delete tree_adaptation_ops_;
    delete write_batch_processor_;
    delete adapt_strategy_;
    delete page_split_policy_;
    delete btree_state_;
    delete tmp_batch_;
    delete table_cache_;
    delete buffer_manager_;
    delete lock_manager_;
    if (leveldb_lsmt_) delete leveldb_lsmt_;
    DeleteNode(root_pg_id_);
}

void BplusTree::DeleteNode(uint32_t page_id) {
    ReadTbb a;
    if (!node_table_.find(a, page_id)) {
        return;
    }
    Node* node = a->second;
    a.release();
    std::vector<uint32_t> child_ids;
    PageUtils::GetChildPageIds(buffer_manager_, page_id, &child_ids);
    for (auto it = child_ids.begin(); it != child_ids.end(); it++) {
        DeleteNode(*it);
    }
    delete node;
}

Options BplusTree::SanitizeOptions(const Options& src, const InternalKeyComparator* icmp,
                                  const InternalFilterPolicy* ipolicy) {
    return ConfigUtils::SanitizeOptions(src, icmp, ipolicy);
}

size_t BplusTree::MemoryUsage() {
    return MemoryUtils::CalculateMemoryUsage(this);
}

void BplusTree::Print() {
    int h = tree_height_.load();
    std::cout << "Tree height is " << h << std::endl;
    if (leveldb_lsmt_ != nullptr) {
        std::cout << "Top LSMT\n";
        leveldb_lsmt_->Print();
    }
    for (int i = 1; i <= h; i++) {
        std::cout << " Tree-" << i << std::endl;
        root_->NodePrint(i);
    }
    std::cout << "Node table: " << node_table_.size() << std::endl;
}

void BplusTree::PrintStat() {
    if (mem_ != nullptr) {
        fprintf(stdout, "Memtable not null\n");
    }
    if (imm_ != nullptr) {
        fprintf(stdout, "Imm not null\n");
    }
    if (leveldb_lsmt_ != nullptr) {
        std::cout << "Top LSMT\n";
        leveldb_lsmt_->PrintStat();
    } 
    int h = tree_height_.load();
    std::cout << "Tree height is " << h << std::endl;
    if (root_->GetNodeLSMT() != nullptr) {
        std::cout << "Buffer Tree Root Node\n";
    }
    for (int i = 1; i < h; i++) {
        std::cout << " Tree-" << i << std::endl;
        root_->NodePrintStat(i);
    }
    std::cout << "Node table: " << node_table_.size() << std::endl;
}

Status BplusTree::Insert(const std::string& key, const std::string& value) {
    std::string ikey;
    AppendInternalKey(&ikey, ParsedInternalKey(key, seq_no_.fetch_add(1), kTypeValue));
    ReadTbb a;
    node_table_.find(a, root_pg_id_);
    Node* root = a->second;
    a.release();
    assert(!PageUtils::IsLeafPage(buffer_manager_, root->pg_id_));
    TreeInsert(std::make_pair(ikey, value));
    return Status::OK();
}

Status BplusTree::Insert(WriteBatch* updates) {
    return write_batch_processor_->Insert(updates);
}

Status BplusTree::Delete(const std::string& key) {
    return Status::NotSupported("Delete is not supported currently\n");
}

Status BplusTree::Query(const std::string& key, std::string* value) {
    return Status::NotSupported("Operation not supported");
}

SortedTreeIterator* BplusTree::NewSortedTreeIterator() {
    auto iter = new SortedTreeIterator(this, seed_.fetch_add(1),
                                      GetUserComparator(),
                                      internal_comparator_);
    return iter;
}

Iterator* BplusTree::NewPageIterator() {
    return new PageIterator(this);
}

void BplusTree::RecordReadSample(const Slice& key) {
    hotspot_manager_->RecordReadSample(key);
}

int BplusTree::EqualSplitInternalPage(Page page,
                                     std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice,uint32_t>>* old_pivots) {
    return PageSplitOperations::EqualSplitInternalPage(this, page, new_pivots, old_pivots);
}

void BplusTree::UpdateOrRewritePivots(uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    PageSplitOperations::UpdateOrRewritePivots(this, pg_id, new_pivots, old_pivots);
}

Page BplusTree::TreeSplitInternalPage(uint32_t pg_id,
                                     std::vector<SlicePageMap>* new_pivots,
                                     std::vector<std::pair<Slice,uint32_t>>* old_pivots,
                                     std::vector<uint32_t>* new_pages,
                                     std::vector<Slice>* guards,
                                     std::vector<std::shared_ptr<FileMetaData>>* out_file_names) {
    return PageSplitOperations::TreeSplitInternalPage(this, pg_id, new_pivots, old_pivots,
                                                     new_pages, guards, out_file_names);
}

Page BplusTree::DistributePivots(uint32_t pg_id, SlicePageMap* temp_pivots,
                                std::vector<uint32_t>* new_pages, std::vector<Slice>* guards) {
    return PageSplitOperations::DistributePivots(this, pg_id, temp_pivots, new_pages, guards);
}

void BplusTree::TreeInsert(std::pair<std::string, std::string> item) {
    // Implementation would handle tree insertion
    // This is a complex operation involving search, split, and balance
    
    std::stack<uint32_t> insert_stack;
    Slice key(item.first);
    Slice value(item.second);
    
    Status s = SearchDown(&insert_stack, key, value);
    if (!s.ok()) return;
    
    s = HandleOverflow(&insert_stack, key, value);
}

bool BplusTree::SafeNode(Page page, const Slice& key, const Slice& value) {
    // Check if the node has enough space for the new entry
    size_t entry_size = MemoryUtils::CalculateEntrySize(key);
    size_t free_space = PageUtils::PageGetFreeSpace(page);
    return free_space >= entry_size;
}

Status BplusTree::SearchDown(std::stack<uint32_t>* insert_stack,
                            const Slice& key, const Slice& value) {
    // Implementation would search down the tree to find insertion point
    uint32_t current_page = root_pg_id_;
    
    while (!PageUtils::IsLeafPage(buffer_manager_, current_page)) {
        insert_stack->push(current_page);
        lock_manager_->ReadLock(current_page);
        
        // Find child based on key comparison
        // This is simplified - real implementation would use page format
        current_page = 1; // Placeholder
        break;
    }
    
    insert_stack->push(current_page);
    return Status::OK();
}

void BplusTree::MayUpdateMinKey(Page cur_page, const Slice& key) {
    PageUtils::PageUpdateMinPivot(cur_page, key);
}

Status BplusTree::TryInsertLeaf(uint32_t pg_id, const Slice& key, const Slice& val) {
    Page page = buffer_manager_->Pin(pg_id);
    if (SafeNode(page, key, val)) {
        PageUtils::PageInsertIndexEntry(page, key, 0); // 0 for leaf entry
        buffer_manager_->Unpin(pg_id);
        return Status::OK();
    }
    buffer_manager_->Unpin(pg_id);
    return Status::IOError("Node full");
}

void BplusTree::UnlockParents(std::stack<uint32_t>* stack) {
    while (!stack->empty()) {
        uint32_t page_id = stack->top();
        stack->pop();
        lock_manager_->ReadUnlock(page_id);
    }
}

void BplusTree::InsertAndSplitLeafPages(uint32_t pg_id, const Slice& key,
                                       const Slice& val, std::vector<uint32_t>* new_pg_ids,
                                       std::vector<Slice>* new_pivots,
                                       InternalKeyComparator icmp) {
    // Implementation would split leaf pages
    // This is a complex operation that would need full page format knowledge
}

Status BplusTree::HandleOverflow(std::stack<uint32_t>* insert_stack,
                                const Slice& key, const Slice& val) {
    // Implementation would handle node overflow by splitting
    if (insert_stack->empty()) return Status::OK();
    
    uint32_t leaf_page = insert_stack->top();
    Status s = TryInsertLeaf(leaf_page, key, val);
    
    if (s.ok()) {
        UnlockParents(insert_stack);
        return s;
    }
    
    // Handle split if needed
    // This would involve complex split logic
    
    return Status::OK();
}

Status BplusTree::TryInsertInternal(uint32_t pg_id, std::vector<Slice>* new_pivots,
                                   std::vector<uint32_t>* new_pg_ids, uint32_t old) {
    // Implementation would insert into internal node
    return Status::OK();
}

void BplusTree::BuildSiblingConn(uint32_t left, uint32_t right) {
    // Implementation would build sibling connections
    Page left_page = buffer_manager_->Pin(left);
    Page right_page = buffer_manager_->Pin(right);
    
    ((TreePageHeader)left_page)->right_ = right;
    ((TreePageHeader)right_page)->left_ = left;
    
    buffer_manager_->Unpin(left);
    buffer_manager_->Unpin(right);
}

void BplusTree::InsertAndSplitInternalPages(uint32_t pg_id, uint32_t old,
                                           std::vector<uint32_t>* new_pg_ids,
                                           std::vector<Slice>* new_pivots) {
    // Implementation would split internal pages
}

void BplusTree::AdaptToRead() {
    tree_adaptation_ops_->AdaptToRead();
}

void BplusTree::StopAdaptToRead() {
    tree_adaptation_ops_->StopAdaptToRead();
}

void BplusTree::SetHotspotRange(const std::string& low, const std::string& up) {
    hotspot_manager_->SetHotspotRange(low, up);
}

void BplusTree::AddMemCompactionWork() {
    background_work_manager_->ScheduleMemCompaction();
}

void BplusTree::AddImmCompactionWork() {
    background_work_manager_->ScheduleImmCompaction();
}

void BplusTree::AddLSMTFlushWork() {
    background_work_manager_->ScheduleLSMTFlush();
}

void BplusTree::FlushLSMT() {
    MutexLock l(&mutex_);
    if (shutting_down_.load()) {
        return;
    }
    bg_working_.store(true, std::memory_order_release);
    {
        mutex_.Unlock();
        tree_adaptation_ops_->InstallLSMTFilesToBol();
        mutex_.Lock();
    }
    bg_working_.store(false, std::memory_order_release);
    background_flush_scheduled_.store(false, std::memory_order_release);
    write_finished_signal_.SignalAll();
}

void BplusTree::AddLSMTCompactionWork() {
    background_work_manager_->ScheduleLSMTCompaction();
}

void BplusTree::AdaptLSMT() {
    MutexLock l(&mutex_);
    if (shutting_down_.load()) {
        background_work_manager_->DecrementFlushCount();
        background_work_manager_->SetLSMTScheduled(false);
        return;
    }
    bg_working_.store(true, std::memory_order_release);
    {
        mutex_.Unlock();
        GetAdaptStrategy()->AdaptLSMT();
        mutex_.Lock();
    }
    background_work_manager_->SetLSMTScheduled(false);
    bg_working_.store(false, std::memory_order_release);
    background_work_manager_->DecrementFlushCount();
    write_finished_signal_.SignalAll();
}

void BplusTree::AddWOTCompactionWork() {
    background_work_manager_->ScheduleWOTCompaction();
}

bool BplusTree::BGFlushIdle() {
    return background_work_manager_->BGFlushIdle();
}

bool BplusTree::BGCompactionIdle() {
    return background_work_manager_->BGCompactionIdle();
}

const std::string& BplusTree::GetLowHotKey() const {
    return hotspot_manager_->GetLowHotKey();
}

const std::string& BplusTree::GetHighHotKey() const {
    return hotspot_manager_->GetHighHotKey();
}

bool BplusTree::RangeWithinHotspot(const Slice& low, const Slice& up) const {
    return hotspot_manager_->RangeWithinHotspot(low, up);
}

DefaultAdapt* BplusTree::GetAdaptStrategy() {
    if (adapt_strategy_ == nullptr) {
        switch (config_.adapt_strategy) {
            case 2:
                adapt_strategy_ = new PrioritySizeLimitAdapt(this);
                break;
            case 3:
                adapt_strategy_ = new PriorityNoDupAdapt(this);
                break;
            case 4:
                adapt_strategy_ = new HotOnlyAdapt(this);
                break;
            case 5:
                adapt_strategy_ = new BalancedAdapt(this);
                break;
            default:
                adapt_strategy_ = new DefaultAdapt(this);
                break;
        }
    }
    return adapt_strategy_;
}

WriteBatch* BplusTree::BuildBatchGroup(Writer** last_writer) {
    return write_batch_processor_->BuildBatchGroup(last_writer);
}

void BplusTree::BGWork(void* db) {
    reinterpret_cast<BplusTree*>(db)->BackgroundCall();
}

// ConcreteBplusTree implementation
ConcreteBplusTree::ConcreteBplusTree(Options options, const std::string& dbname,
                                    std::atomic<uint64_t>& flush_id, bool is_lsmt,
                                    bool is_buffer_tree)
    : BplusTree(options, dbname, flush_id, is_lsmt, is_buffer_tree) {
}

ConcreteBplusTree::~ConcreteBplusTree() {
}

int ConcreteBplusTree::GetLockedRoot() {
    return root_pg_id_ == 0 ? -1 : static_cast<int>(root_pg_id_);
}

bool ConcreteBplusTree::NodeTopLevelOverflows(Node* node) {
    return BufferOperations::TopLevelOverflows(node);
}

bool ConcreteBplusTree::AllChildrenLeaf(uint32_t pg_id) {
    Page page = buffer_manager_->Pin(pg_id);
    if (!page) return false;
    
    bool all_leaf = true;
    uint32_t item_num = PageUtils::PageGetItemCount(page);
    
    for (int i = 0; i < item_num && all_leaf; i++) {
        uint32_t child_id = PageUtils::PageReadChildAtOffset(page, i);
        if (!PageUtils::IsLeafPage(buffer_manager_, child_id)) {
            all_leaf = false;
        }
    }
    
    buffer_manager_->Unpin(pg_id);
    return all_leaf;
}

void ConcreteBplusTree::MergeWithAllChildrenLeaf(uint32_t pg_id, int level_of_files,
                                                std::vector<std::shared_ptr<FileMetaData>>* files,
                                                std::vector<SlicePageMap>* new_pivots,
                                                std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
    // Implementation would merge with all children when they are leaves
    // This is a complex operation specific to the tree structure
}

PageSplitPolicy* ConcreteBplusTree::GetPageSplitPolicy(size_t current_size, size_t target_size) {
    if (page_split_policy_ == nullptr) {
        page_split_policy_ = PageSplitPolicyFactory::CreatePolicy(config_.first_page_split_policy).release();
    }
    return page_split_policy_;
}

void ConcreteBplusTree::AdaptMem() {
    // Implementation for memory adaptation
    GetAdaptStrategy()->AdaptMem();
}

void ConcreteBplusTree::AdaptImm() {
    // Implementation for immutable memory adaptation
    GetAdaptStrategy()->AdaptImm();
}

void ConcreteBplusTree::AdaptWOT() {
    // Implementation for WOT adaptation
    GetAdaptStrategy()->AdaptWOT();
}

void ConcreteBplusTree::UpdateRoot(SlicePageMap result) {
    if (result.empty()) return;
    
    // Create new root if split resulted in multiple pages
    if (result.size() > 1) {
        uint32_t new_root_id = buffer_manager_->Allocate();
        Page new_root = buffer_manager_->Pin(new_root_id);
        PageUtils::PageInit(new_root, new_root_id);
        
        for (auto const& entry : result) {
            PageUtils::PageInsertIndexEntry(new_root, entry.first, entry.second);
        }
        
        // Update root reference
        Page old_root = buffer_manager_->Lookup(root_pg_id_);
        memcpy(old_root, new_root, PAGE_SIZE);
        ((TreePageHeader)old_root)->page_id_ = root_pg_id_;
        
        buffer_manager_->Delete(new_root_id);
        tree_height_.fetch_add(1);
    }
}

void ConcreteBplusTree::BackgroundCall() {
    MutexLock l(&mutex_);
    if (shutting_down_.load()) {
        return;
    }
    
    // Perform background compaction work
    // This would involve checking various conditions and scheduling appropriate work
    
    background_compaction_scheduled_ = false;
    background_work_finished_signal_.SignalAll();
}

void ConcreteBplusTree::CompactMemTable() {
    mutex_.AssertHeld();
    
    if (imm_ == nullptr) return;
    
    // Compact immutable memtable
    // This would involve writing memtable contents to LSMT or tree
    
    imm_->Unref();
    imm_ = nullptr;
    has_imm_.store(false);
}

Status ConcreteBplusTree::MakeRoomForWrite(bool force) {
    return write_batch_processor_->MakeRoomForWrite(force);
}

void ConcreteBplusTree::MaybeScheduleCompaction() {
    mutex_.AssertHeld();
    if (background_compaction_scheduled_) {
        // Already scheduled
    } else if (shutting_down_.load()) {
        // DB is being deleted; no more background compactions
    } else {
        background_compaction_scheduled_ = true;
        env_->Schedule(&BplusTree::BGWork, this);
    }
}

} // namespace WOT_NAMESPACE