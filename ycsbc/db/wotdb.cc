#include "leveldb/include/options.h"
#include "wotdb.h"

using namespace WOT_NAMESPACE;

namespace ycsbc {

WotDB::WotDB(std::map<std::string, std::string>* props)
  : flush_id_(1),
    interval_(0) {
  is_lsmt_ = std::stoi((*props)["lsmt_level_limit"]) >= 6 ? true : false;
  fprintf(stdout, "Using %s\n", is_lsmt_ ? "LSMT" : "AHA");
  write_batch_size_ = std::stoi((*props)["batch_size"]);
  Options options;

  if (props->find("block_cache_size") != props->end()) {
    options.block_cache = NewLRUCache(std::stoi((*props)["block_cache_size"]));
  } else {
    fprintf(stdout, "Using default block cache size: 8MB\n");
  }
  if (props->find("memtable_size") != props->end()) {
    options.write_buffer_size = std::stoi((*props)["memtable_size"]);
  } else {
    fprintf(stdout, "Using default memtable size: 4MB\n");
  }
  if (props->find("file_size") != props->end()) {
    options.max_file_size = std::stoi((*props)["file_size"]);
  } else {
    fprintf(stdout, "Using default file size: 2MB\n");
  }
  if (props->find("block_size") != props->end()) {
    options.block_size = std::stoi((*props)["block_size"]);
  } else {
    fprintf(stdout, "Using default block size: 4KB\n");
  }
  if (props->find("max_open_files") != props->end()) {
    options.max_open_files = std::stoi((*props)["max_open_files"]);
  } else {
    fprintf(stdout, "Using default max_open_files: 1000\n");
  }
  if (props->find("node_lsmt_level_limit") != props->end()) {
    options.node_lsmt_level_limit = std::stoi((*props)["node_lsmt_level_limit"]);
  } else {
    fprintf(stdout, "Using default node_lsmt_level_limit: 2\n");
  }
  if (props->find("lsmt_level_limit") != props->end()) {
    options.lsmt_level_limit = std::stoi((*props)["lsmt_level_limit"]);
    fprintf(stdout, "Using lsmt_level_limit: %d\n", options.lsmt_level_limit);
  } else {
    fprintf(stdout, "Using default lsmt_level_limit: 3\n");
  }
  if (props->find("leaf_limit") != props->end()) {
    options.leaf_limit = std::stoi((*props)["leaf_limit"]);
  } else {
    fprintf(stdout, "Using default leaf_limit: 1\n");
  }
  if (props->find("buffer_shrink_ratio") != props->end()) {
    options.buffer_shrink_ratio = std::stoi((*props)["buffer_shrink_ratio"]);
  } else {
    fprintf(stdout, "Using default buffer_shrink_ratio: 4\n");
  }
  ///////////////////////
  if (props->find("buffer_manager_num") != props->end()) {
    options.buffer_manager_num = std::stoi((*props)["buffer_manager_num"]);
  } else {
    fprintf(stdout, "Using default buffer_manager_num: 512\n");
  }
  if (props->find("flush_file_num") != props->end()) {
    options.flush_file_num = std::stoi((*props)["flush_file_num"]);
  } else {
    fprintf(stdout, "Using default flush_file_num: 0\n");
  }
  ///////////////////////
  if (props->find("adapt_strategy") != props->end()) {
    options.adapt_strategy = std::stoi((*props)["adapt_strategy"]);
    fprintf(stdout, "Using adapt_strategy: %d\n", options.adapt_strategy);
  } else {
    fprintf(stdout, "Using default adapt_strategy: 5\n");
  }
  ///////////////////////
  if (props->find("page_split_first_policy") != props->end()) {
    options.first_page_split_policy = std::stoi((*props)["page_split_first_policy"]);
  } else {
    fprintf(stdout, "Using default page_split_first_policy: 1\n");
  }
  if (props->find("page_split_second_policy") != props->end()) {
    options.second_page_split_policy = std::stoi((*props)["page_split_second_policy"]);
  } else {
    fprintf(stdout, "Using default page_split_second_policy: 1\n");
  }
  ///////////////////////
  if (props->find("proactive_validation") != props->end()) {
    options.proactive_validation = std::stoi((*props)["proactive_validation"]);
  } else {
    fprintf(stdout, "Using default proactive_validation: false\n");
  }

  options.filter_policy = NewBloomFilterPolicy(10);
  options.reuse_logs = false;
  options.compression = kNoCompression;

  tree_ = new AhaTree(options, (*props)["lsmt_path"], flush_id_,
                         (*props)["is_lsmt"]=="1");
}

void WotDB::SetHotspots(const std::vector<std::pair<std::string,std::string>> &hotspot) {
  for (auto &p : hotspot) {
    hotspots_.push_back(p.first);
    hotspots_.push_back(p.second);
    fprintf(stdout, "Hotspot: [%s, %s]\n", p.first.c_str(), p.second.c_str());
  }
}

bool WotDB::KeyInHotspot(const std::string &key) {
  for (size_t i = 0; i < hotspots_.size(); i += 2) {
    if (key >= hotspots_[i] && key <= hotspots_[i+1]) {
      return true;
    }
  }
  return false;
}

int WotDB::Insert(const std::string &table, const std::string &key,
                  std::vector<KVPair> &values) {
  if (tree_insert_ && KeyInHotspot(key)) {
    for (KVPair &p : values) {
      tree_->Insert(key, p.second);
    }
  } else {
    // Assume one entry per batch to simplify counting operations
    for (KVPair &p : values) {
      WriteBatch batch;
      batch.Clear();
      batch.Put(key, p.second);
      // Status s = tree_->Insert(key, p.second);
      Status s = tree_->Insert(&batch);

      if (!s.ok()) {
        std::cerr << "Insertion failed at " << key << "=>" << p.second << "\n";
        return DB::kErrorConflict; 
      } else {
        break;
      }
    }
  }
  if (tmp_key_.empty()) {
    tmp_key_ = key;
  }
  return 1;
}

int WotDB::BatchInsert(const std::string &table,
                            std::vector<KVPair> &values, bool has_hot) {
  WriteBatch batch;
  batch.Clear();
  for (auto &p : values) {
    if (tmp_key_.empty()) {
      tmp_key_ = p.first;
    }
    batch.Put(p.first, p.second);
  }
  // Default to be hotspot
  if (!has_hot) {
    batch.UnsetHasHotspot();
  }
  Status s = tree_->Insert(&batch);
  if (!s.ok()) {
    std::abort();
  }
  return values.size();
}

int WotDB::BatchUpdate(const std::string &table,
                            std::vector<KVPair> &values) {
  if (!tree_insert_) {
    return BatchInsert(table, values);
  }
  std::vector<KVPair> filtered;
  int count = 0;
  for (auto &p : values) {
    // bool hot = hot_queried_start_.empty() ? true :
    //            (p.first >= hot_queried_start_ && p.first <= hot_queried_end_);    
    bool hot = hotspots_.size() == 0 ? true : KeyInHotspot(p.first);
    if (hot) {
      // Assume adaptation has already finished and hotspot is in pages
      tree_->Insert(p.first, p.second);
      count++;
    } else {
      filtered.push_back(p);
    }
  }
  if (filtered.size() > 0) {
    count += BatchInsert(table, filtered, false);
  }
  return count;
}

int WotDB::Read(const std::string &table, const std::string &key,
                const std::vector<std::string> *fields,
                std::vector<KVPair> &result) {
  std::string value;
  Status s = tree_->Query(key, &value);
  if (!s.ok()) {
    // std::cout << "Key " << key << " not found\n";
    return DB::kErrorNoData;
  }
  result.push_back(std::make_pair(key, value));
  return DB::kOK;
}

int WotDB::Scan(const std::string &table, const std::string &key,
                const std::string &key2,
                const std::vector<std::string> *fields,
                std::vector<std::vector<KVPair>> &result, int select) {
  // UnsortedTreeIterator* iter = tree_->NewUnsortedTreeIterator(key, key2);
  // if (key < hot_queried_start_ || key > hot_queried_end_) return 0;
  SortedTreeIterator* iter = tree_->NewSortedTreeIterator();
  std::vector<KVPair> tmp;
  // iter->Seek(key);
  iter->SeekBothEnds(key, key2);
  for (; iter->Valid(); iter->Next()) {
    if (!key2.empty() && iter->key() > key2) break;
    if (select > 0 && tmp.size() >= select) break;
    tmp.push_back(std::make_pair(iter->key(), iter->value()));
  }
  result.push_back(tmp);
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
  delete iter;
  return 1;
}

int WotDB::Update(const std::string &table, const std::string &key,
                    std::vector<KVPair> &values) {
  return Insert(table, key, values);
}

int WotDB::Delete(const std::string &table, const std::string &key){
  Status s = tree_->Delete(key);
  if (!s.ok()) {
    std::cout << "Deletion failed at " << key << std::endl;
    return DB::kErrorConflict;
  }
  return DB::kOK;
}

void WotDB::Print() {
  // std::cout << "TreeHeight: " << tree_->TreeHeight() << std::endl;
  //           ", FlushTree: " << tree_->GetAndResetFlushTree() <<
  //           ", FlushLSMT: " << tree_->GetAndResetFlushLSMT() <<
  //           ", CompactRatio: " << tree_->GetAndResetCompactRatio();
  // std::cout << std::endl;
  // tree_->PrintStat();
#ifdef BREAKDOWN
  tree_->reader_stat_.PrintStat();
  tree_->reader_stat_.Reset();
  tree_->writer_stat_.PrintStat();
  tree_->writer_stat_.Reset();
#endif
}

void WotDB::PrintAllPages() {
  // std::string low, up;
  // if (hot_queried_start_.empty() || hot_queried_end_.empty()) {
  //   low = std::string(tmp_key_.length(), '0');
  //   up = std::string(tmp_key_.length(), '9');
  // } else {
  //   low = hot_queried_start_;
  //   up = hot_queried_end_;
  // }
  // tree_->SetPrintPage();
  // SortedTreeIterator* iter = tree_->NewSortedTreeIterator();
  // std::vector<KVPair> tmp;
  // iter->SeekBothEnds(low, up);
  // for (; iter->Valid(); iter->Next()) {
  //   if (iter->key() > up) break;
  //   tmp.push_back(std::make_pair(iter->key(), iter->value()));
  // }
  // delete iter;
  // tree_->UnsetPrintPage();
}

void WotDB::RemoveTree(bool concurrent) {
  // tree_->IncreaseNodeSize();
}

void WotDB::AddTree(bool adapt) {
  if (adapt) {
    tree_->AdaptToRead();
    if (hotspots_.size() > 0) {
      tree_->SetHotspots(hotspots_);
    }
  } else {
    tree_->StopAdaptToRead();
  }
}

void WotDB::PrintAllData(std::string lk, std::string uk, std::string filename) {
  FILE* f = fopen(filename.c_str(), "w");
  SortedTreeIterator* iter = tree_->NewSortedTreeIterator();
  iter->SeekBothEnds(lk, uk);
  for (; iter->Valid(); iter->Next()) {
    if (!uk.empty() && iter->key() > uk) break;
    auto k = iter->key();
    auto v = iter->value();
    fprintf(f, "print: %s => %c\n", k.substr(k.size()-10).c_str(), v.c_str()[0]);
  }
  delete iter;
  fclose(f);
}

bool WotDB::BGFlushIdle() {
  return tree_->BGFlushIdle();
}

bool WotDB::BGCompactionIdle() {
  return tree_->BGCompactionIdle();
}

} // namespace ycsbc