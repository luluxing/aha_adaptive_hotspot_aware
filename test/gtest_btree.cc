#include "btree/btree.h"
#include "util/helper.h"
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

class BtreeTest : public testing::Test {
 protected:
  void SetUp() override {
    LoadOptions("../src/options.spec", &options_);
    btree_ = new Btree(&options_);
  }

  void TearDown() override {
    delete btree_;
  }

  Btree* btree_;
  std::map<std::string, std::string> options_;
};

// TEST_F(BtreeTest, ConstructionTest) {
//   EXPECT_EQ(btree_->RootPageId(), 0);
//   EXPECT_EQ(btree_->TreeHeight(), 1);
// }

// TEST_F(BtreeTest, InsertAndReadRootTest) {
//   KeyBuffer kb;
//   for (int i = 52; i > 0; i--) {
//     kb.Set(i);
//     btree_->Insert(kb.slice(), Slice(std::string(128, 'a'+(i%26))));
//   }
//   Status s;
//   for (int i = 52; i > 0; i--) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     EXPECT_TRUE(s.ok());
//     EXPECT_EQ(val, Slice(std::string(128, 'a'+(i%26))));
//   }
//   for (int i = 300; i > 200; i--) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     EXPECT_FALSE(s.ok());
//   }
// }

// TEST_F(BtreeTest, LeafNodeSplitTest) {
//   KeyBuffer kb;
//   Status s;
//   // Key: 16B, value: 128B. 8kB page can hold 52 pairs
//   for (int i = 60; i > 0; i--) {
//     kb.Set(i);
//     s = btree_->Insert(kb.slice(), Slice(std::string(128, 'a'+(i%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   EXPECT_EQ(btree_->TreeHeight(), 2);
//   for (int i = 1; i <= 60; i++) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     EXPECT_TRUE(s.ok());
//     EXPECT_EQ(val, Slice(std::string(128, 'a'+(i%26))));
//   }
// }

// TEST_F(BtreeTest, OrderedInsertionInternalNodeSplitTest) {
//   KeyBuffer kb;
//   Status s;
//   // Key: 16B, value: 512B. 8kB page can hold 15 pairs.
//   // Internal page can hold 291 pairs.
//   // need 15 * 291 = 4365 pairs to trigger internal node split
//   int num = 4000;
//   for (int i = 1; i <= num; i++) {
//     kb.Set(i);
//     s = btree_->Insert(kb.slice(), Slice(std::string(512, 'a'+(i%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   EXPECT_EQ(btree_->TreeHeight(), 3);
//   for (int i = 1; i <= num; i++) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     EXPECT_TRUE(s.ok());
//     EXPECT_EQ(val, Slice(std::string(512, 'a'+(i%26))));
//   }
// }

// TEST_F(BtreeTest, ReverseInsertionInternalNodeSplitTest) {
//   KeyBuffer kb;
//   Status s;
//   // Key: 16B, value: 512B. 8kB page can hold 15 pairs.
//   // Internal page can hold 291 pairs.
//   // need 15 * 291 = 4365 pairs to trigger internal node split
//   int num = 4000;
//   for (int i = num; i > 0; i--) {
//     kb.Set(i);
//     s = btree_->Insert(kb.slice(), Slice(std::string(512, 'a'+(i%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   EXPECT_EQ(btree_->TreeHeight(), 3);
//   for (int i = 1; i <= num; i++) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     EXPECT_TRUE(s.ok());
//     EXPECT_EQ(val, Slice(std::string(512, 'a'+(i%26))));
//   }
// }

// TEST_F(BtreeTest, RandomInsertionInternalNodeSplitTest) {
//   KeyBuffer kb;
//   Status s;
//   srand(1);
//   std::map<uint32_t, uint32_t> pairs;
//   for (int i = 500; i > 0; i--) {
//     int r = rand() % 500;
//     kb.Set(r);
//     pairs[r] = r;
//     // std::cout << kb.slice().ToString() << std::endl;
//     s = btree_->Insert(kb.slice(), Slice(std::string(512, 'a'+(i%26))));
//     EXPECT_TRUE(s.ok());
//   }
// }

// TEST_F(BtreeTest, ReadNonExistTest) {
//   KeyBuffer kb;
//   Status s;
//   // Key: 16B, value: 128B. 8kB page can hold 52 pairs
//   for (int i = 60; i > 0; i--) {
//     kb.Set(i);
//     s = btree_->Insert(kb.slice(), Slice(std::string(128, 'a'+(i%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   EXPECT_EQ(btree_->TreeHeight(), 2);
//   for (int i = 61; i <= 70; i++) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     EXPECT_FALSE(s.ok());
//   }
// }

// TEST_F(BtreeTest, DeleteTest) {
//   KeyBuffer kb;
//   Status s;
//   // Key: 16B, value: 128B. 8kB page can hold 52 pairs
//   for (int i = 60; i > 0; i--) {
//     kb.Set(i);
//     s = btree_->Insert(kb.slice(), Slice(std::string(128, 'a'+(i%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   for (int j = 1; j < 60; j++) {
//     kb.Set(j);
//     s = btree_->Delete(kb.slice().ToString());
//     EXPECT_TRUE(s.ok());
//     j++;
//   }
//   for (int i = 1; i <= 60; i++) {
//     kb.Set(i);
//     std::string val;
//     s = btree_->Query(kb.slice().ToString(), &val);
//     if (i % 2 == 0)
//       EXPECT_TRUE(s.ok());
//     else
//       EXPECT_FALSE(s.ok());
//   }
// }

// TEST_F(BtreeTest, ShortRangeScanTest) {
//   KeyBuffer kb, kb2;
//   Status s;
//   srand(1);
//   std::map<uint32_t, uint32_t> pairs;
//   for (int i = 500; i > 0; i--) {
//     int r = rand() % 500;
//     kb.Set(r);
//     pairs[r] = r;
//     s = btree_->Insert(kb.slice(), Slice(std::string(512, 'a'+(r%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   kb.Set(1);
//   kb2.Set(5);
//   Iterator* iter = btree_->NewBtreeIterator();
//   iter->Seek(kb.slice());
//   auto it = pairs.lower_bound(1);
//   for (; iter->Valid(); iter->Next()) {
//     kb.Set(it->first);
//     EXPECT_EQ(iter->key(), kb.slice());
//     EXPECT_EQ(iter->value(), Slice(std::string(512, 'a'+((it->second)%26))));
//     it++;
//   }
// }

// TEST_F(BtreeTest, LongRangeScanTest) {
//   KeyBuffer kb, kb2;
//   Status s;
//   srand(1);
//   std::map<uint32_t, uint32_t> pairs;
//   for (int i = 500; i > 0; i--) {
//     int r = rand() % 500;
//     kb.Set(r);
//     pairs[r] = r;
//     s = btree_->Insert(kb.slice(), Slice(std::string(512, 'a'+(r%26))));
//     EXPECT_TRUE(s.ok());
//   }
//   int start = 1, end = 610;
//   kb.Set(start);
//   kb2.Set(end);
//   Iterator* iter = btree_->NewBtreeIterator());
//   iter->Seek(kb.slice());
//   auto it = pairs.lower_bound(start);
//   for (; iter->Valid(); iter->Next()) {
//     kb.Set(it->first);
//     EXPECT_EQ(iter->key(), kb.slice());
//     EXPECT_EQ(iter->value(), Slice(std::string(512, 'a'+((it->second)%26))));
//     it++;
//   }
// }

// TEST_F(BtreeTest, LargeDataTest) {
//   KeyBuffer key, key2;
//   int num = std::stoi(options_["test_size"]);
//   srand(1);
//   Status s;
//   std::map<uint32_t, std::string> ins;
//   // Each internal tree node can hold 3 index entries
//   for (int i = 0; i < num; i++) {
//     int k = rand() % 100000;
//     key.Set(k);
//     std::string v = std::string(512, 'a' + (i % 26));
//     s = btree_->Insert(key.slice(), Slice(v));
//     EXPECT_TRUE(s.ok());
//     ins[k] = v;
//   }
//   int start = 1, end = num;
//   key.Set(start);
//   key2.Set(end);
//   Iterator* iter = btree_->NewBtreeIterator();
//   iter->Seek(key.slice());
//   auto it = ins.lower_bound(start);
//   for (; iter->Valid(); iter->Next()) {
//     key.Set(it->first);
//     EXPECT_EQ(iter->key(), key.slice());
//     EXPECT_EQ(iter->value(), it->second);
//     it++;
//   }
//   btree_->PrintStat();
// }

std::map<uint32_t, std::string> real;

void ReadData(Btree* tree, int num) {
  KeyBuffer key, key2;
  int start = 0, end = num;
  key.Set(start);
  key2.Set(end);
  std::map<std::string, std::string> result;
  Iterator* iter = tree->NewBtreeIterator();
  do {
    iter->Seek(key.slice());
    result.clear();
    for (; iter->Valid(); iter->Next()) {
      result[iter->key().ToString()] = iter->value().ToString();
    }
  } while (!iter->status().ok());
  auto it = real.lower_bound(start);
  EXPECT_EQ(real.size(), result.size());
  for (auto r = result.begin(); r != result.end(); r++) {
    key.Set(it->first);
    EXPECT_EQ(r->first, key.slice().ToString());
    EXPECT_EQ(r->second, it->second);
    if (r->first != key.slice().ToString()) {
      std::cout << r->first << std::endl;
      break;
    }
    it++;
  }
  delete iter;
}

// TEST_F(BtreeTest, MultiThreadReadTest) {
//   real.clear();
//   srand(1);
//   int thread_num = std::stoi(options_["thread_num"]);
//   KeyBuffer key, key2;
//   int num = std::stoi(options_["test_size"]);
//   Status s;
//   for (int i = 0; i < num; i++) {
//     int k = rand() % 100000;
//     key.Set(k);
//     std::string v = std::string(512, 'a' + (i % 26));
//     s = btree_->Insert(key.slice(), Slice(v));
//     EXPECT_TRUE(s.ok());
//     real[k] = v;
//   }
//   std::vector<std::thread> threads;
//   for (int i = 0; i < thread_num; i++) {
//     std::thread t(ReadData, btree_, 100000);
//     threads.push_back(std::move(t));
//   }
//   for (int i = 0; i < threads.size(); i++) {
//     threads[i].join();
//   }
// }

void AddData(Btree* tree, int start, int length, int num) {
  KeyBuffer key, key2;
  for (int i = start; i < start + length; i++) {
    key.Set(i);
    std::string v = std::string(512, 'a' + (i % 26));
    // std::cout << key.slice().ToString() << std::endl;
    Status s = tree->Insert(key.slice(), Slice(v));
    // real[i] = v;
  }
}

TEST_F(BtreeTest, MultiThreadWriteTest) {
  real.clear();
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
    std::thread t(AddData, btree_, start, length, num);
    start += length;
    threads.push_back(std::move(t));
  }
  for (int i = 0; i < threads.size(); i++) {
    threads[i].join();
  }
  btree_->Print();
  start = 0;
  int end = num;
  key.Set(start);
  key2.Set(end);
  std::map<std::string, std::string> result;
  Iterator* iter = btree_->NewBtreeIterator();
  do {
    iter->Seek(key.slice());
    auto it = real.lower_bound(start);
    for (; iter->Valid(); iter->Next()) {
      result[iter->key().ToString()] = iter->value().ToString();
    }
    EXPECT_EQ(real.size(), result.size());
    for (auto r = result.begin(); r != result.end(); r++) {
      key.Set(it->first);
      EXPECT_EQ(r->first, key.slice().ToString());
      EXPECT_EQ(r->second, it->second);
      it++;
    }
  } while (!iter->status().ok());
  delete iter;
}

// TEST_F(BtreeTest, MultiThreadReadWriteTest) {
//   real.clear();
//   int thread_num = std::stoi(options_["thread_num"]);
//   KeyBuffer key, key2;
//   int num = std::stoi(options_["test_size"]);

//   for (int i = 0; i < num; i++) {
//     std::string v = std::string(512, 'a' + (i % 26));
//     real[i] = v;
//   }

//   int start = 0;
//   int length = num / thread_num;
//   std::vector<std::thread> threads;
//   for (int i = 0; i < thread_num; i++) {
//     std::thread t(AddData, btree_, start, length, num);
//     start += length;
//     threads.push_back(std::move(t));
//   }
//   for (int i = 0; i < thread_num; i++) {
//     std::thread t(ReadData, btree_, num);
//     threads.push_back(std::move(t));
//   }
//   for (int i = 0; i < threads.size(); i++) {
//     threads[i].join();
//   }
//   btree_->Print();
// }

} // namespace