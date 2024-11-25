#include "leveldb/include/slice.h"
#include "wot_adapt_strategy/default_strategy.h"
#include "gtest/gtest.h"

using namespace WOT_NAMESPACE;

namespace {

class PriorityQueue : public testing::Test {
 protected:
  void SetUp() override {
    queue1_ = new PrioritySizeLimitAdapt();
    queue2_ = new PriorityNoDupAdapt();
  }

  void TearDown() override {
    delete queue1_;
    delete queue2_;
  }

  PrioritySizeLimitAdapt* queue1_;
  PriorityNoDupAdapt* queue2_;
};

TEST_F(PriorityQueue, EmptyConstructor) {
  EXPECT_EQ(queue1_->GetWorkQueueSize(), 0);
  EXPECT_EQ(queue2_->GetWorkQueueSize(), 0);
}

TEST_F(PriorityQueue, InsertionSeqAndReadTest) {
  int num = 9;
  for (int i = 0; i < num; i++) {
    queue1_->AddWorkToQueue(i, i);
    queue2_->AddWorkToQueue(i, i);
  }
  
  int node;
  for (int i = 0; i < num; i++) {
    queue1_->GetWorkFromQueue(&node);
    EXPECT_EQ(node, i);
    queue2_->GetWorkFromQueue(&node);
    EXPECT_EQ(node, i);
  }
}

TEST_F(PriorityQueue, InsertionAndReadTest) {
  int num = 9;
  std::map<int, int> level_node_map;
  for (int i = 0; i < num; i++) {
    int level = rand() % 100;
    if (level_node_map.count(level) > 0) {
      i--;
      continue;
    }
    level_node_map[level] = i;
    queue1_->AddWorkToQueue(i, level);
    queue2_->AddWorkToQueue(i, level);
  }
  
  int node;
  auto it = level_node_map.begin();
  for (int i = 0; i < num; i++) {
    queue1_->GetWorkFromQueue(&node);
    EXPECT_EQ(node, it->second);
    queue2_->GetWorkFromQueue(&node);
    EXPECT_EQ(node, it->second);
    it++;
  }
}

TEST_F(PriorityQueue, InsertionDupTest) {
  int num = 15;
  for (int i = 0; i < num; i++) {
    queue1_->AddWorkToQueue(3, 3);
    queue2_->AddWorkToQueue(3, 3);
  }
  EXPECT_EQ(queue1_->GetWorkQueueSize(), 11);
  EXPECT_EQ(queue2_->GetWorkQueueSize(), 1);
}

} // namespace