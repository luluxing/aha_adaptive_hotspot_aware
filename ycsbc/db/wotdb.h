#include "aha_tree/aha_tree.h"
#include "../core/db.h"
#include "../core/properties.h"

namespace ycsbc{

class WotDB : public DB {
 public:
  WotDB(std::map<std::string, std::string>* options);
  ~WotDB() { delete tree_; }
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
  void PrintAllPages();
  void RemoveTree(bool concurrent);
  void AddTree(bool add);
  void EnableTreeInsertion() { tree_insert_ = true; }
  void EnableBufferInsertion() { tree_insert_ = false; }
  void SetHotspotStart(const std::string &start) {
    if (start.empty()) {
      return;
    }
    hot_queried_start_ = start;
  }
  void SetHotspotEnd(const std::string &end) {
    if (end.empty()) {
      return;
    }
    hot_queried_end_ = end;
  }
  void UpdateHotspot(const std::string& ns, const std::string& ne) {
    hot_queried_start_ = ns;
    hot_queried_end_ = ne;
    fprintf(stdout, "Index hotspot is updated to %s - %s\n", ns.c_str(), ne.c_str());
  }
  bool BGFlushIdle();
  bool BGCompactionIdle();
  bool IsBtree() { return false; }
  bool IsLSMT() { return is_lsmt_; }
  int WriteBatchSize() { return write_batch_size_; }
  void PrintAllData(std::string, std::string, std::string);
 
 private:
  WOT_NAMESPACE::AhaTree* tree_;
  bool is_lsmt_;
  std::string hot_queried_start_;
  std::string hot_queried_end_;
  bool tree_insert_ = false;
  std::atomic<uint64_t> flush_id_;
  std::atomic<int> interval_;
  int write_batch_size_;
  std::string tmp_key_;
};

} // namespace ycsbc

