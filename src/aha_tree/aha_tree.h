#ifndef WOT_NAMESPACE_AHA_TREE_H
#define WOT_NAMESPACE_AHA_TREE_H

#include "../wot_index.h"

namespace WOT_NAMESPACE {


class AhaTree : public BplusTree {
 public:
  enum class BtState : uint8_t {
    kBtOverflow,
    kBtUnderflow,
    kBtNOrmal,
  };

  // General constructor for WOT and LSMT
  AhaTree(Options options, const std::string& dbname,
            std::atomic<uint64_t>& flush_id, bool is_lsmt=false, bool is_buffer_tree=false);

  // No copying allowed
  AhaTree(const AhaTree&) = delete;
  void operator=(const AhaTree&) = delete;
  
  ~AhaTree() {
    fprintf(stdout, "Prepare to shutdown\n");
    mutex_.Lock();
    fprintf(stdout, "Shutting down...\n");
    shutting_down_.store(true, std::memory_order_release);
    while (background_compaction_scheduled_) {
      background_work_finished_signal_.Wait();
    }
    fprintf(stdout, "BG compaction thread done\n");
    fflush(stdout);
    // assert(read_counter_.load() == 0);
    while (bg_working_.load()) {
      write_finished_signal_.Wait();
    }
    fprintf(stdout, "BG flushing thread done\n");
    fflush(stdout);
    mutex_.Unlock();
    if (mem_ != nullptr) mem_->Unref();
    if (imm_ != nullptr) imm_->Unref();
    delete tmp_batch_;
    if (leveldb_lsmt_ != nullptr) delete leveldb_lsmt_;
    DeleteNode(root_pg_id_);

    delete lock_manager_;
    buffer_manager_->Unpin(root_pg_id_);
    node_table_.clear();
    if (adapt_strategy_ != nullptr) {
      delete adapt_strategy_;
    }

    delete buffer_manager_;
    if (leveldb_options_.block_cache != nullptr) {
      delete leveldb_options_.block_cache;
    }

    delete table_cache_;
  }

 protected:
  void CompactMemTable() override;
  Status MakeRoomForWrite(bool force) override;
  void MaybeScheduleCompaction() override;
  void BackgroundCall() override;
  void UpdateRoot(SlicePageMap result) override;
  void AdaptMem() override;
  void AdaptImm() override;
  void AdaptWOT() override;

  // Only used in buffer tree's buffer emptying process
  bool AllChildrenLeaf(uint32_t pg_id) override {
    return false;
  }

  void MergeWithAllChildrenLeaf(
    uint32_t pg_id,
    int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) override {}

  int GetLockedRoot() override { return -1; }

  bool NodeTopLevelOverflows(Node* node) override {
    return node->BufferTopLevelOverflows();
  }

  PageSplitPolicy* GetPageSplitPolicy(size_t key_size, size_t val_size) override {
    if (page_split_policy_ == nullptr) {
      switch (page_split_policy_choice_) {
      case 1:
        page_split_policy_ = new EvenSplitPolicy(key_size, val_size);
        break;
      case 2:
        page_split_policy_ = new SoundRemedySplitPolicy(key_size, val_size);
        break;
      default:
        break;
      }
    }
    return page_split_policy_;
  }

 private:
  void BackgroundCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

};

 
} // namespace WOT_NAMESPACE

#endif // WOT_NAMESPACE_AHA_TREE_H  