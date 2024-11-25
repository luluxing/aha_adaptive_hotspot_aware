#include <algorithm>
#include <cstdio>

#include "lsmt.h"
#include "leveldb/include/env.h"
#include "leveldb/include/iterator.h"
#include "leveldb/include/table_builder.h"
#include "leveldb/table/two_level_iterator.h"
#include "leveldb/table/merger.h"
#include "leveldb/table_cache.h"

namespace WOT_NAMESPACE {

static void CleanupIteratorState(void* arg1, void* arg2) {
  // IterState* state = reinterpret_cast<IterState*>(arg1);
  // state->mu->Lock();
  // state->mem->Unref();
  // if (state->imm != nullptr) state->imm->Unref();
  // state->version->Unref();
  // state->mu->Unlock();
  // delete state;
}

// filename.cc:78
bool ParseFileName(const std::string& filename, uint64_t* number) {
  uint64_t num;
  Slice rest(filename);
  if (!ConsumeDecimalNumber(&rest, &num)) {
    return false;
  }
  Slice suffix = rest;
  if (suffix == Slice(".log")) {
    // *type = kLogFile;
  } else if (suffix == Slice(".sst") || suffix == Slice(".ldb")) {
    // *type = kTableFile;
  } else if (suffix == Slice(".dbtmp")) {
    // *type = kTempFile;
  } else {
    return false;
  }
  *number = num;
  return true;
}

// version_set:210
static Iterator* GetFileIterator(void* arg, const ReadOptions& options,
                                const Slice& file_value) {
  TableCache* cache = reinterpret_cast<TableCache*>(arg);
  if (file_value.size() != 16) {
    fprintf(stderr, "FileReader invoked with unexpected value\n");
    std::abort();
    // return NewErrorIterator(
    //     Status::Corruption("FileReader invoked with unexpected value"));
  } else {
    return cache->NewIterator(options, DecodeFixed64(file_value.data()),
                              DecodeFixed64(file_value.data() + 8));
  }
}

// LevelDB::version_set.cc:59
static int64_t TotalFileSize(const std::vector<std::shared_ptr<FileMetaData>>& files) {
  int64_t sum = 0;
  for (size_t i = 0; i < files.size(); i++) {
    sum += files[i]->file_size;
  }
  return sum;
}

// LevelDB::version_set.cc:41
static double MaxBytesForLevel(int level) {
  // Note: the result for level zero is not really used since we set
  // the level-0 compaction threshold based on number of files.

  // Result for both level-0 and level-1. 1024*1024=1048576
  double result = 10. * 1048576.0;
  while (level > 1) {
    result *= 10;
    level--;
  }
  return result;
}

static size_t TargetFileSize(const Options* options) {
  return options->max_file_size;
}

// LevelDB::version_set.cc:30
static int64_t MaxGrandParentOverlapBytes(const Options* options) {
  return 10 * TargetFileSize(options);
}

// LevelDB::version_set.cc:1308
bool FindLargestKey(const InternalKeyComparator& icmp,
                    const std::vector<std::shared_ptr<FileMetaData>>& files,
                    InternalKey* largest_key) {
  if (files.empty()) {
    return false;
  }
  *largest_key = files[0]->largest;
  for (size_t i = 1; i < files.size(); ++i) {
    auto f = files[i];
    if (icmp.Compare(f->largest, *largest_key) > 0) {
      *largest_key = f->largest;
    }
  }
  return true;
}

// LevelDB::version_set.cc:1326
std::shared_ptr<FileMetaData> FindSmallestBoundaryFile(
    const InternalKeyComparator& icmp,
    const std::vector<std::shared_ptr<FileMetaData>>& level_files,
    const InternalKey& largest_key) {
  const Comparator* user_cmp = icmp.user_comparator();
  std::shared_ptr<FileMetaData> smallest_boundary_file(nullptr);
  for (size_t i = 0; i < level_files.size(); ++i) {
    std::shared_ptr<FileMetaData> f = level_files[i];
    if (icmp.Compare(f->smallest, largest_key) > 0 &&
        user_cmp->Compare(f->smallest.user_key(), largest_key.user_key()) ==
            0) {
      if (smallest_boundary_file == nullptr ||
          icmp.Compare(f->smallest, smallest_boundary_file->smallest) < 0) {
        smallest_boundary_file = f;
      }
    }
  }
  return smallest_boundary_file;
}

// LevelDB::version_set.cc:37
static int64_t ExpandedCompactionByteSizeLimit(const Options* options) {
  return 25 * TargetFileSize(options);
}

// LevelDB::version_set.cc:1360
void AddBoundaryInputs(const InternalKeyComparator& icmp,
                       const std::vector<std::shared_ptr<FileMetaData>>& level_files,
                       std::vector<std::shared_ptr<FileMetaData>>* compaction_files) {
  InternalKey largest_key;

  // Quick return if compaction_files is empty.
  if (!FindLargestKey(icmp, *compaction_files, &largest_key)) {
    return;
  }

  bool continue_searching = true;
  while (continue_searching) {
    std::shared_ptr<FileMetaData> smallest_boundary_file =
        FindSmallestBoundaryFile(icmp, level_files, largest_key);

    // If a boundary file was found advance largest_key, otherwise we're done.
    if (smallest_boundary_file != NULL) {
      compaction_files->push_back(smallest_boundary_file);
      largest_key = smallest_boundary_file->largest;
    } else {
      continue_searching = false;
    }
  }
}

// version_set.cc:87
int FindFile(const InternalKeyComparator& icmp,
             const std::vector<std::shared_ptr<FileMetaData>>& files, const Slice& key) {
  uint32_t left = 0;
  uint32_t right = files.size();
  while (left < right) {
    uint32_t mid = (left + right) / 2;
    auto f = files[mid];
    if (icmp.InternalKeyComparator::Compare(f->largest.Encode(), key) < 0) {
      // Key at "mid.largest" is < "target".  Therefore all
      // files at or before "mid" are uninteresting.
      left = mid + 1;
    } else {
      // Key at "mid.largest" is >= "target".  Therefore all files
      // after "mid" are uninteresting.
      right = mid;
    }
  }
  return right;
}

static bool AfterFile(const Comparator* ucmp, const Slice* user_key,
                      const std::shared_ptr<FileMetaData> f) {
  // null user_key occurs before all keys and is therefore never after *f
  return (user_key != nullptr &&
          ucmp->Compare(*user_key, f->largest.user_key()) > 0);
}

static bool BeforeFile(const Comparator* ucmp, const Slice* user_key,
                       const std::shared_ptr<FileMetaData> f) {
  // null user_key occurs after all keys and is therefore never before *f
  return (user_key != nullptr &&
          ucmp->Compare(*user_key, f->smallest.user_key()) < 0);
}

bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                           bool disjoint_sorted_files,
                           const std::vector<std::shared_ptr<FileMetaData>>& files,
                           const Slice* smallest_user_key,
                           const Slice* largest_user_key) {
  const Comparator* ucmp = icmp.user_comparator();
  if (!disjoint_sorted_files) {
    // Need to check against all files
    for (size_t i = 0; i < files.size(); i++) {
      const std::shared_ptr<FileMetaData> f = files[i];
      if (AfterFile(ucmp, smallest_user_key, f) ||
          BeforeFile(ucmp, largest_user_key, f)) {
        // No overlap
      } else {
        return true;  // Overlap
      }
    }
    return false;
  }

  // Binary search over file list
  uint32_t index = 0;
  if (smallest_user_key != nullptr) {
    // Find the earliest possible internal key for smallest_user_key
    InternalKey small_key(*smallest_user_key, kMaxSequenceNumber,
                          kValueTypeForSeek);
    index = FindFile(icmp, files, small_key.Encode());
  }

  if (index >= files.size()) {
    // beginning of range is after all files, so no overlap.
    return false;
  }

  return !BeforeFile(ucmp, largest_user_key, files[index]);
}

// version_set.cc:163
class LevelDBLSMT::LevelFileNumIterator : public Iterator {
 public:
  LevelFileNumIterator(const InternalKeyComparator& icmp,
                       const std::vector<std::shared_ptr<FileMetaData>>* flist)
      : icmp_(icmp), flist_(flist), index_(flist->size()) {  // Marks as invalid
  }
  bool Valid() const override { return index_ < flist_->size(); }
  void Seek(const Slice& target) override {
    index_ = FindFile(icmp_, *flist_, target);
  }
  void SeekToFirst() override { index_ = 0; }
  void SeekToLast() override {
    index_ = flist_->empty() ? 0 : flist_->size() - 1;
  }
  void Next() override {
    assert(Valid());
    index_++;
  }
  void Prev() override {
    assert(Valid());
    if (index_ == 0) {
      index_ = flist_->size();  // Marks as invalid
    } else {
      index_--;
    }
  }
  Slice key() const override {
    assert(Valid());
    return (*flist_)[index_]->largest.Encode();
  }
  Slice value() const override {
    assert(Valid());
    EncodeFixed64(value_buf_, (*flist_)[index_]->number);
    EncodeFixed64(value_buf_ + 8, (*flist_)[index_]->file_size);
    return Slice(value_buf_, sizeof(value_buf_));
  }
  Status status() const override { return Status::OK(); }

 private:
  const InternalKeyComparator icmp_;
  const std::vector<std::shared_ptr<FileMetaData>>* const flist_;
  uint32_t index_;

  // Backing store for value().  Holds the file number and size.
  mutable char value_buf_[16];
};

LevelDBLSMT::LevelDBLSMT(Env* env, const std::string& dbpath, std::atomic<uint64_t>& flush_id,
                        const Options* options, const InternalKeyComparator& cmp,
                        TableCache* table_cache, int level_limit, LSMTStatus status,
                        int leaf_limit)
: level_limit_(level_limit),
  env_(env),
  dbname_(dbpath),
  options_(options),
  icmp_(cmp),
  table_cache_(table_cache),
  file_id_(flush_id),
  lsmt_status_(status),
  output_file_size(options->max_file_size),
  leaf_limit_(leaf_limit) {
  for (int level = config::kNumLevels - 1; level >= 0; level--) {
    files_[level].clear();
  }
}

LevelDBLSMT::LevelDBLSMT(LevelDBLSMT& lsmt)
: level_limit_(lsmt.GetLevelLimit()),
  env_(lsmt.env_),
  dbname_(lsmt.dbname_),
  options_(lsmt.options_),
  icmp_(lsmt.icmp_),
  table_cache_(lsmt.table_cache_),
  file_id_(lsmt.file_id_),
  lsmt_status_(lsmt.lsmt_status_),
  compact_level_(lsmt.compact_level_),
  leaf_limit_(lsmt.leaf_limit_),
  output_file_size(lsmt.output_file_size),
  has_hotspot_(lsmt.has_hotspot_) {
  for (int l = 0; l <= lsmt.GetBottomLevel(); l++) {
    auto fs = &files_[l];
    fs->clear();
    for (int i = 0; i < lsmt.files_[l].size(); i++) {
      fs->push_back(lsmt.files_[l][i]);
    }
    if (!lsmt.compact_pointer_[l].empty()) {
      compact_pointer_[l] = lsmt.compact_pointer_[l];
    }
  }
  for (auto f : lsmt.obsolete_files_) {
    obsolete_files_.insert(f);
  }
}

LevelDBLSMT::~LevelDBLSMT() {
  for (int level = 0; level < config::kNumLevels; level++) {
    for (size_t i = 0; i < files_[level].size(); i++) {
      std::shared_ptr<FileMetaData> f = files_[level][i];
      // assert(f->refs > 0);
      // f->refs--;
      // if (f->refs <= 0) {
        // delete f;
        // f.reset();
      // }
    }
    files_[level].clear();
  }
}

struct LevelDBLSMT::CompactionState {
  // Files produced by compaction
  struct Output {
    uint64_t number;
    uint64_t file_size;
    uint64_t file_entry;
    InternalKey smallest, largest;
  };

  Output* current_output() { return &outputs[outputs.size() - 1]; }

  explicit CompactionState(LevelDBCompaction* c)
      : compaction(c),
        // smallest_snapshot(0),
        outfile(nullptr),
        builder(nullptr),
        total_bytes(0) {}

  LevelDBCompaction* const compaction;

  // Sequence numbers < smallest_snapshot are not significant since we
  // will never have to service a snapshot below smallest_snapshot.
  // Therefore if we have seen a sequence number S <= smallest_snapshot,
  // we can drop all entries for the same key with sequence numbers < S.
  // SequenceNumber smallest_snapshot;

  std::vector<Output> outputs;

  // State kept for output being generated
  WritableFile* outfile;
  TableBuilder* builder;

  uint64_t total_bytes;

  // keep the used files
  std::set<uint64_t> deletes;

  // this constant is removed as we want to make it dynamic
  // this constant is moved from Compaction as it can be null there
  // static const uint64_t max_output_file_size = 3 * 1024 * 1024;
  uint64_t max_output_file_size;
};

static bool NewestFirst(FileMetaData* a, 
                        FileMetaData* b) {
  return a->number > b->number;
}

void LevelDBLSMT::ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                                 bool (*func)(void*, int, FileMetaData*)) {
  const Comparator* ucmp = icmp_.user_comparator();

  // Search level-0 in order from newest to oldest.
  std::vector<FileMetaData*> tmp;
  tmp.reserve(files_[0].size());
  for (uint32_t i = 0; i < files_[0].size(); i++) {
    auto f = files_[0][i].get();
    if (ucmp->Compare(user_key, f->smallest.user_key()) >= 0 &&
        ucmp->Compare(user_key, f->largest.user_key()) <= 0) {
      tmp.push_back(f);
    }
  }
  if (!tmp.empty()) {
    std::sort(tmp.begin(), tmp.end(), NewestFirst);
    for (uint32_t i = 0; i < tmp.size(); i++) {
      if (!(*func)(arg, 0, tmp[i])) {
        return;
      }
    }
  }

  // Search other levels.
  for (int level = 1; level < GetBottomLevel(); level++) {
    size_t num_files = files_[level].size();
    if (num_files == 0) continue;

    // Binary search to find earliest index whose largest key >= internal_key.
    uint32_t index = FindFile(icmp_, files_[level], internal_key);
    if (index < num_files) {
      auto f = files_[level][index];
      if (ucmp->Compare(user_key, f->smallest.user_key()) < 0) {
        // All of "f" is past any data for user_key
      } else {
        if (!(*func)(arg, level, f.get())) {
          return;
        }
      }
    }
  }
}

bool LevelDBLSMT::UpdateStats(const GetStats& stats) {
  FileMetaData* f = stats.seek_file;
  if (f != nullptr) {
    f->allowed_seeks.fetch_sub(1);
    if (f->allowed_seeks.load() <= 0 && file_to_compact_level_.load() == -1) {
      file_num_to_compact_.store(f->number);
      file_to_compact_level_.store(stats.seek_file_level);
      return true;
    }
  }
  return false;
}

bool LevelDBLSMT::RecordReadSample(Slice internal_key) {
  ParsedInternalKey ikey;
  if (!ParseInternalKey(internal_key, &ikey)) {
    return false;
  }

  struct State {
    GetStats stats;  // Holds first matching file
    int matches;

    static bool Match(void* arg, int level, FileMetaData* f) {
      State* state = reinterpret_cast<State*>(arg);
      state->matches++;
      if (state->matches == 1) {
        // Remember first match.
        state->stats.seek_file = f;
        state->stats.seek_file_level = level;
      }
      // We can stop iterating once we have a second match.
      return state->matches < 2;
    }
  };

  State state;
  state.matches = 0;
  ForEachOverlapping(ikey.user_key, internal_key, &state, &State::Match);

  // Must have at least two matches since we want to merge across
  // files. But what if we have a single file that contains many
  // overwrites and deletions?  Should we have another mechanism for
  // finding such files?
  if (state.matches >= 2) {
    // 1MB cost is about 1 seek (see comment in Builder::Apply).
    return UpdateStats(state.stats);
  }
  return false;
}

int LevelDBLSMT::NumLevelFiles(int level) const {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  return files_[level].size();
}

// loosely based on db_impl.cc:1077
Iterator* LevelDBLSMT::NewMergedIterator(const ReadOptions& options) {
  std::vector<Iterator*> list;
  AddIterators(options, &list);
  Iterator* internal_iter =
      NewMergingIterator(&icmp_, &list[0], list.size());

  // TODO: the cleanup action is removed. Is it necessary?
  // IterState* cleanup = new IterState(&mutex_, mem_, imm_, versions_->current());
  internal_iter->RegisterCleanup(CleanupIteratorState, nullptr, nullptr);

  return internal_iter;
}

bool LevelDBLSMT::OverlapInLevel(int level, const Slice* smallest_user_key,
                             const Slice* largest_user_key) {
  return SomeFileOverlapsRange(icmp_, (level > 0), files_[level],
                               smallest_user_key, largest_user_key);
}

int LevelDBLSMT::PickLevelForMemTableOutput(const Slice& smallest_user_key,
                                        const Slice& largest_user_key) {
  int level = 0;
  if (!OverlapInLevel(0, &smallest_user_key, &largest_user_key)) {
    // Push to next level if there is no overlap in next level,
    // and the #bytes overlapping in the level after that are limited.
    InternalKey start(smallest_user_key, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey limit(largest_user_key, 0, static_cast<ValueType>(0));
    std::vector<std::shared_ptr<FileMetaData>> overlaps;
    int max_mem_compact_level = config::kMaxMemCompactLevel < level_limit_ ?
                                config::kMaxMemCompactLevel : level_limit_;
    while (level < max_mem_compact_level) {
      if (OverlapInLevel(level + 1, &smallest_user_key, &largest_user_key)) {
        break;
      }
      if (level + 2 < config::kNumLevels) {
        // Check that file does not overlap too many grandparent bytes.
        GetOverlappingInputs(level + 2, &start, &limit, &overlaps);
        const int64_t sum = TotalFileSize(overlaps);
        if (sum > MaxGrandParentOverlapBytes(options_)) {
          break;
        }
      }
      level++;
    }
  }
  if (level_limit_ > 0 && level >= level_limit_) {
    return 0;
  }
  return level;
}

void LevelDBLSMT::Print() {
  for (int level = 0; level < config::kNumLevels; level++) {
    auto fs = files_[level];
    if (fs.empty()) continue;
    fprintf(stdout, "\tLevel #%d: ", level);
    size_t total_size = 0;
    for (auto const & f : fs) {
      fprintf(stdout, "%ld: %s->%s of size %ld; ", f->number,
              f->smallest.user_key().ToString().c_str(),
              f->largest.user_key().ToString().c_str(), f->file_size);
      total_size += f->file_size;
    }
    fprintf(stdout, "total %ld\n", total_size);
  }
}

void LevelDBLSMT::PrintStat() {
  fprintf(stdout, "LSMT filesize: %ld; ", output_file_size);
  for (int level = 0; level < config::kNumLevels; level++) {
    auto fs = files_[level];
    if (fs.empty()) continue;
    size_t total_size = 0;
    for (auto const & f : fs) {
      total_size += f->file_size;
      if (f->file_size == 0) {
        fprintf(stderr, "Error: empty file\n");
      }
    }
    fprintf(stdout, "\n\tLevel[%d]=%ld file of %ldB",
                    level, fs.size(), total_size);
  }
  fprintf(stdout, "\n");
}

// version_set.cc:229
void LevelDBLSMT::AddIterators(const ReadOptions& options,
                               std::vector<Iterator*>* iters) {
  // Merge all level zero files together since they may overlap
  for (size_t i = 0; i < files_[0].size(); i++) {
    iters->push_back(table_cache_->NewIterator(options,
        files_[0][i]->number, files_[0][i]->file_size));
  }

  // For levels > 0, we can use a concatenating iterator that sequentially
  // walks through the non-overlapping files in the level, opening them
  // lazily.
  for (int level = 1; level < config::kNumLevels; level++) {
    if (!files_[level].empty()) {
      iters->push_back(NewConcatenatingIterator(options, level));
    }
  }
}

// version_set.cc:222
Iterator* LevelDBLSMT::NewConcatenatingIterator(const ReadOptions& options,
                                            int level) const {
  return NewTwoLevelIterator(
      new LevelFileNumIterator(icmp_, &files_[level]), &GetFileIterator,
        table_cache_, options);
}

void LevelDBLSMT::Clear() {
  for (int i = 0; i < config::kNumLevels; i++) {
    files_[i].clear();
  }
}

void LevelDBLSMT::ClearAll() {
  for (int i = 0; i < config::kNumLevels; i++) {
    for (size_t j = 0; j < files_[i].size(); j++) {
      obsolete_files_.insert(files_[i][j]->number);
    }
    files_[i].clear();
  }
}

uint64_t LevelDBLSMT::FileSizeLimit() {
  int level = level_limit_ < 0 ? config::kNumLevels : level_limit_;
  uint64_t lim = output_file_size * config::kL0_CompactionTrigger;
  double r = 15. * 1048576.0;
  for (int l = 1; l < level; l++) {
    lim += r;
    r *= 10;
  }
  return lim;
}

void LevelDBLSMT::Finalize() {
  // Precomputed best level for next compaction
  int best_level = -1;
  double best_score = -1;

  int max_level = GetBottomLevel();
  for (int level = 0; level <= max_level; level++) {
    double score;
    if (level == 0) {
      score = files_[level].size() /
              static_cast<double>(config::kL0_CompactionTrigger);
    } else {
      // Compute the ratio of current size to size limit.
      const uint64_t level_bytes = TotalFileSize(files_[level]);
      score =
          static_cast<double>(level_bytes) / MaxBytesForLevel(level);
    }

    if (score > best_score) {
      best_level = level;
      best_score = score;
    }
  }

  compact_level_ = best_level;
  compaction_score_ = best_score;
}

// LevelDB::version_set.cc:717
OpState LevelDBLSMT::AddFile(int level, FileMetaData of, const Page page) {
  FileMetaData f;
  f.number = of.number;
  f.file_size = of.file_size;
  f.file_entry = of.file_entry;
  f.smallest = of.smallest;
  f.largest = of.largest;
  std::shared_ptr<FileMetaData> fp = std::make_shared<FileMetaData>(f);

  fp->allowed_seeks.store(static_cast<int>((f.file_size / 16384U)));
  if (f.allowed_seeks.load() < 100) fp->allowed_seeks.store(100);

  if (level > 0) {
    BySmallestKey cmp;
    cmp.internal_comparator = &icmp_;
    FileSet* added_files = new FileSet(cmp);
    added_files->insert(fp);
    std::set<uint64_t> deleted_files;
    Status s = ApplyFileChanges(level, added_files, deleted_files);
    if (!s.ok()) {
      s = Status::NotFound("Error: add file\n");
      fprintf(stderr, "Error: add file %s\n", s.ToString().c_str()); 
    }
  } else {
    auto files = &files_[level];
    files->push_back(fp);
  }
  // For standalone LSMT, we do not perform compaction but find the best level
  if (lsmt_status_ == LSMTStatus::kStandalone) {
    Finalize();
    return OpState::kOk;
  }
  if (MayNeedCompaction(level) == OpState::kOverflow) {
    return CompactLevel(level, page);
  }
  return OpState::kOk;
}

// Only used in buffer tree
int LevelDBLSMT::AppendFile(int level, FileMetaData of) {
  assert(level == 0);
  FileMetaData f;
  f.number = of.number;
  f.file_size = of.file_size;
  f.file_entry = of.file_entry;
  f.smallest = of.smallest;
  f.largest = of.largest;
  std::shared_ptr<FileMetaData> fp = std::make_shared<FileMetaData>(f);

  fp->allowed_seeks.store(static_cast<int>((f.file_size / 16384U)));
  if (f.allowed_seeks.load() < 100) fp->allowed_seeks.store(100);
  auto files = &files_[level];
  files->push_back(fp);
  return files_[0].size();
}

// Files are appended at level-0 with no further compaction
OpState LevelDBLSMT::AppendFiles(int level, std::vector<std::shared_ptr<FileMetaData>>* metas) {
  assert(level == 0);

  std::vector<std::shared_ptr<FileMetaData>>* files = &files_[level];
  for (auto const& meta: *metas) {
    std::shared_ptr<FileMetaData> fp = meta;
    // fp->refs++;
    files->push_back(fp);
    // fp.reset();
  }
  return MayNeedCompaction(level);
}

OpState LevelDBLSMT::AddFiles(int level, std::vector<std::shared_ptr<FileMetaData>>* metas,
                              const Page page) {
  assert(level == 0);

  std::vector<std::shared_ptr<FileMetaData>>* files = &files_[level];
  for (auto const& meta: *metas) {
    std::shared_ptr<FileMetaData> fp = meta;
    // fp->refs++;
    files->push_back(fp);
    // fp.reset();
  }
  if (MayNeedCompaction(level) == OpState::kOverflow) {
    return CompactLevel(level, page);
  }
  return OpState::kOk;
}

OpState LevelDBLSMT::CompactTopLevel(const Page page) {
  // Search within level-0 to find possible min key
  // std::vector<std::shared_ptr<FileMetaData>>* files = &files_[0];
  // for (size_t i = 0; i < files->size(); i++) {
  //   auto f = files->at(i);
  //   if (mink->empty() || icmp_.Compare(f->smallest.Encode(), *mink) < 0) {
  //     *mink = f->smallest.Encode().ToString();
  //   }
  // }
  return CompactLevel(0, page);
}

// We skip PickCompaction() and directly call set up inputs
OpState LevelDBLSMT::DoCompact(const Page page, port::Mutex* mu, TreeLockManager* lm, int lock_id) {
  assert(compact_level_ >= 0);
  bool overflow = false;
  if (level_limit_ > 0 && compact_level_ >= level_limit_ - 1) {
    overflow = true;
  }
  if (overflow && ((TreePageHeader) page)->is_leaf_) {
    compaction_score_ = -1;
    compact_level_ = -1;
    return OpState::kOverflow;
  }
  LevelDBCompaction* c = new LevelDBCompaction(compact_level_);

  SetCompactionInputs(compact_level_, c);
  compact_level_ = c->level();

  if (c->num_input_files(0) == 0) {
    delete c;
    return OpState::kOk;
  }
  if (c->IsTrivialMove(-1)) {
    TrivialMove(c, compact_level_);
  } else {
    CompactionState* compact = new CompactionState(c);
    compact->max_output_file_size = output_file_size;
    Iterator* input = MakeInputIterator(compact->compaction);
    
    mu->Unlock();
    // if (!overflow) lm->WriteUnlock(lock_id);
    Status s = DoCompactionWorkImpl(compact, input, page);
    // if (!overflow) lm->WriteLock(lock_id);
    mu->Lock();

    if (s.ok()) {
      s = InstallCompactionResults(compact, compact_level_ + 1);
    }

    if (!s.ok()) {
      fprintf(stderr, "Error: incorrect compaction output: %s\n", s.ToString().c_str());
      std::abort();
    }
    // RemoveObsoleteFiles(compact);
    CleanupCompaction(compact);
  }
  delete c;
  Finalize();
  
  return overflow ? OpState::kOverflow : OpState::kOk;
}

uint64_t LevelDBLSMT::TotalFileSizeLSMT() {
  uint64_t sz = 0;
  int level = GetBottomLevel();
  for (int l = 0; l <= level; l++) {
    sz += TotalFileSize(files_[l]);
  }
  return sz;
}

// This is based on the leveled compaction
// LevelDB::version_set.cc:1031
OpState LevelDBLSMT::MayNeedCompaction(int level) {
  double score;
  switch (lsmt_status_) {
    case LSMTStatus::kSmallLeaf:
      score = files_[level].size() / static_cast<double>(leaf_limit_);
      break;
    case LSMTStatus::kLeaf:
    case LSMTStatus::kRootBuffer:
    case LSMTStatus::kStandalone:
    case LSMTStatus::kBuffer:
      if (level == 0) {
        score = files_[level].size() / static_cast<double>(config::kL0_CompactionTrigger);
      }
      if (level > 0 /*|| (level == 0 && score < 1)*/) {
        const uint64_t level_bytes = TotalFileSize(files_[level]);
        score = static_cast<double>(level_bytes) / MaxBytesForLevel(level);
      }
      break;
  }
  // if (level == 0 && lsmt_status_ == kStandalone) {
  //   score = files_[level].size() / static_cast<double>(config::kL0_CompactionTrigger);
  // } else {
  //   // When it is node-associated LSMT or level>0
  //   const uint64_t level_bytes = TotalFileSize(files_[level]);
  //   score = static_cast<double>(level_bytes) / MaxBytesForLevel(level);
  // }
  if (score >= 1) {
    return OpState::kOverflow;
  }
  return OpState::kOk;
}

// LevelDB::version_set.cc:1187
void LevelDBLSMT::GetRange(const std::vector<std::shared_ptr<FileMetaData>>& inputs,
                          InternalKey* smallest, InternalKey* largest) {
  assert(!inputs.empty());
  smallest->Clear();
  largest->Clear();
  for (size_t i = 0; i < inputs.size(); i++) {
    auto f = inputs[i];
    if (i == 0) {
      *smallest = f->smallest;
      *largest = f->largest;
    } else {
      if (icmp_.Compare(f->smallest, *smallest) < 0) {
        *smallest = f->smallest;
      }
      if (icmp_.Compare(f->largest, *largest) > 0) {
        *largest = f->largest;
      }
    }
  }
}

// LevelDB::version_set.cc:1211
void LevelDBLSMT::GetRange2(const std::vector<std::shared_ptr<FileMetaData>>& inputs1,
                           const std::vector<std::shared_ptr<FileMetaData>>& inputs2,
                           InternalKey* smallest, InternalKey* largest) {
  std::vector<std::shared_ptr<FileMetaData>> all = inputs1;
  all.insert(all.end(), inputs2.begin(), inputs2.end());
  GetRange(all, smallest, largest);
}

// LevelDB::version_set.cc:498
void LevelDBLSMT::GetOverlappingInputs(int level, const InternalKey* begin,
                                   const InternalKey* end,
                                   std::vector<std::shared_ptr<FileMetaData>>* inputs) {
  assert(level >= 0);
  assert(level < config::kNumLevels);
  inputs->clear();
  Slice user_begin, user_end;
  if (begin != nullptr) {
    user_begin = begin->user_key();
  }
  if (end != nullptr) {
    user_end = end->user_key();
  }
  const Comparator* user_cmp = icmp_.user_comparator();
  for (size_t i = 0; i < files_[level].size();) {
    auto f = files_[level][i++];
    const Slice& file_start = f->smallest.user_key();
    const Slice& file_limit = f->largest.user_key();
    if (begin != nullptr && user_cmp->Compare(file_limit, user_begin) < 0) {
      // "f" is completely before specified range; skip it
    } else if (end != nullptr && user_cmp->Compare(file_start, user_end) > 0) {
      // "f" is completely after specified range; skip it
    } else {
      inputs->push_back(f);
      if (level == 0) {
        // Level-0 files may overlap each other.  So check if the newly
        // added file has expanded the range.  If so, restart search.
        if (begin != nullptr && user_cmp->Compare(file_start, user_begin) < 0) {
          user_begin = file_start;
          inputs->clear();
          i = 0;
        } else if (end != nullptr &&
                   user_cmp->Compare(file_limit, user_end) > 0) {
          user_end = file_limit;
          inputs->clear();
          i = 0;
        }
      }
    }
  }
}

int LevelDBLSMT::GetSomeBottomLevelFiles(
                std::vector<std::shared_ptr<FileMetaData>>* fs, int flush_num) {
  int level = GetBottomLevel();
  if (level < 0) return level;
  if (level == 0 || files_[level].size() <= flush_num) {
    flush_pointer_[level].clear();
    return GetAllBottomLevelFiles(fs);
  }

  for (int i = 0; i < files_[level].size(); i++) {
    auto f = files_[level][i];
    if (flush_pointer_[level].empty() ||
        icmp_.Compare(f->largest.Encode(), flush_pointer_[level]) > 0) {
      fs->push_back(f);
    }
    if (fs->size() >= flush_num) {
      break;
    }
    if (i == files_[level].size() - 1) {
      i = 0;
      flush_pointer_[level].clear();
    }
  }
  flush_pointer_[level] = fs->back()->largest.Encode().ToString();
  return level;
}

// Loosely based on db_impl.cc:702 & version_set.cc:1252
void LevelDBLSMT::SetCompactionInputs(int level, LevelDBCompaction* c) {
  // TODO: Pick the first file that comes after compact_pointer_[level]
  // and use pointer to navigate between to-be-compacted files
  // int added_num = 0;
  // double target_size = MaxBytesForLevel(level);
  // uint64_t level_bytes = TotalFileSize(files_[level]);
  const bool size_compaction = (lsmt_status_ != LSMTStatus::kStandalone) ||
          (lsmt_status_ == LSMTStatus::kStandalone && compaction_score_ >= 1);
  const bool seek_compaction = (file_to_compact_level_.load() > -1);
  if (size_compaction) {
    assert(level >= 0);
    assert(level + 1 < config::kNumLevels);
    for (size_t i = 0; i < files_[level].size(); i++) {
      auto f = files_[level][i];
      if (compact_pointer_[level].empty() ||
          icmp_.Compare(f->largest.Encode(), compact_pointer_[level]) > 0) {
        c->inputs_[0].push_back(f);
        // added_num++;
        break;
        // IMPORTANT (1222): I cannot figure out why I chose to add more files.
        // level_bytes -= f->file_size;
        // pick more than 1 files in the input level to be compacted
        // in edge case where we insert in order, level-0 files may not overlap
        // with each other, so we manually add all level-0 files.
        // if (level != 0 && level_bytes < target_size)
        //   break;
      }
    }
    if (c->inputs_[0].empty()) {
      // Wrap-around to the beginning of the key space
      c->inputs_[0].push_back(files_[level][0]);
    }
  } else if (seek_compaction) {
    // Seek compaction
    level = file_to_compact_level_.load();
    c->level_ = level;
    file_to_compact_level_.store(-1);
    // make sure this file exist in the level
    bool found = false;
    for (size_t i = 0; i < files_[level].size(); i++) {
      auto f = files_[level][i];
      if (f->number == file_num_to_compact_) {
        found = true;
        c->inputs_[0].push_back(f);
        break;
      }
    }
    if (!found) {
      return;
    }
  } else {
    fprintf(stderr, "Error: no compaction\n");
    return;
  }
    
  // assert (c->inputs_[0].size() > 0);
  c->input_version_ = this;
  // Files in level 0 may overlap each other, so pick up all overlapping ones
  if (level == 0) {
    InternalKey smallest, largest;
    GetRange(c->inputs_[0], &smallest, &largest);
    GetOverlappingInputs(level, &smallest, &largest, &c->inputs_[0]);
    assert(!c->inputs_[0].empty());
  }
  // if (level_limit_ < 0 || level + 1 < level_limit_) {
    SetupOtherInputs(c);
  // }
}

void LevelDBLSMT::SetCompactionInputsBottomLevel(LevelDBCompaction* c) {
  int added_num = 0;
  int level = GetBottomLevel();
  for (size_t i = 0; i < files_[level].size(); i++) {
    auto f = files_[level][i];
    c->inputs_[0].push_back(f);
  }
  assert (c->inputs_[0].size() > 0);
  c->input_version_ = this;
}

void LevelDBLSMT::GetLevel0FilesInRange(std::vector<std::shared_ptr<FileMetaData>>* files,
                       std::string low, std::string up) {
  // Files are overlapping
  for (size_t i = 0; i < files_[0].size(); i++) {
    auto f = files_[0][i];
    if (user_comparator()->Compare(f->largest.user_key(), low) < 0) {
      // "f" is completely before specified range; skip it
    } else if (user_comparator()->Compare(f->smallest.user_key(), up) > 0) {
      // "f" is completely after specified range; skip it
    } else {
      files->push_back(f);
    }
  }
}

void LevelDBLSMT::GetLevelFilesInRange(std::vector<std::shared_ptr<FileMetaData>>* files,
                       std::string low, std::string up, int level) {
  assert(level != 0);
  // Binary search over file list
  uint32_t index = 0;
  // Find the earliest possible internal key for smallest_user_key
  InternalKey small_key(low, kMaxSequenceNumber,
                        kValueTypeForSeek);
  index = FindFile(icmp_, files_[level], small_key.Encode());

  if (index >= files_[level].size()) {
    // beginning of range is after all files, so no overlap.
    return;
  }
  for (size_t i = index; i < files_[level].size(); i++) {
    auto f = files_[level][i];
    if (user_comparator()->Compare(f->largest.user_key(), low) < 0) {
      // "f" is completely before specified range; skip it
    } else if (user_comparator()->Compare(f->smallest.user_key(), up) > 0) {
      // "f" is completely after specified range; skip it
      break;
    } else {
      files->push_back(f);
    }
  }
}

// Search from bottom level until we find files that overlap with range
int LevelDBLSMT::GetFilesInRange(std::vector<std::shared_ptr<FileMetaData>>* files,
                       std::string low, std::string up) {
  int level = GetBottomLevel();
  do {
    if (level == -1) return -1;
    if (level == 0) {
      GetLevel0FilesInRange(files, low, up);
    } else {
      GetLevelFilesInRange(files, low, up, level);
    }
    level--;
  } while (files->empty());
  // level = files->size() > 0 ? level + 1 : level;
  return level + 1;
}

void LevelDBLSMT::FinalizeRetrievalAndDelete(int level_of_files,
                                    std::vector<std::shared_ptr<FileMetaData>>* files) {
  FinalizeRetrieval(level_of_files, files);
  for (auto const & f : *files) {
    obsolete_files_.insert(f->number);
  }
}

// LOCKED
void LevelDBLSMT::FinalizeRetrieval(int level_of_files,
                                    std::vector<std::shared_ptr<FileMetaData>>* files) {
  int level = GetBottomLevel();
  if (level == -1) return;

  // Iterate all files in this level
  // for (int l = level; l >= 0; l--) {
  std::vector<std::shared_ptr<FileMetaData>> remain;
  size_t j = 0;
  for (size_t i = 0; i < files_[level_of_files].size(); i++) {
    auto f = files_[level_of_files][i];
    size_t j = 0;
    for (; j < files->size(); j++) {
      if (f->number == files->at(j)->number) break;
    }
    if (j == files->size()) {
      remain.push_back(f);
    }
  }
  auto level_files = &files_[level_of_files];
  level_files->clear();
  for (auto const & i : remain) {
    level_files->push_back(i);
  }
  if (files->size() > 0) {
    compact_pointer_[level_of_files] = files->at(files->size() - 1)->
                                    largest.Encode().ToString();
  }
  remain.clear();
  // }
}

// Status LevelDBLSMT::RetrieveFilesForTreePushdown(
//       std::vector<std::shared_ptr<FileMetaData>>* files, int flush_count) {
//   int level = GetBottomLevel();
//   if (level == -1) {
//     return Status::NotFound("Empty LSMT\n");
//   }

//   // If bottom level is level-i (i > 0), we pick flush_count
//   // number of files and update compaction pointer.
//   int added_num = 0;
//   std::string compact_pointer = compact_pointer_[level];
//   for (size_t i = 0; i < files_[level].size(); i++) {
//     auto f = files_[level][i];
//     if (added_num < flush_count && (compact_pointer.empty() ||
//           icmp_.Compare(f->largest.Encode(), compact_pointer) > 0)) {
//       files->push_back(f);
//       added_num++;
//     }
//     if (i == files_[level].size() - 1 && added_num == 0) {
//       if (!compact_pointer.empty()) {
//         compact_pointer.clear();
//         i = -1;
//       }
//     }
//   }
//   return Status::OK();
// }

// version_set.cc:1219
Iterator* LevelDBLSMT::MakeInputIterator(LevelDBCompaction* c) {
  ReadOptions options;
  options.verify_checksums = false;//options_->paranoid_checks;
  options.fill_cache = false;
  // Level-0 files have to be merged together.  For other levels,
  // we will make a concatenating iterator per level.
  // TODO(opt): use concatenating iterator for level-0 if there is no overlap
  const int space = (c->level() == 0 ? c->inputs_[0].size() + 1 : 2);
  Iterator** list = new Iterator*[space];
  int num = 0;
  for (int which = 0; which < 2; which++) {
    if (!c->inputs_[which].empty()) {
      if (c->level() + which == 0) {
        const std::vector<std::shared_ptr<FileMetaData>>& files = c->inputs_[which];
        for (size_t i = 0; i < files.size(); i++) {
          list[num++] = table_cache_->NewIterator(options, files[i]->number,
                                                  files[i]->file_size);
        }
      } else {
        // Create concatenating iterator for the files from this level
        list[num++] = NewTwoLevelIterator(
            new LevelFileNumIterator(icmp_, &c->inputs_[which]),
            &GetFileIterator, table_cache_, options);
      }
    }
  }
  assert(num <= space);
  Iterator* result = NewMergingIterator(&icmp_, list, num);
  delete[] list;
  return result;
}

// db_impl.cc::825
Status LevelDBLSMT::FinishCompactionOutputFile(CompactionState* compact,
                                              Iterator* input) {
  assert(compact != nullptr);
  assert(compact->outfile != nullptr);
  assert(compact->builder != nullptr);

  const uint64_t output_number = compact->current_output()->number;
  assert(output_number != 0);

  // Check for iterator errors
  Status s = input->status();
  const uint64_t current_entries = compact->builder->NumEntries();
  if (s.ok()) {
    s = compact->builder->Finish();
  } else {
    compact->builder->Abandon();
  }
  const uint64_t current_bytes = compact->builder->FileSize();
  compact->current_output()->file_size = current_bytes;
  compact->current_output()->file_entry = current_entries;
  compact->total_bytes += current_bytes;
  delete compact->builder;
  compact->builder = nullptr;

  // Finish and check for file errors
  if (s.ok()) {
    s = compact->outfile->Sync();
  }
  if (s.ok()) {
    s = compact->outfile->Close();
  }
  delete compact->outfile;
  compact->outfile = nullptr;

  // if (s.ok() && current_entries > 0) {
  //   // Verify that the table is usable
  //   ReadOptions options;
  //   options.verify_checksums = false;//options_->paranoid_checks;
  //   options.fill_cache = false;
  //   Iterator* iter =
  //       table_cache_->NewIterator(options, output_number, current_bytes);
  //   s = iter->status();
  //   delete iter;
  // }
  return s;
}

// db_impl.cc:800
Status LevelDBLSMT::OpenCompactionOutputFile(CompactionState* compact) {
  assert(compact != nullptr);
  assert(compact->builder == nullptr);
  uint64_t file_number;
  {
    file_id_.fetch_add(1);
    file_number = file_id_.fetch_add(1) + 1;
    // pending_outputs_.insert(file_number);
    CompactionState::Output out;
    out.number = file_number;
    out.smallest.Clear();
    out.largest.Clear();
    compact->outputs.push_back(out);
  }

  // Make the output file
  // std::cout << "compact result? " << dbname_  << ", " << file_number << std::endl;
  std::string fname = TableFileName(dbname_, file_number);
  Status s = env_->NewWritableFile(fname, &compact->outfile);
  if (s.ok()) {
    Options opt;
    opt.comparator = &icmp_;
    compact->builder = new TableBuilder(opt, compact->outfile);
  }
  return s;
}

Status LevelDBLSMT::DoCompactionTreeWorkImpl(CompactionState* compact,
                                          Iterator* input,
                                          std::vector<Slice>* guards,
                                          bool stop_early) {
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  input->SeekToFirst();
  bool compact_via_guard = false;
  bool beyond_guard = false;
  int guard_idx = 0;
  if (guards != nullptr && guards->size() > 0) {
    compact_via_guard = true;
    if (input->Valid()) {
      Slice key = input->key();
      ParseInternalKey(key, &ikey);
      while (guard_idx < guards->size()) {
        if (user_comparator()->Compare(guards->at(guard_idx), ikey.user_key) > 0) {
          break;
        }
        guard_idx++;
      }
      if (guard_idx > 0) {
        guard_idx = guard_idx - 1;
      } else {
        beyond_guard = true;
        guard_idx = 0;
      }
    }
  }
  bool reach_guard_end = false;
  while (input->Valid()) {
    Slice key = input->key();

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=
              0) {
        // First occurrence of this user key
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }
      if (last_sequence_for_key < kMaxSequenceNumber) {
        // Hidden by an newer entry for same user key
        drop = true;  // (A)
      } else if (ikey.type == kTypeDeletion && (compact->compaction == nullptr ||
                 compact->compaction->IsBaseLevelForKey(ikey.user_key, config::kNumLevels))) {
        drop = true;
      }

      last_sequence_for_key = ikey.sequence;
    }
    if (!drop) {
      // If we are given guards, we must output files according to that
      // regardless of the file sizes (nonempty).
      // Close output file if the next added key is greater than guard.
      if (beyond_guard) {
        if (guards->size() > 1 &&
            user_comparator()->Compare(guards->at(0), ikey.user_key) <= 0 &&
            user_comparator()->Compare(ikey.user_key, guards->at(guards->size() - 1)) < 0) {
          beyond_guard = false;
        } else if (guards->size() == 1 && 
                  user_comparator()->Compare(guards->at(0), ikey.user_key) <= 0) {
          beyond_guard = false;
        }

        if (!beyond_guard) {
          if (compact->builder != nullptr && compact->builder->NumEntries() > 0) {
            status = FinishCompactionOutputFile(compact, input);
            if (!status.ok()) {
              break;
            }
          }
        }
      }
      if (compact_via_guard && !beyond_guard) {
        bool next_guard = guard_idx >= guards->size() - 1 ? false : 
              user_comparator()->Compare(guards->at(guard_idx + 1), ikey.user_key) <= 0;
        if (compact->builder != nullptr && next_guard && compact->builder->NumEntries() > 0) {
          // The input here does not matter too much and is only used for checking
          // status. I tried to use a pointer of pointer of iterator, but not work.
          status = FinishCompactionOutputFile(compact, input);
          if (!status.ok()) {
            break;
          }
        }
        if (next_guard) guard_idx++;
        if (stop_early && guards->size() > 1 && guard_idx == guards->size() - 1)
          beyond_guard = true;
      }

      // Open output file if necessary
      if (compact->builder == nullptr) {
        status = OpenCompactionOutputFile(compact);
        if (!status.ok()) {
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);
      compact->builder->Add(key, input->value());

      // Close output file if it is big enough
      if (compact->builder->FileSize() >=
                    compact->max_output_file_size) {
                    // compact->compaction->MaxOutputFileSize()) {
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          break;
        }
      }
    }
    input->Next();
  }
  if (status.ok() && compact->builder != nullptr) {
    status = FinishCompactionOutputFile(compact, input);
  }
  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = nullptr;
  return status;
}

// Standalone LSMT is not protected by lock
Status LevelDBLSMT::DoCompactionWorkImpl(CompactionState* compact,
                                          Iterator* input,
                                          const Page page) {
  Status status;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  input->SeekToFirst();
  bool compact_via_guard = false;
  int guard_idx = 0;
  if (page != nullptr && ((TreePageHeader) page)->item_num_ > 0) {
    uint32_t guards_size = ((TreePageHeader) page)->item_num_;
    compact_via_guard = true;
    if (input->Valid()) {
      Slice key = input->key();
      ParseInternalKey(key, &ikey);
      // When lsmt pushes down to root, min pivot of root may need to be updated
      // if (modify_page &&
      //     user_comparator()->Compare(PageReadPivotAtOffset(page, 0), ikey.user_key) > 0) {
      //   PageUpdateMinPivot(page, ikey.user_key);
      // }
      while (guard_idx < guards_size) {
        if (user_comparator()->Compare(
                PageReadPivotAtOffset(page, guard_idx), ikey.user_key) > 0) {
          break;
        }
        guard_idx++;
      }
      // assert(guard_idx > 0);
      // When the min_key of the page has not been updated, there might be keys that are
      // smaller than the current min_key. These keys still belong to the first child.
      if (guard_idx == 0) guard_idx = 1;
      guard_idx = guard_idx - 1;
    }
  }
  bool reach_guard_end = false;
  while (input->Valid()) {
    Slice key = input->key();
    if ((compact->compaction != nullptr && compact->compaction->ShouldStopBefore(key)) &&
        compact->builder != nullptr) {
      status = FinishCompactionOutputFile(compact, input);
      if (!status.ok()) {
        break;
      }
    }

    // Handle key/value, add to state, etc.
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      // Do not hide error keys
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=
              0) {
        // First occurrence of this user key
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if (last_sequence_for_key < kMaxSequenceNumber) {
        // Hidden by an newer entry for same user key
        drop = true;  // (A)
      } else
      if (ikey.type == kTypeDeletion && (compact->compaction == nullptr ||
                 compact->compaction->IsBaseLevelForKey(ikey.user_key, config::kNumLevels))) {
        drop = true;
      }

      last_sequence_for_key = ikey.sequence;
    }
    if (!drop) {
      // If we are given guards, we must output files according to that
      // regardless of the file sizes (nonempty).
      // Close output file if the next added key is greater than guard.
      if (compact_via_guard) {
        uint32_t guards_size = ((TreePageHeader) page)->item_num_;
        bool next_guard = guard_idx >= guards_size - 1 ? false : 
              user_comparator()->Compare(
                    PageReadPivotAtOffset(page, guard_idx + 1), ikey.user_key) <= 0;
        if (compact->builder != nullptr && next_guard && compact->builder->NumEntries() > 0) {
          // The input here does not matter too much and is only used for checking
          // status. I tried to use a pointer of pointer of iterator, but not work.
          status = FinishCompactionOutputFile(compact, input);
          if (!status.ok()) {
            break;
          }
        }
        if (next_guard) guard_idx++;
      }

      // Open output file if necessary
      if (compact->builder == nullptr) {
        status = OpenCompactionOutputFile(compact);
        if (!status.ok()) {
          break;
        }
      }
      if (compact->builder->NumEntries() == 0) {
        compact->current_output()->smallest.DecodeFrom(key);
      }
      compact->current_output()->largest.DecodeFrom(key);
      compact->builder->Add(key, input->value());

      // Close output file if it is big enough
      if (!compact_via_guard && compact->builder->FileSize() >=
                                  compact->max_output_file_size) {
                    // compact->compaction->MaxOutputFileSize()) {
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok()) {
          break;
        }
      }
    }
    input->Next();
  }
  if (status.ok() && compact->builder != nullptr) {
    status = FinishCompactionOutputFile(compact, input);
  }
  if (status.ok()) {
    status = input->status();
  }
  delete input;
  input = nullptr;
  return status;
}

// db_impl.cc:892
Status LevelDBLSMT::DoCompactionWork(CompactionState* compact, const Page page, int target_level) {
  Iterator* input = MakeInputIterator(compact->compaction);
  Status status = DoCompactionWorkImpl(compact, input, page);
  if (status.ok()) {
    status = InstallCompactionResults(compact, target_level);
  }
  return status;
}

Status LevelDBLSMT::CompactTree(std::vector<Slice>* guards,
                                std::vector<std::shared_ptr<FileMetaData>>* outfiles) {
  ReadOptions options;
  options.fill_cache = false;
  Iterator* iter = NewMergedIterator(options);
  CompactionState* compact = new CompactionState(nullptr);
  compact->max_output_file_size = output_file_size;
  Status status = DoCompactionTreeWorkImpl(compact, iter, guards);
  if (status.ok()) {
    status = InstallCompactTreeResults(compact, outfiles);
  }
  AddDeletedFilesAllLevels(compact);
  // RemoveObsoleteFiles(compact);
  CleanupCompaction(compact);
  return status;
}

Status LevelDBLSMT::CompactTree(int* split_num,
                                std::vector<std::shared_ptr<FileMetaData>>* outfiles) {
  size_t total_size = TotalFileSizeLSMT();
  ReadOptions options;
  options.fill_cache = false;
  Iterator* iter = NewMergedIterator(options);
  CompactionState* compact = new CompactionState(nullptr);
  compact->max_output_file_size = total_size / (*split_num);
  Status status = DoCompactionTreeWorkImpl(compact, iter, nullptr);
  if (status.ok()) {
    status = InstallCompactTreeResults(compact, outfiles);
  }
  *split_num = outfiles->size();
  AddDeletedFilesAllLevels(compact);
  // RemoveObsoleteFiles(compact);
  CleanupCompaction(compact);
  return status;
}

bool LevelDBLSMT::NeedCompactBottomLevel() {
  int level = GetBottomLevel();
  if (level < 0) return false;
  if (level >= 1) return false;

  if (files_[/*level=*/0].size() == 1) {
    return false;
  }
  // TODO: if the level-0 files are not overlapping, we can skip compacting them
  return true;
}

Status LevelDBLSMT::CompactBottomLevel(const Page page) {
  int level = GetBottomLevel();
  if (level < 0) return Status::NotFound("Target level is negative");

  if (files_[level].size() == 1 && page == nullptr) {
    return Status::OK();
  }

  LevelDBCompaction* c = new LevelDBCompaction(level);
  SetCompactionInputsBottomLevel(c);
  assert(c->num_input_files(1) == 0);
  CompactLevelImpl(c, level, page, level);

  return Status::OK();
}

// Only compact files that are overlapping with the range in this level.
// The output files are still written to this level
// This LSM-tree is only read-locked. rootLSMT will be w-locked when installing.
// No w-lock is needed for nodeLSMT as we use double-buffering
void LevelDBLSMT::CompactFilesInRange(int level,
                                    std::vector<std::shared_ptr<FileMetaData>>* files,
                                    const Page page) {
  assert(level >= 0);
  // Level-0 is the bottom-most level that has files overlap with range
  LevelDBCompaction* c = new LevelDBCompaction(level);
  for (auto const& f: *files) {
    c->inputs_[0].push_back(f);
  }
  c->input_version_ = this;
  
  // Compact files overlapping the range and write the new files to the same level
  CompactionState* compact = new CompactionState(c);
  compact->max_output_file_size = output_file_size;
  Iterator* input = MakeInputIterator(compact->compaction);
  Status status = DoCompactionWorkImpl(compact, input, page);
  // if (lm != nullptr) lm->EscalateLock(-1);
  if (status.ok()) {
    status = InstallCompactionResults(compact, level);
  }

  // RemoveObsoleteFiles(compact);
  CleanupCompaction(compact);
  delete c;

  // if (lm != nullptr) {
  //   GetObsoleteFiles(dead_files);
  //   lm->AlleviateLock(-1);
  // }
  // return level;
}

Status LevelDBLSMT::InstallCompactTreeResults(CompactionState* compact,
                                          std::vector<std::shared_ptr<FileMetaData>>* outfiles) {
  BySmallestKey cmp;
  cmp.internal_comparator = &icmp_;
  FileSet* added_files = new FileSet(cmp);
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    FileMetaData f;
    f.number = out.number;
    f.file_size = out.file_size;
    f.file_entry = out.file_entry;
    f.smallest = out.smallest;
    f.largest = out.largest;
    f.allowed_seeks.store(static_cast<int>((f.file_size / 16384U)));
    if (f.allowed_seeks.load() < 100) f.allowed_seeks.store(100);
    std::shared_ptr<FileMetaData> p = std::make_shared<FileMetaData>(f);
    added_files->insert(p);
  }

  for (const auto& added_file : *added_files) {  
    outfiles->push_back(added_file);
  }
  
  // We omit the file range overlapping check as these files are all new
  added_files->clear();
  delete added_files;
  return Status::OK();
}

void LevelDBLSMT::AddDeletedFilesAllLevels(CompactionState* compact) {
  int level = GetBottomLevel();
  if (level < 0) return;
  for (int which = 0; which <= level; which++) {
    for (size_t i = 0; i < files_[which].size(); i++) {
      compact->deletes.insert(files_[which][i]->number);
      obsolete_files_.insert(files_[which][i]->number);
    }
  }
}

void LevelDBLSMT::AddDeletedFiles(CompactionState* compact,
                            std::vector<std::set<uint64_t>>* deleted) {
  LevelDBCompaction* c = compact->compaction;
  for (int which = 0; which < 2; which++) {
    std::set<uint64_t> ds;
    for (size_t i = 0; i < c->inputs_[which].size(); i++) {
      ds.insert(c->inputs_[which][i]->number);
      compact->deletes.insert(c->inputs_[which][i]->number);
      obsolete_files_.insert(c->inputs_[which][i]->number);
    }
    deleted->push_back(ds);
  }
}

// db_impl.cc:874
Status LevelDBLSMT::InstallCompactionResults(CompactionState* compact, int target) {
  // Add compaction outputs
  // need to remove used files and install new files sequentially
  std::vector<std::set<uint64_t>> deleted_files;
  AddDeletedFiles(compact, &deleted_files);

  const int level = compact->compaction->level();
  BySmallestKey cmp;
  cmp.internal_comparator = &icmp_;
  FileSet* added_files = new FileSet(cmp);
  for (size_t i = 0; i < compact->outputs.size(); i++) {
    const CompactionState::Output& out = compact->outputs[i];
    FileMetaData f;
    f.number = out.number;
    f.file_size = out.file_size;
    f.file_entry = out.file_entry;
    f.smallest = out.smallest;
    f.largest = out.largest;
    f.allowed_seeks.store(static_cast<int>((f.file_size / 16384U)));
    if (f.allowed_seeks.load() < 100) f.allowed_seeks.store(100);
    std::shared_ptr<FileMetaData> p = std::make_shared<FileMetaData>(f);
    added_files->insert(p);
  }

  Status s;
  int target_level = target;
  if (target_level == -1) {
    target_level = (level_limit_ > 0 && level + 1 >= level_limit_) ?
                        level : level + 1;
    if (lsmt_status_ == LSMTStatus::kStandalone) {
      target_level = level + 1;
    }
  }
  for (int lvl = level; lvl <= target_level; lvl++) {
    if (lvl == target_level) {
      s = ApplyFileChanges(lvl, added_files, deleted_files[lvl - level]);
    } else {
      s = ApplyFileChanges(lvl, nullptr, deleted_files[lvl - level]);
    }
    if (!s.ok()) {
      return Status::NotFound("Error: add file\n");
    }
  }
  added_files->clear();
  delete added_files;
  // if (level > 0) {
  //   for (uint32_t i = 1; i < files_[level].size(); i++) {
  //     const InternalKey& prev_end = files_[level][i - 1]->largest;
  //     const InternalKey& this_begin = files_[level][i]->smallest;
  //     if (icmp_.Compare(prev_end, this_begin) >= 0) {
  //       std::fprintf(stderr, "overlapping ranges in same level %s vs. %s\n",
  //                   prev_end.DebugString().c_str(),
  //                   this_begin.DebugString().c_str());
  //       std::abort();
  //     }
  //   }
  // }
  return Status::OK();
}

Status LevelDBLSMT::ApplyFileChanges(int level, FileSet* added, std::set<uint64_t> deleted_files) {
  size_t reserved_size;
  std::vector<std::shared_ptr<FileMetaData>> new_files;
  // LevelDB uses const_iterator but raises segv in my case
  auto base_files = files_[level];
  if (base_files.size() == 0 && !flush_pointer_[level].empty()) {
    // If the target level is empty, we init the flush pointer
    flush_pointer_[level].clear();
  }
  auto base_iter = base_files.begin();
  auto base_end = base_files.end();
  if (added != nullptr) {
    reserved_size = base_files.size() + added->size();
  } else {
    reserved_size = base_files.size();
  }
  // files_[level].reserve(reserved_size);
  int first_added = -1;
  if (added != nullptr) {
    // Install files in the next level only when there can be a next level
    for (const auto& added_file : *added) {
      // Add all smaller files listed in base_
      for (; base_iter != base_end; ++base_iter) {
        if (icmp_.Compare((*base_iter)->largest, added_file->smallest) < 0) {
          MaybeAddFile(level, *base_iter, &new_files, deleted_files);
        } else {
          break;
        }
      }
      if (first_added == -1) {
        first_added = new_files.size();
      }
      MaybeAddFile(level, added_file, &new_files, deleted_files);
    }
  }
    
  // Add remaining base files
  for (; base_iter != base_end; ++base_iter) {
    MaybeAddFile(level, *base_iter, &new_files, deleted_files);
  }

  std::vector<std::shared_ptr<FileMetaData>>* files = &files_[level];
  files->clear();
  files->resize(new_files.size());
  for (int i = 0; i < new_files.size(); i++) {
    files->at(i) = new_files[i];
  }
  new_files.clear();
  return Status::OK();
}

// version_set.cc:717
void LevelDBLSMT::MaybeAddFile(int level, std::shared_ptr<FileMetaData> f,
                                std::vector<std::shared_ptr<FileMetaData>>* files,
                                std::set<uint64_t> deletes) {
  if (deletes.count(f->number) > 0) {
    // File is deleted: do nothing
  } else {
    if (level > 0 && !files->empty()) {
      // Must not overlap
      assert(icmp_.Compare((*files)[files->size() - 1]->largest,
                                  f->smallest) < 0);
    }
    // f->refs++;
    files->push_back(f);
  }
}

// Loosely based on db_impl.cc:702 & version_set.cc:1252
OpState LevelDBLSMT::CompactLevel(int level, const Page page) {
  if (lsmt_status_ != LSMTStatus::kStandalone &&
       level_limit_ > 0 && level >= level_limit_ - 1) {
    // AHA tree only: this needs to be flushed to the buffer tree children
    return OpState::kOverflow;
  }

  LevelDBCompaction* c = new LevelDBCompaction(level);

  SetCompactionInputs(level, c);

  return CompactLevelImpl(c, level, page, level + 1);
}

void LevelDBLSMT::TrivialMove(LevelDBCompaction* c, int level) {
  assert(c->num_input_files(0) == 1);
  assert(c->num_input_files(1) == 0);
  std::shared_ptr<FileMetaData> f = c->input(0, 0);
  BySmallestKey cmp;
  cmp.internal_comparator = &icmp_;
  FileSet* added_files = new FileSet(cmp);
  added_files->insert(f);
  std::set<uint64_t> deleted_files;
  Status s = ApplyFileChanges(level + 1, added_files, deleted_files);
  if (!s.ok()) {
    s = Status::NotFound("Error: add file\n");
    fprintf(stderr, "Error: add file %s\n", s.ToString().c_str()); 
  }
  deleted_files.insert(f->number);
  added_files->clear();
  assert(deleted_files.size() == 1);
  s = ApplyFileChanges(level, nullptr, deleted_files);
  if (!s.ok()) {
    s = Status::NotFound("Error: remove file\n");
    fprintf(stderr, "Error: remove file %s\n", s.ToString().c_str()); 
  }
  // There are no obsolete files in this case
}

OpState LevelDBLSMT::CompactLevelImpl(LevelDBCompaction* c,
                                      int level, const Page page,
                                      int target_level) {
  if (level != target_level && c->IsTrivialMove(level_limit_)) {
    TrivialMove(c, level);
  } else {
    CompactionState* compact = new CompactionState(c);
    compact->max_output_file_size = output_file_size;
    Status s = DoCompactionWork(compact, page, target_level);
    if (!s.ok()) {
      fprintf(stderr, "Error: incorrect compaction output: %s\n", s.ToString().c_str());
      std::abort();
    }
    // RemoveObsoleteFiles(compact);
    CleanupCompaction(compact);
  }
  delete c;
  // int target_level = (level_limit_ > 0 && level + 1 >= level_limit_) ?
  //                       level : level + 1;
  // compact_level_ = -1;
  // if (lsmt_status_ == LSMTStatus::kStandalone && level + 1 >= level_limit_) {
  //   // root lsmt compacts bottom level
  //   return OpState::kOverflow;
  // }
  if (level == target_level) {
    // Compact bottom level()
    return OpState::kOverflow;
  }
  if (MayNeedCompaction(target_level) == OpState::kOverflow) {
    // if (lsmt_status_ == LSMTStatus::kStandalone) {
    //   // if (level_limit_ <= 0) {
    //     compact_level_ = target_level;
    //     return OpState::kOk;
    //   // }
    // } 
    // assert(level_limit_ > 0);
    // if (target_level + 1 >= level_limit_) {
    //   return OpState::kOverflow;
    // } else {
      return CompactLevel(target_level, page);
    // }
  }
  return OpState::kOk;
}

void LevelDBLSMT::GetObsoleteFiles(std::set<uint64_t>& files) {
  // Copy from obsolete_files_ to files
  for (auto const& f: obsolete_files_) {
    files.insert(f);
    table_cache_->Evict(f);     
  }
  obsolete_files_.clear();
}

void LevelDBLSMT::RemoveObsoleteFiles() {
  DeleteFiles(obsolete_files_);
  obsolete_files_.clear();
}

void LevelDBLSMT::DeleteFiles(std::set<uint64_t> dead) {
  std::vector<std::string> filenames;
  env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose
  uint64_t number;
  // FileType type;
  std::vector<std::string> files_to_delete;
  for (std::string& filename : filenames) {
    // we do not have many file types, just one xxx.sst
    // ignore other file types
    if (ParseFileName(filename, &number)) {
      bool keep = true;
      keep = (dead.find(number) == dead.end());
      if (!keep) {
        files_to_delete.push_back(std::move(filename));
        table_cache_->Evict(number);     
      }
    }
  }

  // While deleting all files unblock other threads. All files being deleted
  // have unique names which will not collide with newly created files and
  // are therefore safe to delete while allowing other threads to proceed.
  for (const std::string& filename : files_to_delete) {
    env_->RemoveFile(dbname_ + "/" + filename);
  }
  files_to_delete.clear();
  filenames.clear();
}

void LevelDBLSMT::RemoveFiles(Env* env, const std::string& dbname,
                              const std::set<uint64_t>& dead) {
  std::vector<std::string> filenames;
  env->GetChildren(dbname, &filenames);  // Ignoring errors on purpose
  uint64_t number;
  // FileType type;
  std::vector<std::string> files_to_delete;
  for (std::string& filename : filenames) {
    // we do not have many file types, just one xxx.sst
    // ignore other file types
    if (ParseFileName(filename, &number)) {
      bool keep = true;
      keep = (dead.find(number) == dead.end());
      if (!keep) {
        files_to_delete.push_back(std::move(filename));
      }
    }
  }

  for (const std::string& filename : files_to_delete) {
    env->RemoveFile(dbname + "/" + filename);
  }
  files_to_delete.clear();
  filenames.clear();
}

// db_impl.cc:783
void LevelDBLSMT::CleanupCompaction(CompactionState* compact) {
  if (compact->builder != nullptr) {
    // May happen if we get a shutdown call in the middle of compaction
    compact->builder->Abandon();
    delete compact->builder;
  } else {
    assert(compact->outfile == nullptr);
  }
  delete compact->outfile;
  // for (size_t i = 0; i < compact->outputs.size(); i++) {
  //   const CompactionState::Output& out = compact->outputs[i];
  //   pending_outputs_.erase(out.number);
  // }
  delete compact;
}

// LevelDB::version_set.cc:1385
void LevelDBLSMT::SetupOtherInputs(LevelDBCompaction* c) {
  const int level = c->level();
  InternalKey smallest, largest;

  AddBoundaryInputs(icmp_, files_[level], &c->inputs_[0]);
  GetRange(c->inputs_[0], &smallest, &largest);

  GetOverlappingInputs(level + 1, &smallest, &largest, &c->inputs_[1]);
  AddBoundaryInputs(icmp_, files_[level + 1], &c->inputs_[1]);

  // Get entire range covered by compaction
  InternalKey all_start, all_limit;
  GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);

  // See if we can grow the number of inputs in "level" without
  // changing the number of "level+1" files we pick up.
  if (!c->inputs_[1].empty()) {
    std::vector<std::shared_ptr<FileMetaData>> expanded0;
    GetOverlappingInputs(level, &all_start, &all_limit, &expanded0);
    AddBoundaryInputs(icmp_, files_[level], &expanded0);
    const int64_t inputs0_size = TotalFileSize(c->inputs_[0]);
    const int64_t inputs1_size = TotalFileSize(c->inputs_[1]);
    const int64_t expanded0_size = TotalFileSize(expanded0);
    if (expanded0.size() > c->inputs_[0].size() &&
        inputs1_size + expanded0_size <
            ExpandedCompactionByteSizeLimit(options_)) {
      InternalKey new_start, new_limit;
      GetRange(expanded0, &new_start, &new_limit);
      std::vector<std::shared_ptr<FileMetaData>> expanded1;
      GetOverlappingInputs(level + 1, &new_start, &new_limit,
                                     &expanded1);
      AddBoundaryInputs(icmp_, files_[level + 1], &expanded1);
      if (expanded1.size() == c->inputs_[1].size()) {
        smallest = new_start;
        largest = new_limit;
        c->inputs_[0] = expanded0;
        c->inputs_[1] = expanded1;
        GetRange2(c->inputs_[0], c->inputs_[1], &all_start, &all_limit);
      }
    }
  }

  // Compute the set of grandparent files that overlap this compaction
  // (parent == level+1; grandparent == level+2)
  // we set a level limit for WOT buffer LSMT
  if ((level_limit_ < 0 && level + 2 < config::kNumLevels) ||
        (level_limit_ > 0 && level + 2 < level_limit_)) {
    GetOverlappingInputs(level + 2, &all_start, &all_limit,
                                   &c->grandparents_);
  }

  // if (level != 0)
  compact_pointer_[level] = largest.Encode().ToString();
  // c->edit_.SetCompactPointer(level, largest);
}

// LevelDB::version_set.cc:1481
LevelDBCompaction::LevelDBCompaction(int level)
    : level_(level),
      input_version_(nullptr),
      grandparent_index_(0),
      seen_key_(false),
      overlapped_bytes_(0) {
  for (int i = 0; i < config::kNumLevels; i++) {
    level_ptrs_[i] = 0;
  }
}

// LevelDB::version_set.cc:1499
bool LevelDBCompaction::IsTrivialMove(int level_limit) const {
  // Avoid a move if there is lots of overlapping grandparent data.
  // Otherwise, the move could create a parent file that will require
  // a very expensive merge later on.
  if (level_limit > 0 && level_ + 1 >= level_limit) return false;
  return (num_input_files(0) == 1 && num_input_files(1) == 0 &&
          TotalFileSize(grandparents_) <= MaxGrandParentOverlapBytes(input_version_->options_));
}

// LevelDB::version_set.cc:1517
bool LevelDBCompaction::IsBaseLevelForKey(const Slice& user_key, int level_lim) {
  // Maybe use binary search to find right entry instead of linear search?
  const Comparator* user_cmp = input_version_->icmp_.user_comparator();
  int max_level = level_lim > 0 ? level_lim : config::kNumLevels;
  for (int lvl = level_ + 2; lvl < max_level; lvl++) {
    const std::vector<std::shared_ptr<FileMetaData>>& files = input_version_->files_[lvl];
    while (level_ptrs_[lvl] < files.size()) {
      auto f = files[level_ptrs_[lvl]];
      if (user_cmp->Compare(user_key, f->largest.user_key()) <= 0) {
        // We've advanced far enough
        if (user_cmp->Compare(user_key, f->smallest.user_key()) >= 0) {
          // Key falls in this file's range, so definitely not base level
          return false;
        }
        break;
      }
      level_ptrs_[lvl]++;
    }
  }
  return true;
}

// LevelDB::version_set.cc:1538
bool LevelDBCompaction::ShouldStopBefore(const Slice& internal_key) {
  // Scan to find earliest grandparent file that contains key.
  const InternalKeyComparator* icmp = &input_version_->icmp_;
  while (grandparent_index_ < grandparents_.size() &&
         icmp->Compare(internal_key,
                       grandparents_[grandparent_index_]->largest.Encode()) >
             0) {
    if (seen_key_) {
      overlapped_bytes_ += grandparents_[grandparent_index_]->file_size;
    }
    grandparent_index_++;
  }
  seen_key_ = true;

  if (overlapped_bytes_ > MaxGrandParentOverlapBytes(input_version_->options_)) {
    // Too much overlap for current output; start new output
    overlapped_bytes_ = 0;
    return true;
  } else {
    return false;
  }
}

} // namespace WOT_NAMESPACE