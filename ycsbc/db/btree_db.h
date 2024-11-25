#include <atomic>
#include "btree/btree.h"
#include "../core/db.h"
#include "../core/properties.h"

namespace ycsbc{

class BtreeDB : public DB {
 public:
  BtreeDB(std::map<std::string, std::string>* options);
  ~BtreeDB() { delete tree_; }
  void Init() {};
  void Close() {};
  int Read(const std::string &table, const std::string &key,
                  const std::vector<std::string> *fields,
                std::vector<KVPair> &result);
  int Scan(const std::string &table, const std::string &key,
                  int record_count, const std::vector<std::string> *fields,
                  std::vector<std::vector<KVPair>> &result, int select=-1) { return 1; }
                  
  int Scan(const std::string &table, const std::string &key,
                  const std::string &key2,
                  const std::vector<std::string> *fields,
                  std::vector<std::vector<KVPair>> &result, int select=-1);
  int Update(const std::string &table, const std::string &key,
                    std::vector<KVPair> &values);
  int Insert(const std::string &table, const std::string &key,
                    std::vector<KVPair> &values);
  int BatchInsert(const std::string &table,
                  std::vector<KVPair> &values, bool has_hot=true) { return DB::kErrorNoData; }
  int BatchUpdate(const std::string &table,
                  std::vector<KVPair> &values) { return DB::kErrorNoData; }

  int Delete(const std::string &table, const std::string &key);
  void Print();
  void PrintAllPages();
  void RemoveTree(bool concurrent);
  void AddTree(bool add);
  void EnableTreeInsertion() {}
  void EnableBufferInsertion() {}
  // void SetHotspotStart(const std::string &start) { hot_queried_start_ = start; }
  // void SetHotspotEnd(const std::string &end) { hot_queried_end_ = end; }
  bool BGCompactionIdle() { return true; }
  bool BGFlushIdle() { return true; }
  bool IsBtree() { return true; }
  bool IsLSMT() { return false; }
  int WriteBatchSize() { return 0; }
  void PrintAllData(std::string, std::string, std::string);
  void SetHotspots(const std::vector<std::pair<std::string,std::string>> &hotspot) {}
 
 private:
  WOT_NAMESPACE::Btree* tree_;
  // std::string hot_queried_start_;
  // std::string hot_queried_end_;
  std::atomic<int> interval_;
  std::string tmp_key_;
};

} // namespace ycsbc

