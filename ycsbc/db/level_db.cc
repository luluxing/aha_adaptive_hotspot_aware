#include <atomic>
#include "level_db.h"
#include "leveldb/cache.h"
#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/write_batch.h"

namespace ycsbc {

class CountComparator : public Comparator {
 public:
  CountComparator(const Comparator* wrapped) : wrapped_(wrapped) {}
  ~CountComparator() override {}
  int Compare(const Slice& a, const Slice& b) const override {
    count_.fetch_add(1, std::memory_order_relaxed);
    return wrapped_->Compare(a, b);
  }
  const char* Name() const override { return wrapped_->Name(); }
  void FindShortestSeparator(std::string* start,
                             const Slice& limit) const override {
    wrapped_->FindShortestSeparator(start, limit);
  }

  void FindShortSuccessor(std::string* key) const override {
    return wrapped_->FindShortSuccessor(key);
  }

  size_t comparisons() const { return count_.load(std::memory_order_relaxed); }

  void reset() { count_.store(0, std::memory_order_relaxed); }

 private:
  mutable std::atomic<size_t> count_{0};
  const Comparator* const wrapped_;
};

LeveldbYcsb::LeveldbYcsb(std::map<std::string, std::string>* params)
: db_(nullptr),
  write_options_(WriteOptions()),
  interval_(0) {
  write_batch_size_ = std::stoi((*params)["batch_size"]);
  fprintf(stdout, "LevelDB writeBatch size is %ld\n", write_batch_size_);
  Open(params);
}

void LeveldbYcsb::Open(std::map<std::string, std::string>* params) {
  assert(db_ == nullptr);
  Options options;
  options.env = leveldb::Env::Default();;
  options.create_if_missing = true;
  options.block_cache = NewLRUCache(3 << 20);
  options.write_buffer_size = std::stoi((*params)["memtable_size"]);
  options.max_file_size = std::stoi((*params)["file_size"]);
  options.block_size = 4096; // 4KB because the same prefix are compacted
  // CountComparator count_comparator = BytewiseComparator();
  // count_comparator.reset();
  // options.comparator = &count_comparator;
  
  options.max_open_files = 74; // same as in Bol??

  options.filter_policy = NewBloomFilterPolicy(10);
  options.reuse_logs = false;
  options.compression = kNoCompression;
  Status s = leveldb::DB::Open(options, (*params)["lsmt_path"], &db_);
  if (!s.ok()) {
    std::fprintf(stderr, "open error: %s\n", s.ToString().c_str());
    std::exit(1);
  }
}

LeveldbYcsb::~LeveldbYcsb() {
  delete db_;
}

int LeveldbYcsb::Read(const std::string &table, const std::string &key,
                  const std::vector<std::string> *fields,
                std::vector<KVPair> &result) {
  ReadOptions options;
  std::string value;
  int found = 0;
  if (db_->Get(options, key, &value).ok()) {
    return DB::kOK;
  }
  return DB::kErrorNoData;
}
                
int LeveldbYcsb::Scan(const std::string &table, const std::string &key,
                const std::string &key2,
                const std::vector<std::string> *fields,
                std::vector<std::vector<KVPair>> &result, int select) {
  ReadOptions options;
  Iterator* iter = db_->NewIterator(options);
  int found = 0;
  std::vector<KVPair> tmp;
  for (iter->Seek(key); iter->Valid(); iter->Next()) {
    if (!key2.empty() && strcmp(iter->key().ToString().c_str(), key2.c_str()) > 0) break;
    if (select > 0 && found >= select) break;
    found++;
    tmp.push_back(std::make_pair(iter->key().ToString(), iter->value().ToString()));
  }
  result.push_back(tmp);
  delete iter;
  if (interval_.fetch_add(1, std::memory_order_relaxed) % 100000 == 0) {
    fprintf(stdout, "Result size: %ld in [%s, %s]:", tmp.size(), key.c_str(), key2.c_str());
  //   // for (auto &p : tmp) {
  //   //   // Print the substring of the key that contains last 10 characters
  //   //   // Print the first character of the value
  //   //   fprintf(stdout, "%s => %c;", p.first.substr(p.first.size()-10).c_str(), p.second[0]);
  //   // }
    fprintf(stdout, "\n");
  }
  // fprintf(stdout, "Output size: %ld in [%s, %s]\n", tmp.size(), key.c_str(), key2.c_str());
  return 1;
}
                
int LeveldbYcsb::Update(const std::string &table, const std::string &key,
                  std::vector<KVPair> &values) {
  interval_.fetch_add(1, std::memory_order_relaxed);
  return Insert(table, key, values);
}

int LeveldbYcsb::Insert(const std::string &table, const std::string &key,
                  std::vector<KVPair> &values) {
  for (KVPair &p : values) {
    WriteBatch batch;
    batch.Clear();
    batch.Put(key, p.second);
    // Status s = tree_->Insert(key, p.second);
    Status s = db_->Put(write_options_, key, p.second);
    if (!s.ok()) {
      std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
      std::exit(1);
    } else {
      break;
    }
  }
  return DB::kOK;
}

int LeveldbYcsb::BatchInsert(const std::string &table,
                            std::vector<KVPair> &values, bool has_hot) {
  WriteBatch batch;
  batch.Clear();
  for (auto &p : values) {
    batch.Put(p.first, p.second);
  }
  Status s = db_->Write(write_options_, &batch);
  if (!s.ok()) {
    std::abort();
  }
  return values.size();
}

int LeveldbYcsb::BatchUpdate(const std::string &table,
                            std::vector<KVPair> &values) {
  return BatchInsert(table, values);
}

void LeveldbYcsb::PrintAllData(std::string lk, std::string uk, std::string filename) {
  ReadOptions options;
  FILE* f = fopen(filename.c_str(), "w");
  Iterator* iter = db_->NewIterator(options);
  for (iter->Seek(lk); iter->Valid(); iter->Next()) {
    if (!uk.empty() && strcmp(iter->key().ToString().c_str(), uk.c_str()) > 0) break;
    auto k = iter->key().ToString();
    auto v = iter->value().ToString();
    fprintf(f, "print: %s => %c\n", k.substr(k.size()-10).c_str(), v.c_str()[0]);
  }
  delete iter;
  fclose(f);
}

int LeveldbYcsb::Delete(const std::string &table, const std::string &key) { return 1; }

void LeveldbYcsb::Print() {}
void LeveldbYcsb::RemoveTree(bool concurrent) {}
void LeveldbYcsb::AddTree(bool add) {}

bool LeveldbYcsb::BGFlushIdle() { return true; }
bool LeveldbYcsb::BGCompactionIdle() { return false; }

} // namespace ycsbc