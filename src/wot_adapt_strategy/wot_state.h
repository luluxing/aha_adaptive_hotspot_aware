#ifndef WOT_STATE_WOT_NAMESAPCE_H_
#define WOT_STATE_WOT_NAMESAPCE_H_

#include "leveldb/include/iterator.h"
#include "leveldb/memtable.h"
#include "leveldb/port/port_stdcxx.h"
#include "wot_buf_mgr/tree_page.h"

namespace WOT_NAMESPACE {

class BplusTree;
class Node;

class BplusTreeState {
 public:
  // Template method
  void NewIterator(BplusTree* tree, std::vector<Iterator*>* iter_vec,
                        const Slice& low, const Slice& up, MemTable** mem, 
                        MemTable** imm, std::vector<int>* locked_nodes,
                        const Comparator* user_comparator, int* non_page_num);

  bool HotScan(BplusTree* tree, const Slice& low, const Slice& up);

  bool has_buffer_page;

 protected:
  void ChangeState(BplusTree* tree, BplusTreeState* state);
  void FindChildrenNodes(BplusTree* tree, uint32_t pg_id, const Slice& low,
                          const Slice& up, std::vector<uint32_t>& nodes_vec,
                          const Comparator* user_comparator);

  virtual void DoAddMemToIter(BplusTree* tree, bool in_hotspot,
                              std::vector<Iterator*>* iter_vec, 
                              MemTable**) = 0;
  virtual void DoAddImmToIter(BplusTree* tree, bool in_hotspot,
                              std::vector<Iterator*>* iter_vec, 
                              MemTable**) = 0;
  virtual void DoMayAdaptMem(BplusTree* tree, bool, int) = 0;
  virtual void DoAddLSMTtoIter(BplusTree* tree, bool, std::vector<Iterator*>* iter_vec) = 0;
  virtual void DoAddWOTtoIter(BplusTree* tree, int node_level, Node* node,
                              bool is_hot, std::vector<Iterator*>* iter_vec) = 0;
  virtual void DoMayTriggerWOTAdapt(BplusTree* tree, bool in_hotspot) = 0;
  virtual void DoValidate(BplusTree* tree, bool in_hotspot,
                          int non_page_num, const Comparator* user_comparator) = 0;

  virtual void LockLSMT(BplusTree* tree, std::vector<int>* locked_nodes);

};

// This state assumes the majority of the mixed workload is read.
// We adapt to read-optimized index during range queries and
// distinguish two insertions from hot and cold data, with
// hot data into read-optimized index and cold data into buffer.
class BplusTreeReadOptState : public BplusTreeState {
 public:
  BplusTreeReadOptState(bool proactive_validation)
  : proactive_validation_(proactive_validation),
    validate_cnt_(0),
    pure_page_cnt_(0) {}

 protected:
  void DoAddMemToIter(BplusTree* tree, bool in_hotspot,
                      std::vector<Iterator*>* iter_vec, 
                      MemTable**) override;
  void DoAddImmToIter(BplusTree* tree, bool in_hotspot,
                      std::vector<Iterator*>* iter_vec, 
                      MemTable**) override;
  void DoMayAdaptMem(BplusTree* tree, bool, int) override;
  void DoAddLSMTtoIter(BplusTree* tree, bool, std::vector<Iterator*>* iter_vec) override;
  void DoAddWOTtoIter(BplusTree* tree, int node_level, Node* node,
                      bool is_hot, std::vector<Iterator*>* iter_vec) override;
  void DoMayTriggerWOTAdapt(BplusTree* tree, bool in_hotspot) override;
  void DoValidate(BplusTree* tree, bool in_hotspot,
                  int non_page_num, const Comparator* user_comparator) override;
  bool proactive_validation_;

 private:
  void ValidateRange(BplusTree* tree, const Comparator* user_comparator);

  const int validate_group_ = 1000;
  const double validate_threshold_ = 0.8;
  bool validation_triggered_ = false;

  port::Mutex validate_mtx_;
  std::atomic<uint64_t> validate_cnt_;
  std::atomic<uint64_t> pure_page_cnt_;
};

// This state assumes the majority of the mixed workload is write.
// We DO NOT adapt to read-optimized index during range queries and
// DO NOT distinguish two insertions from hot and cold data, with
// ALL data inserted into buffer.
class BplusTreeWriteOptState : public BplusTreeState {
 public:
  BplusTreeWriteOptState() {}

 protected:
  void DoAddMemToIter(BplusTree* tree, bool in_hotspot,
                      std::vector<Iterator*>* iter_vec, 
                      MemTable**) override;
  void DoAddImmToIter(BplusTree* tree, bool in_hotspot,
                      std::vector<Iterator*>* iter_vec, 
                      MemTable**) override;
  void DoMayAdaptMem(BplusTree* tree, bool, int) override;
  void DoAddLSMTtoIter(BplusTree* tree, bool, std::vector<Iterator*>* iter_vec) override;
  void DoAddWOTtoIter(BplusTree* tree, int node_level, Node* node,
                      bool is_hot, std::vector<Iterator*>* iter_vec) override;
  void DoMayTriggerWOTAdapt(BplusTree* tree, bool in_hotspot) override;
  void DoValidate(BplusTree* tree, bool in_hotspot,
                  int non_page_num, const Comparator* user_comparator) override;
};

class BufferTreeState : public BplusTreeReadOptState {
  public:
    BufferTreeState()
    : BplusTreeReadOptState(true) {}
  
    void SetValidation(bool validation) {
      proactive_validation_ = validation;
    }

  protected:
    void LockLSMT(BplusTree* tree, std::vector<int>* locked_nodes) override {}
};


} // namespace WOT_NAMESPACE

#endif