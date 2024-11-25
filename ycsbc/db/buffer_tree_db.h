#include "buffer_tree/buffer_tree.h"
#include "../core/db.h"
#include "../core/properties.h"

namespace ycsbc{

#ifdef INCLUDE_BUFFERTREE
class BufferTreeDB : public DB {
 public:
  BufferTreeDB(std::map<std::string, std::string>* options);
  ~BufferTreeDB() { delete tree_; }
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
  bool BGFlushIdle();
  bool BGCompactionIdle();
  bool IsBtree() { return false; }
  bool IsLSMT() { return is_lsmt_; }
  int WriteBatchSize() { return write_batch_size_; }
  void PrintAllData(std::string, std::string, std::string);
 
 private:
  WOT_NAMESPACE::BufferTree* tree_;
  bool is_lsmt_;
  bool is_buffer_tree_;
  std::string hot_queried_start_;
  std::string hot_queried_end_;
  bool tree_insert_ = false;
  std::atomic<uint64_t> flush_id_;
  std::atomic<int> interval_;
  int write_batch_size_;
  std::string tmp_key_;
};

#endif
} // namespace ycsbc

