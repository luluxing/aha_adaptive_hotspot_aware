#include "wot_adapt.h"
#include "util.h"
#include <gtest/gtest.h>

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

class LSMTTest : public testing::Test {
 protected:
  void SetUp() override {
    LoadOptions("../src/options.spec", &options_);
    tree_ = new BplusTree(&options_, /*is_lsmt*/true, /*is_btree*/false);
  }

  void TearDown() override {
    delete tree_;
  }

  BplusTree* tree_;
  std::map<std::string, std::string> options_;
};

TEST_F(LSMTTest, OrderedInsertionTest) {
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

TEST_F(LSMTTest, ReverseOrderedInsertionTest) {
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

TEST_F(LSMTTest, RandomInsertionTest) {
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

} // namespace