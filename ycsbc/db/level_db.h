#include "leveldb/db.h"
#include "leveldb/env.h"
#include "../core/db.h"
#include "../core/properties.h"

using namespace leveldb;

namespace ycsbc{

class LeveldbYcsb : public DB {
 public:
  LeveldbYcsb(std::map<std::string, std::string>* params);
  ~LeveldbYcsb();
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
                  std::vector<KVPair> &values, bool has_hot=true);
  int BatchUpdate(const std::string &table,
                  std::vector<KVPair> &values);

  int Delete(const std::string &table, const std::string &key);
  void Print();
  void RemoveTree(bool concurrent);
  void AddTree(bool add);
  void EnableTreeInsertion() {}
  void EnableBufferInsertion() {}
  void SetHotspotStart(const std::string &start) { hot_queried_start_ = start; }
  void SetHotspotEnd(const std::string &end) { hot_queried_end_ = end; }
  void UpdateHotspot(const std::string& ns, const std::string& ne) {
    hot_queried_start_ = ns;
    hot_queried_end_ = ne;
  }
  bool BGFlushIdle();
  bool BGCompactionIdle();
  bool IsBtree() { return false; }
  bool IsLSMT() { return true; }
  int WriteBatchSize() { return write_batch_size_; }
  void PrintAllPages() {}
  void PrintAllData(std::string, std::string, std::string);
 
 private:
  void Open(std::map<std::string, std::string>* params);

  leveldb::DB* db_;
  leveldb::WriteOptions write_options_;
  bool is_lsmt_;
  std::string hot_queried_start_;
  std::string hot_queried_end_;
  int write_batch_size_;
  std::atomic<uint64_t> interval_;
};

} // namespace ycsbc

