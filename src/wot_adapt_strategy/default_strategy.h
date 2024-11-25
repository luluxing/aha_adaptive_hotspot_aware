#ifndef WOT_ADAPT_STRATEGY_H
#define WOT_ADAPT_STRATEGY_H

#include <oneapi/tbb.h>
#include <set>

#include "leveldb/memtable.h"
#include "leveldb/port/port_stdcxx.h"
#include "wot_buf_mgr/tree_page.h"

namespace WOT_NAMESPACE {

class BplusTree;
class Node;
class LevelDBLSMT;
class FileMetaData;

class DefaultAdapt {
 public:
  DefaultAdapt(BplusTree* tree);
  DefaultAdapt() { tree_ = nullptr;}
  ~DefaultAdapt() = default;

  void AdaptLSMT();
  virtual void AdaptWOT();
  // Need to check between enqueue and adapt, the node has not been adapted
  virtual bool CanProceedAdapt(Node* node, int* adapt_node);

  void SetHotKeys(const Slice& low, const Slice& up);

  virtual bool HasTargetData(LevelDBLSMT*);
  virtual void AddWorkToQueue(int node_id, int node_level=0);
  virtual int GetWorkQueueSize();
  virtual void ScheduleLSMTAdapt();
  void ScheduleMemAdapt();
  void ScheduleImmAdapt();
  void ResetWorkQueueSize(int size);

 protected:
  BplusTree* tree_;
  port::Mutex adapt_mutex_;
  std::string low_hot_key_;
  std::string up_hot_key_;
  int queue_size_ = 10;

  virtual void ClearWorkQueue();
  virtual bool GetWorkFromQueue(int* node_id);

  virtual void CompactRootBufferForFlush(int* level_of_files,
                                         std::vector<std::shared_ptr<FileMetaData>>* files);
  virtual void CompactNodeBufferForFlush(Node* cur_node, Page page, int* level_of_files,
                        std::vector<std::shared_ptr<FileMetaData>>* files);
  Node* FindNodePath(uint32_t adapt_node, Node* node,
                    std::vector<std::pair<Node*, int>>* nodes_pool,
                    uint32_t* pool_idx);
 private:
  oneapi::tbb::concurrent_bounded_queue<int> adapt_work_queue_;

  // Empricially set the flush count to 5
  int flush_count_ = 5;
  void BulkloadLeafnode(uint32_t node_id);


};

typedef std::pair<int, int> WeightNodePair;

class CompareWeight {
 public:
    bool operator()(const WeightNodePair& u, const WeightNodePair& v) const {
      if (u.first > v.first) return true; // u has higher weight
      if (u.first < v.first) return false;
      if (u.second < v.second) return true; // u has smaller node id
      return false;
    }
};

class PrioritySizeLimitAdapt : public DefaultAdapt {
 public:
  PrioritySizeLimitAdapt(BplusTree* tree) : DefaultAdapt(tree) {}
  PrioritySizeLimitAdapt() : DefaultAdapt() {}

//  protected:
  int GetWorkQueueSize() override;
  void ClearWorkQueue() override;
  void AddWorkToQueue(int node_id, int node_level) override;
  bool GetWorkFromQueue(int* node_id) override;
  void ScheduleLSMTAdapt() override;

  oneapi::tbb::concurrent_priority_queue<WeightNodePair, CompareWeight>
    work_priority_queue_;
};

class PriorityNoDupAdapt : public PrioritySizeLimitAdapt {
 public:
  PriorityNoDupAdapt(BplusTree* tree) : PrioritySizeLimitAdapt(tree) {}
  PriorityNoDupAdapt() : PrioritySizeLimitAdapt() {}

//  protected:
  void ClearWorkQueue() override;
  void AddWorkToQueue(int node_id, int node_level) override;
  bool GetWorkFromQueue(int* node_id) override;

  // Reuse the following functions from PrioritySizeLimitAdapt
  // void ScheduleLSMTAdapt() override;
  // int GetWorkQueueSize() override;

 private:
  std::set<int> work_set_;
};

// Compact only the nodes' buffer that overlap with the hotspot range.
// If no such file exists, mark this buffer as free from hotspot range.
// When new file is added to buffer, we unset the bit as we do not check
// precisely whether the file is in the hotspot range.
class HotOnlyAdapt : public PriorityNoDupAdapt {
 public:
  HotOnlyAdapt(BplusTree* tree) : PriorityNoDupAdapt(tree) {
    fprintf(stdout, "HotOnlyAdapt bases on PriorityNoDupAdapt\n");
  }

  bool HasTargetData(LevelDBLSMT* lsmt) override;

 protected:
  void CompactRootBufferForFlush(int* level_of_files,
                      std::vector<std::shared_ptr<FileMetaData>>* files) override;
  void CompactNodeBufferForFlush(Node* cur_node, Page page, int* level_of_files,
                      std::vector<std::shared_ptr<FileMetaData>>* files) override;
};

// Instead of growing the height of the target leaf node, we split this leaf node
// into multiple leaf pages and install them into the parent node. This keeps the
// tree in a balanced state.
class BalancedAdapt : public HotOnlyAdapt {
 public:
  BalancedAdapt(BplusTree* tree) : HotOnlyAdapt(tree) {}

 protected:
  void AdaptWOT() override;
  bool CanProceedAdapt(Node* node, int* adapt_node) override;

 private:
  void NewFindNodePath(uint32_t adapt_node, Node* node,
                        std::vector<uint32_t>* nodes_pool);

};

// Instead of retrieving work queue to decide which node to adapt, we adapt the
// entire hotspot range from low to high. We may reuse the path from root to the
// node above leaf. We grow the tree height of the target leaf node.
class EagerAdaptIncreaseHeight : public DefaultAdapt {
 public:
  EagerAdaptIncreaseHeight(BplusTree* tree) : DefaultAdapt(tree) {}


 protected:
  
};

// Instead of retrieving work queue to decide which node to adapt, we adapt the
// entire hotspot range from low to high. We may reuse the path from root to the
// node above leaf. We DO NOT grow the tree height of the target leaf node same as
// balanced adapt.
class EagerAdaptSameHeight : public DefaultAdapt {
 public:
  EagerAdaptSameHeight(BplusTree* tree) : DefaultAdapt(tree) {}


 protected:
  
};

} // namespace WOT_NAMESPACE

#endif // WOT_ADAPT_STRATEGY_H