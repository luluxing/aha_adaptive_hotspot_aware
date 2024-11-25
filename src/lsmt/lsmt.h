#ifndef LSMT_WOT_NAMESPACE_H_
#define LSMT_WOT_NAMESPACE_H_

#include <atomic>
#include <memory>
#include <vector>
#include <set>

#include "leveldb/builder.h"
#include "leveldb/include/options.h"

#include "util/state.h"
#include "wot_buf_mgr/tree_page.h"
#include "wot_lock_mgr/lock_manager.h"

namespace WOT_NAMESPACE {

class LevelDBCompaction;
class TableCache;

// this is mostly adopted from LevelDB version_set
class LevelDBLSMT {
 public:
  struct GetStats {
    FileMetaData* seek_file;
    int seek_file_level;
  };

  // dbname is a combination of path and LSMT prefix
  LevelDBLSMT(Env*, const std::string& dbname, std::atomic<uint64_t>& flush_id_,
              const Options*, const InternalKeyComparator&, TableCache*,
              int level_limit, LSMTStatus status, int leaf_limit);
  
  LevelDBLSMT(LevelDBLSMT& lsmt);

  ~LevelDBLSMT();

  OpState AddFile(int level, FileMetaData f, const Page page);
  OpState AddFiles(int level, std::vector<std::shared_ptr<FileMetaData>>* f,
                  const Page page);
  OpState AppendFiles(int level, std::vector<std::shared_ptr<FileMetaData>>* f);
  int AppendFile(int level, FileMetaData f);

  void Finalize();
  void RemoveObsoleteFiles();
  void GetObsoleteFiles(std::set<uint64_t>& files);
  static void RemoveFiles(Env* env, const std::string& dbname,
                          const std::set<uint64_t>& files);

  int PickLevelForMemTableOutput(const Slice& smallest_user_key,
                                  const Slice& largest_user_key);

  Status CompactTree(std::vector<Slice>*, std::vector<std::shared_ptr<FileMetaData>>*);
  Status CompactTree(int*, std::vector<std::shared_ptr<FileMetaData>>*, Iterator* iter=nullptr);

  // Force at least min_flush_file files of bottom level to be compacted
  Status CompactBottomLevel(const Page page);
  void CompactFilesInRange(int level,
                          std::vector<std::shared_ptr<FileMetaData>>* files,
                          const Page page);
  bool NeedCompactBottomLevel();

  // Retrieve files that are in the bottom level. We share the compaction
  // pointer with the compaction process. If the compaction pointer is empty,
  // this level has not been compacted nor flushed. If the compaction pointer
  // is non-empty, this level has been flushed (cannot be compacted because
  // this is the bottom level).
  // Status RetrieveFilesForTreePushdown(std::vector<std::shared_ptr<FileMetaData>>* files,
  //                                     int flush_count);
  void FinalizeRetrieval(int level_of_files, std::vector<std::shared_ptr<FileMetaData>>* files);
  void FinalizeRetrievalAndDelete(int level_of_files,
                                  std::vector<std::shared_ptr<FileMetaData>>* files);

  void Clear();
  void ClearAll();

  void Print();
  void PrintStat();

  uint64_t TotalFileSizeLSMT();

  uint64_t FileSizeLimit();

  bool NeedsCompaction() {
    // leveldb also uses file_to_compact_ != nullptr
    return (compaction_score_ >= 1) || (file_to_compact_level_.load() > -1);
    // return compact_level_ > -1;
  }

  bool UpdateStats(const GetStats& stats);

  Iterator* NewMergedIterator(const ReadOptions& options);

  void AddIterators(const ReadOptions& options,
                    std::vector<Iterator*>* iters);

  void SetLevelLimit(int level) {
    assert(level > 0 && level < config::kNumLevels);
    level_limit_ = level;
  }

  int GetLevelLimit() { return level_limit_; }

  Slice GetOneUserKey() {
    for (int i = 0; i <= GetBottomLevel(); i++) {
      if (files_[i].size() > 0) {
        return files_[i][0]->largest.user_key();
      }
    }
    return Slice();
  }

  Slice GetSmallestUserKey() {
    auto level = GetBottomLevel();
    if (level < 0) return Slice();
    Slice smallest = Slice();
    // Iterate over all files in level-0
    for (auto f : files_[0]) {
      if (smallest.empty() || f->smallest.user_key().compare(smallest) < 0) {
        smallest = f->smallest.user_key();
      }
    }
    for (int i = 1; i <= GetBottomLevel(); i++) {
      if (files_[i].size() > 0) {
        if (smallest.empty() || files_[i][0]->smallest.user_key().compare(smallest) < 0) {
          smallest = files_[i][0]->smallest.user_key();
        }
      }
    }
    return smallest;
  }

  uint64_t TotalEntryNum() {
    uint64_t num = 0;
    int level = GetBottomLevel();
    if (level < 0) return 0;
    for (int i = 0; i <= level; i++) {
      for (auto f : files_[i]) {
        num += f->file_entry;
      }
    }
    return num;
  }

  int GetAllBottomLevelFiles(std::vector<std::shared_ptr<FileMetaData>>* fs) {
    int level = GetBottomLevel();
    if (level < 0) return level;
    for (int i = 0; i < files_[level].size(); i++) {
      fs->push_back(files_[level][i]);
    }
    return level;
  }

  int GetSomeBottomLevelFiles(std::vector<std::shared_ptr<FileMetaData>>* fs, int);

  const std::vector<std::shared_ptr<FileMetaData>>* GetBottomLevelFiles(int* my_level) {
    int level = GetBottomLevel();
    if (level < 0) return nullptr;
    if (my_level != nullptr) {
      *my_level = level;
    }
    return &files_[level];
  }

  void ClearBottomLevelFiles() {
    int level = GetBottomLevel();
    if (level < 0) return;
    auto files = &files_[level];
    files->clear();
  }

  OpState DoCompact(const Page page, port::Mutex* mu, TreeLockManager* lm, int lock_id);

  int NumLevelFiles(int level) const;

  // We distinguish these two LSMTs because we use different
  // level-0 compaction logic. Standalone LSMT uses the number
  // of files while node-associated one uses the size limit.
  void SetLSMTStatus(LSMTStatus status) { lsmt_status_ = status; }
  LSMTStatus GetLSMTStatus() { return lsmt_status_; }
  // Debugging
  // int compaction_level_count = 0;
  // int compaction_tree_count = 0;

  // Experiment
  // void SetLeafLimit(int leaf_limit) { leaf_limit_ = leaf_limit; }
  
  int GetBottomLevel() {
    int level = config::kNumLevels - 1;
    for (; level >= 0; level--) {
      if (files_[level].size() > 0) return level;
    }
    return -1;
  }

  uint64_t output_file_size;// = 1 * 1024 * 1024;

  int GetFilesInRange(std::vector<std::shared_ptr<FileMetaData>>* files,
                       std::string low, std::string up);

  void GetLevelFilesInRange(std::vector<std::shared_ptr<FileMetaData>>* files,
                       std::string low, std::string up, int level);
  void GetLevel0FilesInRange(std::vector<std::shared_ptr<FileMetaData>>* files,
                       std::string low, std::string up);
  OpState CompactTopLevel(const Page p);

  bool TopLevelOverflows() {
    return files_[0].size() >= config::kL0_CompactionTrigger;
  }

  int GetLSMTFileSizeInLevel(int level) {
    int size = 0;
    for (auto f : files_[level]) {
      size += f->file_size;
    }
    return size;
  }

  int GetLevel0FileNum() {
    if (files_->size() > 0) {
      return files_[0].size();
    }
    return -1;
  }

  void SetHotspot(const std::string& lk, const std::string& uk, bool hot) {
    std::string key = lk + uk;
    has_hotspot_[key] = hot;
  }

  bool HasHotspot(const std::string& lk, const std::string& uk) {
    // Create a concatenate string of lk and uk and use it as a key
    std::string key = lk + uk;
    if (has_hotspot_.find(key) == has_hotspot_.end()) {
      has_hotspot_[key] = true;
      return true;
    }
    return has_hotspot_[key];
  }

  bool RecordReadSample(Slice key);

 private:
  friend class LevelDBCompaction;
  struct CompactionState;

  class LevelFileNumIterator;

  struct BySmallestKey {
    const InternalKeyComparator* internal_comparator;

    bool operator()(std::shared_ptr<FileMetaData> f1, std::shared_ptr<FileMetaData> f2) const {
      int r = internal_comparator->Compare(f1->smallest, f2->smallest);
      if (r != 0) {
        return (r < 0);
      } else {
        // Break ties by file number
        return (f1->number < f2->number);
      }
    }
  };

  typedef std::set<std::shared_ptr<FileMetaData>, BySmallestKey> FileSet;

  Iterator* NewConcatenatingIterator(const ReadOptions&, int level) const;

  OpState MayNeedCompaction(int level);

  void ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                          bool (*func)(void*, int, FileMetaData*));

  // bool SomeLevelMayNeedCompaction();

  bool OverlapInLevel(int level, const Slice* smallest_user_key,
                             const Slice* largest_user_key);

  OpState CompactLevel(int level, const Page page);
  OpState CompactLevelImpl(LevelDBCompaction* c, int level,
                           const Page page, int target_level);

  void SetCompactionInputs(int, LevelDBCompaction*);
  void SetCompactionInputsBottomLevel(LevelDBCompaction*);

  void TrivialMove(LevelDBCompaction* c, int level);
  
  void GetRange(const std::vector<std::shared_ptr<FileMetaData>>& inputs,
                InternalKey* smallest, InternalKey* largest);
  void GetRange2(const std::vector<std::shared_ptr<FileMetaData>>& inputs1,
                const std::vector<std::shared_ptr<FileMetaData>>& inputs2,
                InternalKey* smallest, InternalKey* largest);

  void GetOverlappingInputs(int level, const InternalKey* begin,
                            const InternalKey* end,
                            std::vector<std::shared_ptr<FileMetaData>>* inputs);

  Status DoCompactionWork(CompactionState* c, const Page page, int target_level);

  Status DoCompactionWorkImpl(CompactionState* c, Iterator* iter,
                              const Page page);

  Status DoCompactionTreeWorkImpl(CompactionState* c, Iterator* iter,
                                  std::vector<Slice>* guards,
                                  bool stop_early=false);

  Iterator* MakeInputIterator(LevelDBCompaction* c);

  void SetupOtherInputs(LevelDBCompaction* c);

  Status FinishCompactionOutputFile(CompactionState* compact,
                                    Iterator* input);
  
  Status OpenCompactionOutputFile(CompactionState* compact);

  Status InstallCompactionResults(CompactionState* compact, int level=-1);
  Status InstallCompactTreeResults(CompactionState* compact,
                        std::vector<std::shared_ptr<FileMetaData>>*);

  void AddDeletedFiles(CompactionState* compact,
                       std::vector<std::set<uint64_t>>* deleted);
  void AddDeletedFilesAllLevels(CompactionState* compact);
  Status ApplyFileChanges(int level, FileSet* added, std::set<uint64_t> deleted);
  void MaybeAddFile(int level, std::shared_ptr<FileMetaData> f,
                    std::vector<std::shared_ptr<FileMetaData>>*,
                    std::set<uint64_t> deletes);
  void CleanupCompaction(CompactionState* compact);

  void DeleteFiles(std::set<uint64_t> fs);

  const Comparator* user_comparator() const {
    return icmp_.user_comparator();
  }

  int level_limit_ = -1; // A soft limit on the number of levels in root lsmt
  Env* const env_;
  const std::string dbname_;
  const Options* const options_;
  const InternalKeyComparator& icmp_;
  TableCache* const table_cache_;
  std::atomic<uint64_t>& file_id_;
  
  std::vector<std::shared_ptr<FileMetaData>> files_[config::kNumLevels];
  std::string compact_pointer_[config::kNumLevels];

  // Use an array to store the flush pointers for each level to 
  // avoid the confusion when the bottom level has been flushed
  std::string flush_pointer_[config::kNumLevels];

  LSMTStatus lsmt_status_;

  int leaf_limit_;

  int compact_level_ = -1;
  double compaction_score_ = -1;

  // Next file to compact based on seek stats.
  std::atomic<uint64_t> file_num_to_compact_;
  std::atomic<int> file_to_compact_level_ = -1;

  std::set<uint64_t> obsolete_files_;

  // bool has_hotspot_ = true;
  std::map<std::string, bool> has_hotspot_;
};

// this is mostly adopted from LevelDB Compaction
class LevelDBCompaction {
 public:
  ~LevelDBCompaction() {
    inputs_[0].clear();
    inputs_[1].clear();
    grandparents_.clear();
  }

  int level() const { return level_; }

  // "which" must be either 0 or 1
  int num_input_files(int which) const { return inputs_[which].size(); }

  // Return the ith input file at "level()+which" ("which" must be 0 or 1).
  std::shared_ptr<FileMetaData> input(int which, int i) const { return inputs_[which][i]; }

  // Maximum size of files to build during this compaction.
  // we move this constant to CompactionState
  // uint64_t MaxOutputFileSize() const { return max_output_file_size_; }

  // Is this a trivial compaction that can be implemented by just
  // moving a single input file to the next level (no merging or splitting)
  bool IsTrivialMove(int level_limit) const;

  // Add all inputs to this compaction as delete operations to *edit.
  // void AddInputDeletions();

  // Returns true if the information we have available guarantees that
  // the compaction is producing data in "level+1" for which no data exists
  // in levels greater than "level+1".
  bool IsBaseLevelForKey(const Slice& user_key, int level_lim);

  // Returns true iff we should stop building the current output
  // before processing "internal_key".
  bool ShouldStopBefore(const Slice& internal_key);

  // we move this constant to CompactionState
  // static const uint64_t max_output_file_size_ = 3 * 1024 * 1024;

  // Include at least this number of files in the input level
  static const uint8_t min_compact_file = 8;

  static const uint8_t min_flush_file = 10;

 private:
  friend class LevelDBLSMT;

  LevelDBCompaction(int level);

  int level_;
  
  LevelDBLSMT* input_version_;

  // Each compaction reads inputs from "level_" and "level_+1"
  // make it larger for CompactWithOneChild
  std::vector<std::shared_ptr<FileMetaData>> inputs_[2];  // The two sets of inputs

  // State used to check for number of overlapping grandparent files
  // (parent == level_ + 1, grandparent == level_ + 2)
  std::vector<std::shared_ptr<FileMetaData>> grandparents_;
  size_t grandparent_index_;  // Index in grandparent_starts_
  bool seen_key_;             // Some output key has been seen
  int64_t overlapped_bytes_;  // Bytes of overlap between current output
                              // and grandparent files

  // State for implementing IsBaseLevelForKey

  // level_ptrs_ holds indices into input_version_->levels_: our state
  // is that we are positioned at one of the file ranges for each
  // higher level than the ones involved in this compaction (i.e. for
  // all L >= level_ + 2).
  size_t level_ptrs_[config::kNumLevels];
};

} // namespace WOT_NAMESPACE

#endif
