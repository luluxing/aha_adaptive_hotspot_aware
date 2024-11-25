#include "btree_db.h"
#include <iostream>

using namespace WOT_NAMESPACE;

namespace ycsbc {

BtreeDB::BtreeDB(std::map<std::string, std::string>* options)
: tree_(new Btree(options)),
  interval_(0) {}

int BtreeDB::Read(const std::string &table, const std::string &key,
                  const std::vector<std::string> *fields,
                  std::vector<KVPair> &result) {
  std::string value;
  Status s = tree_->Query(key, &value);
  if (!s.ok()) {
    return DB::kErrorNoData;
  }
  result.push_back(std::make_pair(key, value));
  return DB::kOK;
}
            
int BtreeDB::Scan(const std::string &table, const std::string &key,
                  const std::string &key2,
                  const std::vector<std::string> *fields,
                  std::vector<std::vector<KVPair>> &result, int select) {
  Iterator* iter = tree_->NewBtreeIterator();
  std::vector<KVPair> tmp;
  do {
    tmp.clear();
    for (iter->Seek(key); iter->Valid(); iter->Next()) {
      if (!key2.empty() && iter->key().ToString() > key2) break;
      if (select > 0 && tmp.size() >= select) break;
      tmp.push_back(std::make_pair(iter->key().ToString(), iter->value().ToString()));
    }
  } while (!iter->status().ok());
  result.push_back(tmp);
  if (interval_.fetch_add(1, std::memory_order_relaxed) % 100000 == 0) {
    fprintf(stdout, "Result size: %ld in [%s, %s]:", tmp.size(), key.c_str(), key2.c_str());
    // for (auto &p : tmp) {
    //   // Print the substring of the key that contains last 10 characters
    //   // Print the first character of the value
    //   fprintf(stdout, "%s => %c;", p.first.substr(p.first.size()-10).c_str(), p.second[0]);
    // }
    fprintf(stdout, "\n");
  }
  // fprintf(stdout, "Result size: %ld in [%s, %s]\n", tmp.size(), key.c_str(), key2.c_str());
  delete iter;
  return 1;
}

int BtreeDB::Update(const std::string &table, const std::string &key,
                    std::vector<KVPair> &values) {
  assert(values.size() == 1);
  return Insert(table, key, values);
}

int BtreeDB::Insert(const std::string &table, const std::string &key,
                  std::vector<KVPair> &values) {
  for (KVPair &p : values) {
    Status s = tree_->Insert(key, p.second);
    if (key.substr(key.size() - 10) == "0012126682") {
      fprintf(stdout, "%s\n", p.second.c_str());
    }
    if (tmp_key_.empty()) {
      tmp_key_ = key;
    }
    if (!s.ok()) {
      std::cerr << "Insertion failed at " << key << "=>" << p.second << "\n";
      return DB::kErrorConflict; 
    } else {
      break;
    }
  }
  return 1;
}

int BtreeDB::Delete(const std::string &table, const std::string &key) {
  Status s = tree_->Delete(key);
  if (!s.ok()) {
    std::cerr << "Deletion failed at " << key << "\n";
    return DB::kErrorConflict; 
  }
  return DB::kOK;
}

void BtreeDB::Print() {
  // tree_->PrintStat();
}

void BtreeDB::PrintAllPages() {
  // std::string low, up;
  // if (hot_queried_start_.empty() || hot_queried_end_.empty()) {
  //   low = std::string(tmp_key_.length(), '0');
  //   up = std::string(tmp_key_.length(), '9');
  // } else {
  //   low = hot_queried_start_;
  //   up = hot_queried_end_;
  // }
  // tree_->SetPrintPage();
  // Iterator* iter = tree_->NewBtreeIterator();
  // std::vector<KVPair> tmp;
  // do {
  //   tmp.clear();
  //   for (iter->Seek(low); iter->Valid(); iter->Next()) {
  //     if (iter->key().ToString() > up) break;
  //     tmp.push_back(std::make_pair(iter->key().ToString(), iter->value().ToString()));
  //   }
  // } while (!iter->status().ok());
  // delete iter;
  // tree_->UnsetPrintPage();
}

void BtreeDB::PrintAllData(std::string lk, std::string uk, std::string filename) {
  Iterator* iter = tree_->NewBtreeIterator();
  std::vector<KVPair> tmp;
  FILE* f = fopen(filename.c_str(), "w");
  do {
    tmp.clear();
    for (iter->Seek(lk); iter->Valid(); iter->Next()) {
      if (!uk.empty() && iter->key().ToString() > uk) break;
      auto k = iter->key().ToString();
      auto v = iter->value().ToString();
      fprintf(f, "print: %s => %c\n", k.substr(k.size()-10).c_str(), v.c_str()[0]);
    }
  } while (!iter->status().ok());
  fclose(f);
  delete iter;
}

void BtreeDB::RemoveTree(bool concurrent) {}

void BtreeDB::AddTree(bool add) {}

} // namespace ycsbc