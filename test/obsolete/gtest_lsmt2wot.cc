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

class Lsmt2WotTest : public testing::Test {
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

// LSMT only has 1 level
// TEST_F(Lsmt2WotTest, SmallLSMTtest) {
//   KeyBuffer key, key2;
//   int num = std::stoi(options_["test_size"]);
//   srand(1);
//   std::map<uint32_t, std::string> ins;
//   // Each internal tree node can hold 3 index entries
//   for (int i = 0; i < 4000; i++) {
//     int k = rand() % 100000;
//     key.Set(k);
//     std::string v = std::string(512, 'a' + (i % 26));
//     tree_->Insert(key.slice().ToString(), Slice(v).ToString());
//     ins[k] = v;
//   }
//   tree_->PrintStat();

//   EXPECT_EQ(tree_->TreeHeight(), 1);
//   tree_->AdaptToRead();

//   int scan_num = 20;//2;
//   int query_size = 6000;//num/2;
//   int start = 0, end;
  
//   for (int q = 0; q < scan_num; q++) {
//     end = start + query_size;
//     key.Set(start);
//     key2.Set(end);
//     std::map<std::string, std::string> result;
//     auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
//     for (iter->Seek(); iter->Valid(); iter->Next()) {
//       // std::cout << iter->key() << std::endl;
//       result[iter->key()] = iter->value();
//     }
//     auto i = result.begin();
//     auto it = ins.lower_bound(start);
//     for (; it != ins.end() && i != result.end(); it++) {
//       key.Set(it->first);
//       // std::cout << it->first << std::endl;
//       EXPECT_EQ(key.slice().ToString(), i->first);
//       EXPECT_EQ(it->second, i->second);
//       i++;
//     }
    
//     start = end;
//   }
//   tree_->PrintStat();
//   EXPECT_EQ(tree_->TreeHeight(), 1);
// }

// LSMT has multiple levels
// TEST_F(Lsmt2WotTest, LargeLSMTtest) {
//   KeyBuffer key, key2;
//   int num = std::stoi(options_["test_size"]);
//   srand(1);
//   std::map<uint32_t, std::string> ins;
//   // Each internal tree node can hold 3 index entries
//   for (int i = 0; i < num; i++) {
//     int k = rand() % 100000;
//     key.Set(k);
//     std::string v = std::string(512, 'a' + (i % 26));
//     tree_->Insert(key.slice().ToString(), v);
//     ins[k] = v;
//   }
//   tree_->PrintStat();

//   EXPECT_EQ(tree_->TreeHeight(), 1);
//   tree_->AdaptToRead();

//   int scan_num = 200;//2;
//   int query_size = 10000;//num/2;
//   int start = 0, end;
  
//   for (int q = 0; q < scan_num; q++) {
//     end = start + query_size;
//     key.Set(start);
//     key2.Set(end);
//     std::map<std::string, std::string> result;
//     auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
//     for (iter->Seek(); iter->Valid(); iter->Next()) {
//       // std::cout << iter->key() << std::endl;
//       result[iter->key()] = iter->value();
//     }
//     auto i = result.begin();
//     auto it = ins.lower_bound(start);
//     for (; it != ins.end() && i != result.end(); it++) {
//       key.Set(it->first);
//       // std::cout << it->first << std::endl;
//       EXPECT_EQ(key.slice().ToString(), i->first);
//       EXPECT_EQ(it->second, i->second);
//       i++;
//     }
    
//     start = end;
//   }
//   tree_->PrintStat();
//   EXPECT_EQ(tree_->TreeHeight(), 2);
// }

// Write and read are interleaved
TEST_F(Lsmt2WotTest, InterleaveTest) {
  KeyBuffer key, key2;
  int num = std::stoi(options_["test_size"]);
  int scan_num = 2;
  int query_size = 10000;//num/2;
  int start = 0, end;
  srand(1);
  std::map<uint32_t, std::string> ins;
  // Each internal tree node can hold 3 index entries
  for (int interleave = 0; interleave < 15; interleave++) {
    std::cout << interleave << "#\n";
    for (int i = 0; i < num; i++) {
      int k = rand() % 100000;
      key.Set(k);
      std::string v = std::string(512, 'a' + (i % 26));
      tree_->Insert(key.slice().ToString(), v);
      ins[k] = v;
    }
    tree_->AdaptToRead();
    for (int q = 0; q < scan_num; q++) {
      end = start + query_size;
      key.Set(start);
      key2.Set(end);
      std::map<std::string, std::string> result;
      auto iter = tree_->NewUnsortedTreeIterator(key.slice(), key2.slice());
      for (iter->Seek(); iter->Valid(); iter->Next()) {
        // std::cout << iter->key() << std::endl;
        result[iter->key()] = iter->value();
      }
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
    scan_num += 20;
  }
  
  // tree_->PrintStat();

  EXPECT_EQ(tree_->TreeHeight(), 3);
}

TEST_F(Lsmt2WotTest, SortedIteratorTest) {
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
  tree_->AdaptToRead();

  tree_->PrintStat();
  EXPECT_EQ(tree_->TreeHeight(), 1);
  int start = 0;
  int end = 1000000;
  key.Set(start);
  key2.Set(end);
  std::cout << "Begin range query\n";
  int x = 0;
  do {
    // std::cout << x << " !!\n";
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
      // if (k3.slice().ToString() != iter->key()) {
      //   std::exit(1);
      // }
    }
    delete iter;
    x++;
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    // tree_->PrintStat();
  } while (x < 15);
  tree_->PrintStat();
}


} // namespace  