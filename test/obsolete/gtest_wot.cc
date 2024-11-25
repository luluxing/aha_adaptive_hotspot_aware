#include "wot_adapt.h"
#include "util.h"
#include <gtest/gtest.h>
#include <thread>

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

class WOTTest : public testing::Test {
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

// make level-1 limit: 1MB=1048576
// make file size: 1MB=1048576
// make memtable size: 1MB=1048576
// make config::kL0_CompactionTrigger=2
// make PAGE_SIZE 256

TEST_F(WOTTest, OrderedInsertionTest) {
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);
  std::map<uint32_t, std::string> res;
  // Each internal tree node can hold 3 index entries
  for (int i = 1; i < num; i++) {
    key.Set(i);
    std::string v = std::string(512, 'a' + (i % 26));
    // std::cout << key.slice().ToString() << "=>" << std::endl;
    tree_->Insert(key.slice().ToString(), Slice(v).ToString());
    res[i] = v;
  }
  // tree_->Print();
  EXPECT_EQ(tree_->TreeHeight(), 2);
  int start = 1;
  int end = num - 1;
  key.Set(start);
  key2.Set(end);
  
  auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
  std::map<std::string, std::string> result;
  for (iter->Seek(); iter->Valid(); iter->Next()) {
    // std::cout << iter->key() << std::endl;
    result[iter->key()] = iter->value();
  }

  auto i = result.begin();
  auto it = res.lower_bound(start);
  for (; it != res.end() && i != result.end(); it++) {
    key.Set(it->first);
    // std::cout << it->first << std::endl;
    EXPECT_EQ(key.slice().ToString(), i->first);
    EXPECT_EQ(it->second, i->second);
    i++;
  }
}

TEST_F(WOTTest, ReverseOrderedInsertionTest) {
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);
  std::map<uint32_t, std::string> res;
  // Each internal tree node can hold 3 index entries
  for (int i = num; i > 0; i--) {
    key.Set(i);
    std::string v = std::string(512, 'a' + (i % 26));
    // std::cout << key.slice().ToString() << "=>" << std::endl;
    tree_->Insert(key.slice().ToString(), Slice(v).ToString());
    res[i] = v;
  }
  // tree_->Print();
  EXPECT_EQ(tree_->TreeHeight(), 2);
  int start = 1;
  int end = num - 1;
  key.Set(start);
  key2.Set(end);
  
  auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
  std::map<std::string, std::string> result;
  for (iter->Seek(); iter->Valid(); iter->Next()) {
    // std::cout << iter->key() << std::endl;
    result[iter->key()] = iter->value();
  }

  auto i = result.begin();
  auto it = res.lower_bound(start);
  for (; it != res.end() && i != result.end(); it++) {
    key.Set(it->first);
    // std::cout << it->first << std::endl;
    EXPECT_EQ(key.slice().ToString(), i->first);
    EXPECT_EQ(it->second, i->second);
    i++;
  }
}

// When random seed is set to 10, there is a strange bug:
// If the above two tests are commented, everything is fine.
// Otherwise, this test blocks in the middle and gdb cannot
// debug because it works fine with debugger.
TEST_F(WOTTest, RandomInsertionTest) {
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);
  srand(1);
  std::map<uint32_t, std::string> res;
  // Each internal tree node can hold 3 index entries
  for (int i = 0; i < num; i++) {
    int k = rand() % 100000;
    key.Set(k);
    std::string v = std::string(512, 'a' + (i % 26));
    tree_->Insert(key.slice().ToString(), Slice(v).ToString());
    res[k] = v;
  }
  // tree_->Print();
  EXPECT_EQ(tree_->TreeHeight(), 2);
  int start = 1;
  int end = num;
  key.Set(start);
  key2.Set(end);
  
  auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
  std::map<std::string, std::string> result;
  for (iter->Seek(); iter->Valid(); iter->Next()) {
    // std::cout << iter->key() << std::endl;
    result[iter->key()] = iter->value();
  }

  auto i = result.begin();
  auto it = res.lower_bound(start);
  for (; it != res.end() && i != result.end(); it++) {
    key.Set(it->first);
    // std::cout << it->first << std::endl;
    EXPECT_EQ(key.slice().ToString(), i->first);
    EXPECT_EQ(it->second, i->second);
    i++;
  }
}

TEST_F(WOTTest, AdaptReadTest) {
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);
  srand(1);
  std::map<uint32_t, std::string> ins;
  // Each internal tree node can hold 3 index entries
  for (int i = 0; i < num; i++) {
    int k = rand() % 100000;
    key.Set(k);
    std::string v = std::string(512, 'a' + (i % 26));
    tree_->Insert(key.slice().ToString(), Slice(v).ToString());
    ins[k] = v;
  }
  // tree_->Print();
  tree_->AdaptToRead();
  EXPECT_EQ(tree_->TreeHeight(), 3);
  int scan_num = 300;//2;
  int query_num = 10000;//num/2;
  int start = 0, end;
  
  for (int q = 0; q < scan_num; q++) {
    end = start + query_num;
    key.Set(start);
    key2.Set(end);
    std::map<std::string, std::string> result;
    auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
    for (iter->Seek(); iter->Valid(); iter->Next()) {
      // std::cout << iter->key() << std::endl;
      result[iter->key()] = iter->value();
    }
    delete iter;
    auto i = result.begin();
    auto it = ins.lower_bound(start);
    for (; it != ins.end() && i != result.end(); it++) {
      key.Set(it->first);
      // std::cout << it->first << std::endl;
      EXPECT_EQ(key.slice().ToString(), i->first);
      EXPECT_EQ(it->second, i->second);
      i++;
    }
    
    start = end;
  }
  
  
  // tree_->Print();

}

TEST_F(WOTTest, SortedIteratorTest) {
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);
  srand(1);
  WriteBatch batch;
  std::map<uint32_t, std::string> ins;
  for (int i = 0; i < num; i++) {
    int k = rand() % 1000000;
    key.Set(k);
    std::string v = std::string(512, 'a' + (i % 26));
    batch.Clear();
    batch.Put(key.slice(), Slice(v));
    Status s = tree_->Insert(&batch);
    ins[k] = v;
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));
  tree_->PrintStat();
  EXPECT_EQ(tree_->TreeHeight(), 2);
  int start = 0;
  int end = 1000000;
  key.Set(start);
  key2.Set(end);
  std::cout << "Begin range query\n";
  
  auto iter = tree_->NewSortedTreeIterator();
  iter->Seek(key.slice());
  auto i = ins.lower_bound(start);
  KeyBuffer k3;
  for (; iter->Valid(); iter->Next()) {
    // std::cout << iter->key() << std::endl;
    k3.Set(i->first);
    EXPECT_EQ(k3.slice().ToString(), iter->key());
    EXPECT_EQ(i->second, iter->value());
    i++;
  }
  delete iter;
  tree_->PrintStat();
}

} // namespace