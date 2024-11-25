#ifndef WOT_NAMESPACE_BUFFER_TREE_H
#define WOT_NAMESPACE_BUFFER_TREE_H

#include "wot_index.h"

namespace WOT_NAMESPACE {

#ifdef INCLUDE_BUFFERTREE
class BufferTree : public BplusTree {
 public:
  enum class BtState : uint8_t {
    kBtOverflow,
    kBtUnderflow,
    kBtNOrmal,
  };

  // General constructor for WOT and LSMT
  BufferTree(Options options, const std::string& dbname,
            std::atomic<uint64_t>& flush_id, bool is_lsmt=false, bool is_buffer_tree=false);

  // No copying allowed
  BufferTree(const BufferTree&) = delete;
  void operator=(const BufferTree&) = delete;
  
  ~BufferTree() {}

  // Buffer tree does not change its state as it adapts to read automatically
  // TODO: when there is mixed read and write, does read still adapt or not?
  void AdaptToRead() override {
    mutex_.AssertHeld();
    ((BufferTreeState*) btree_state_)->SetValidation(true);
    return;
  }
  
  void StopAdaptToRead() override {}

 protected:
  void CompactMemTable() override;
  Status MakeRoomForWrite(bool force) override;
  void MaybeScheduleCompaction() override;
  void BackgroundCall() override;
  void UpdateRoot(SlicePageMap result) override;
  void AdaptMem() override;
  void AdaptImm() override;
  void AdaptWOT() override;

  bool AllChildrenLeaf(uint32_t pg_id) override;

  void MergeWithAllChildrenLeaf(
    uint32_t pg_id,
    int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) override;

  int GetLockedRoot() override { return root_pg_id_; }

  bool NodeTopLevelOverflows(Node* node) override {
    if (node->GetNodeLSMTFileSizeInLevel(0) > block_num_ * leveldb_options_.max_file_size) {
      return true;
    }
    return false;
  }

  void UpdateOrRewritePivots(
    uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) override;

  int EqualSplitInternalPage(
    Page p, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots) override;

  Page TreeSplitInternalPage(
    uint32_t pg_id,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots,
    std::vector<uint32_t>* new_pages,
    std::vector<Slice>* guards,
    std::vector<std::shared_ptr<FileMetaData>>* out_file_names) override;

  void AddImmCompactionWork() override {
    mutex_.AssertHeld();
    if (pushdown_imm_scheduled.load()) {
      return;
    }
    pushdown_imm_scheduled.store(true, std::memory_order_release);
    // Buffer tree has only BGCthread
    scheduled_compaction_num_.fetch_add(1);
    env_->Schedule(&BplusTree::BGAdaptImm, this);
  }

  void AddMemCompactionWork() override {
    mutex_.AssertHeld();
    if (has_imm_ || pushdown_mem_scheduled.load()) {
      return;
    }
    pushdown_mem_scheduled.store(true, std::memory_order_release);
    // Buffer tree has only BGCthread
    scheduled_compaction_num_.fetch_add(1);
    env_->Schedule(&BplusTree::BGAdaptMem, this);
  }

  void AddWOTCompactionWork() override {
    mutex_.AssertHeld();
    if (pushdown_mem_scheduled.load() || pushdown_imm_scheduled.load()) {
      return;
    }
    // Buffer tree has only BGCthread
    scheduled_compaction_num_.fetch_add(1);
    env_->Schedule(&BplusTree::BGAdaptWOT, this);
  }

  PageSplitPolicy* GetPageSplitPolicy(size_t key_size, size_t val_size) override {
    if (page_split_policy_ == nullptr) {
      page_split_policy_ = new DefaultSplitPolicy(key_size, val_size);
    }
    return page_split_policy_;
  }

  // Buffer tree empties the entire level each time
  DefaultAdapt* GetAdaptStrategy() override {
    if (adapt_strategy_ == nullptr) {
      adapt_strategy_ = new DefaultAdapt(this);
      fprintf(stdout, "Buffer tree: default strategy flushing entire level\n");
    }
    return adapt_strategy_;
  }

 private:
  bool FinishUsingImm();
  void BufferTreeConstructRoot(Iterator* iter);
  void AddFileToRoot(FileMetaData meta);
  void AddPageIterators(uint32_t pg_id, std::vector<Iterator*>* iters);
  void BufferTreeUpdatePivots(
    uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots);

  int block_num_;

};

#endif

} // namespace WOT_NAMESPACE

#endif // WOT_NAMESPACE_BUFFER_TREE_H