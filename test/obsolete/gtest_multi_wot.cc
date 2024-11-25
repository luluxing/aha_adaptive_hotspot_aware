#include "wot_adapt.h"
#include "util.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace WOT_NAMESPACE;

namespace {

int FLAGS_key_prefix = 0;

class KeyBuffer {
 public:
  KeyBuffer() {
    assert(FLAGS_key_prefix < sizeof(buffer_));
    memset(buffer_, 'a', FLAGS_key_prefix);
  }
  KeyBuffer& operator=(KeyBuffer& other) = delete;
  KeyBuffer(KeyBuffer& other) = delete;

  void Set(int k) {
    std::snprintf(buffer_ + FLAGS_key_prefix,
                  sizeof(buffer_) - FLAGS_key_prefix, "%016d", k);
  }

  Slice slice() const { return Slice(buffer_, FLAGS_key_prefix + 16); }

 private:
  char buffer_[1024];
};

class MultiTest : public testing::Test {
 protected:
  void SetUp() override {
    LoadOptions("../src/options.spec", &options_);
    tree_ = new BplusTree(&options_, /*is_lsmt*/false, /*is_btree*/false);
  }

  void TearDown() override {
    delete tree_;
  }

  BplusTree* tree_;
  std::map<std::string, std::string> options_;
  
};

std::map<uint32_t, std::string> real;

void AddData(BplusTree* tree_, int start, int length, int num) {
  KeyBuffer key, key2;
  WriteBatch batch;
  for (int i = start; i < start + length; i++) {
    key.Set(i);
    std::string v = std::string(512, 'a' + (i % 26));
    batch.Clear();
    batch.Put(key.slice(), Slice(v));
    // std::cout << key.slice().ToString() << std::endl;
    Status s = tree_->Insert(&batch);
    // real[i] = v;
  }

  tree_->AdaptToRead();
  // Sleep and wait for all compaction to finish
  std::this_thread::sleep_for(std::chrono::seconds(3));

  EXPECT_EQ(tree_->TreeHeight(), 2);
  start = 1;
  int end = num - 1;
  key.Set(start);
  key2.Set(end);
  std::cout << "Begin range query\n";
  auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
  std::map<std::string, std::string> result;
  for (iter->Seek(); iter->Valid(); iter->Next()) {
    // std::cout << iter->key() << std::endl;
    result[iter->key()] = iter->value();
  }
  delete iter;
  auto i = result.begin();
  auto it = real.lower_bound(start);
  for (; it != real.end() && i != result.end(); it++) {
    key.Set(it->first);
    // std::cout << it->first << "," << i->first << std::endl;
    EXPECT_EQ(key.slice().ToString(), i->first);
    EXPECT_EQ(it->second, i->second);
    i++;
  }
}

TEST_F(MultiTest, MultiInsertionTest) {
  int thread_num = std::stoi(options_["thread_num"]);
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);

  for (int i = 0; i < num; i++) {
    std::string v = std::string(512, 'a' + (i % 26));
    real[i] = v;
  }

  int start = 0;
  int length = num / thread_num;
  std::vector<std::thread> threads;
  for (int i = 0; i < thread_num; i++) {
    std::thread t(AddData, tree_, start, length, num);
    start += length;
    threads.push_back(std::move(t));
  }
  for (int i = 0; i < threads.size(); i++) {
    threads[i].join();
    std::cout << "Done\n";
  }  
}

}