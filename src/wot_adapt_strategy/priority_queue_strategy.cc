#include "default_strategy.h"
#include "wot_index.h"

namespace WOT_NAMESPACE {

int PrioritySizeLimitAdapt::GetWorkQueueSize() {
  adapt_mutex_.Lock();
  int ret = work_priority_queue_.size();
  adapt_mutex_.Unlock();
  return ret;
}

void PrioritySizeLimitAdapt::ClearWorkQueue() {
  adapt_mutex_.Lock();
  work_priority_queue_.clear();
  adapt_mutex_.Unlock();
}

void PrioritySizeLimitAdapt::AddWorkToQueue(int node_id, int node_level) {
  if (GetWorkQueueSize() > 10) {
    return;
  }

  WeightNodePair pair = std::make_pair(node_level, node_id);
  work_priority_queue_.push(pair);
}

bool PrioritySizeLimitAdapt::GetWorkFromQueue(int* node_id) {
  WeightNodePair pair;
  if (work_priority_queue_.try_pop(pair)) {
    *node_id = pair.second;
    return true;
  }
  return false;
}

void PrioritySizeLimitAdapt::ScheduleLSMTAdapt() {
  // AddWorkToQueue(-1, 0);
  assert(tree_->TreeHeight() > 1);
  tree_->AddLSMTCompactionWork();
}

void PriorityNoDupAdapt::ClearWorkQueue() {
  adapt_mutex_.Lock();
  work_priority_queue_.clear();
  work_set_.clear();
  adapt_mutex_.Unlock();
}

void PriorityNoDupAdapt::AddWorkToQueue(int node_id, int node_level) {
  adapt_mutex_.Lock();
  if (work_set_.count(node_id) > 0) {
    adapt_mutex_.Unlock();
    return;
  }
  work_set_.insert(node_id);
  adapt_mutex_.Unlock();
  // WeightNodePair pair = std::make_pair(node_level, node_id);
  work_priority_queue_.emplace(std::make_pair(node_level, node_id));
}

bool PriorityNoDupAdapt::GetWorkFromQueue(int* node_id) {
  WeightNodePair pair;
  if (work_priority_queue_.try_pop(pair)) {
    *node_id = pair.second;
    adapt_mutex_.Lock();
    auto it = work_set_.find(pair.second);
    if (it != work_set_.end()) {
      work_set_.erase(it);
    }
    adapt_mutex_.Unlock();
    return true;
  }
  return false;
}

} // namespace WOT_NAMESPACE