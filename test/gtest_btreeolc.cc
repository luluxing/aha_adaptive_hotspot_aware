#include "btree/btree_olc.h"
#include "util/helper.h"
#include <gtest/gtest.h>
#include <thread>

using namespace WOT_NAMESPACE;

namespace {

int FLAGS_key_prefix = 50;
int key_range = 10000000;
int val_range = 10000000;

class KeyBuffer {
 public:
  KeyBuffer() {
    assert(FLAGS_key_prefix < sizeof(buffer_));
    memset(buffer_, '0', FLAGS_key_prefix);
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

class BtreeOLCTest : public testing::Test {
 protected:
  void SetUp() override {
    // LoadOptions("../src/options.spec", &options_);
    options_["buffer_manager_num"] = "1024";
    options_["buffer_file"] = "../build/btreeolc_path";
    btree_ = new BtreeOLC(&options_);
  }

  void TearDown() override {
    delete btree_;
  }

  BtreeOLC* btree_;
  std::map<std::string, std::string> options_;
};

void Insert(int count, BtreeOLC* tree,
          std::vector<int>& inserted, std::vector<int>& vals) {
  KeyBuffer kb, vb;
  for (int i = 0; i < count; i++) {
    int k = rand() % key_range;
    int v = rand() % val_range;
    kb.Set(k);
    vb.Set(v);
    // Search in the inserted vector, if found a same key
    // replace the value with the new value in the corresponding
    // index in vals
    auto it = std::find(inserted.begin(), inserted.end(), k);
    if (it != inserted.end()) {
      vals[it - inserted.begin()] = v;
    } else {
      inserted.push_back(k);
      vals.push_back(v);
    }
    if (i % 10000 == 0) {
      fprintf(stdout, "Processing %d...\n", i);
    }
    tree->Insert(kb.slice(), vb.slice());
  } 
}

void Query(int count, BtreeOLC* tree,
          std::vector<int>& inserted, std::vector<int>& vals) {
  KeyBuffer kb, vb;
  for (int i = 0; i < count; i++) {
    if (i >= inserted.size()) {
      break;
    }
    kb.Set(inserted[i]);
    vb.Set(vals[i]);
    std::string value;
    EXPECT_TRUE(tree->Query(kb.slice().ToString(), &value).ok());
    EXPECT_EQ(value, vb.slice().ToString());
  }
}

void Scan(int count, BtreeOLC* tree, int scan_len,
          std::vector<int>& inserted, std::vector<int>& vals) {
  KeyBuffer kb1, kb2, vb;
  // Sort the inserted keys and rearrange vals according to the sorted keys
  std::vector<std::pair<int, uint32_t>> paired;
  for (size_t i = 0; i < inserted.size(); ++i)
    paired.push_back({inserted[i], vals[i]});
  std::sort(paired.begin(), paired.end());
  // Split the pair back into the individual vectors
  inserted.clear();
  vals.clear();
  for (const auto &pair : paired) {
    inserted.push_back(pair.first);
    vals.push_back(pair.second);
  }
  for (int i = 0; i < inserted.size(); i++) {
    kb1.Set(inserted[i]);
    kb2.Set(inserted[i] + scan_len);
    auto* scanned = tree->BtreeOLCScanIterator(kb1.slice(), kb2.slice());
    scanned->Seek(kb1.slice());
    auto exact = std::find(inserted.begin(), inserted.end(), inserted[i]);
    while (exact != inserted.end() && *exact <= inserted[i] + scan_len) {
      kb1.Set(*exact);
      vb.Set(vals[exact - inserted.begin()]);
      std::string value;
      EXPECT_TRUE(scanned->Valid());
      EXPECT_EQ(scanned->key().compare(kb1.slice().ToString()), 0);
      EXPECT_EQ(scanned->value().compare(vb.slice().ToString()), 0);
      exact++;
      scanned->Next();
    }
    EXPECT_FALSE(scanned->Valid());
    delete scanned;
  }
}

TEST_F(BtreeOLCTest, ConstructionTest) {
  EXPECT_EQ(btree_->RootPageId(), 0);
  EXPECT_EQ(btree_->TreeHeight(), 1);
}

TEST_F(BtreeOLCTest, InsertAndQueryRootTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int count = leaf_cap - 1;
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);
  Query(count, btree_, inserted, vals);
}

TEST_F(BtreeOLCTest, LeafSplitTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int count = leaf_cap + leaf_cap - 1;
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);
  Query(count, btree_, inserted, vals);
  EXPECT_EQ(btree_->TreeHeight(), 2);
}

TEST_F(BtreeOLCTest, InnerSplitTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int inner_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() + 3 * ITEMID_SIZE);
  int count = inner_cap * leaf_cap;
  fprintf(stdout, "Inserting %d\n", count);
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);
  Query(count, btree_, inserted, vals);
  EXPECT_EQ(btree_->TreeHeight(), 3);
}

TEST_F(BtreeOLCTest, MakeRootTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int inner_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() + 3 * ITEMID_SIZE);
  int count = inner_cap * leaf_cap * inner_cap;
  fprintf(stdout, "Inserting %d\n", count);
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);
  Query(count, btree_, inserted, vals);
  EXPECT_EQ(btree_->TreeHeight(), 4);
}

TEST_F(BtreeOLCTest, ShortRangeTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int inner_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() + 3 * ITEMID_SIZE);
  int count = inner_cap * leaf_cap;
  fprintf(stdout, "Inserting %d\n", count);
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);
  EXPECT_EQ(btree_->TreeHeight(), 3);

  fprintf(stdout, "Done inserting\n");
  int range = 1;
  Scan(count, btree_, range, inserted, vals);
}

TEST_F(BtreeOLCTest, LongRangeTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int inner_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() + 3 * ITEMID_SIZE);
  int count = inner_cap * leaf_cap;
  fprintf(stdout, "Inserting %d\n", count);
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);
  EXPECT_EQ(btree_->TreeHeight(), 3);

  // Find the min and max value in inserted
  int min = *std::min_element(inserted.begin(), inserted.end());
  int max = *std::max_element(inserted.begin(), inserted.end());

  fprintf(stdout, "Done inserting\n");
  int range = max - min;
  Scan(count, btree_, range, inserted, vals);
}

void GenInsertions(int count, std::vector<int>& inserted, std::vector<int>& vals) {
  for (int i = 0; i < count; i++) {
    int k = rand() % key_range;
    auto it = std::find(inserted.begin(), inserted.end(), k);
    if (it != inserted.end()) {
      continue;
    } else {
      inserted.push_back(k);
      vals.push_back(k);
    }
  }
}

void ThreadScan(int start, int end, BtreeOLC* tree,
                  std::vector<int>& inserted, std::vector<int>& vals) {
  KeyBuffer kb1, kb2, vb;
  int scan_len = 1000;
  for (int i = 0; i < inserted.size(); i++) {
    kb1.Set(inserted[i]);
    kb2.Set(inserted[i] + scan_len);
    auto* scanned = tree->BtreeOLCScanIterator(kb1.slice(), kb2.slice());
    scanned->Seek(kb1.slice());
    auto exact = std::find(inserted.begin(), inserted.end(), inserted[i]);
    while (exact != inserted.end() && *exact <= inserted[i] + scan_len) {
      kb1.Set(*exact);
      vb.Set(vals[exact - inserted.begin()]);
      std::string value;
      EXPECT_TRUE(scanned->Valid());
      EXPECT_EQ(scanned->key().compare(kb1.slice().ToString()), 0);
      EXPECT_EQ(scanned->value().compare(vb.slice().ToString()), 0);
      exact++;
      scanned->Next();
    }
    EXPECT_FALSE(scanned->Valid());
    delete scanned;
  }
}

void ThreadRead(int start, int end, BtreeOLC* tree,
                  std::vector<int>& inserted, std::vector<int>& vals) {
  KeyBuffer kb, vb;
  for (int i = start; i < end; i++) {
    if (i >= inserted.size()) {
      break;
    }
    kb.Set(inserted[i]);
    vb.Set(vals[i]);
    std::string value;
    EXPECT_TRUE(tree->Query(kb.slice().ToString(), &value).ok());
    EXPECT_EQ(value, vb.slice().ToString());
  } 
}

void ThreadInsert(int start, int end, BtreeOLC* tree,
                  std::vector<int>& inserted, std::vector<int>& vals) {
  KeyBuffer kb, vb;
  for (int i = start; i < end; i++) {
    if (i >= inserted.size()) {
      break;
    }
    int k = inserted[i];
    int v = vals[i];
    kb.Set(k);
    vb.Set(v);
    tree->Insert(kb.slice(), kb.slice());
  } 
}

TEST_F(BtreeOLCTest, MultiThreadWriteTest) {
  // Start 4 threads to insert 1000000 keys
  std::vector<int> inserted;
  std::vector<int> vals;
  int count = 10000;
  GenInsertions(count, inserted, vals);

  fprintf(stdout, "Done generating, start inserting\n");
  std::vector<std::thread> threads;
  int thread_num = 10;
  int seg = count / thread_num;
  for (int i = 0; i < thread_num; i++) {
    threads.push_back(std::thread(ThreadInsert, seg*i, seg*(i+1),
                      btree_, std::ref(inserted), std::ref(vals)));
  }
  for (int i = 0; i < thread_num; i++) {
    threads[i].join();
  }
  Query(count, btree_, inserted, vals);
}

TEST_F(BtreeOLCTest, MultiThreadReadTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int inner_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() + 3 * ITEMID_SIZE);
  int count = 2 * inner_cap * leaf_cap;
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);

  fprintf(stdout, "Done inserting, start multi reading\n");
  std::vector<std::thread> threads;
  int thread_num = 8;
  int seg = count / thread_num;
  for (int i = 0; i < thread_num; i++) {
    threads.push_back(std::thread(ThreadRead, seg*i, seg*(i+1),
                      btree_, std::ref(inserted), std::ref(vals)));
  }
  for (int i = 0; i < thread_num; i++) {
    threads[i].join();
  }
}

TEST_F(BtreeOLCTest, MultiThreadScanTest) {
  KeyBuffer kb;
  kb.Set(1);
  int leaf_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() * 2 + 3 * ITEMID_SIZE);
  int inner_cap = (PAGE_SIZE - PgHdSize) / (kb.slice().size() + 3 * ITEMID_SIZE);
  int count = 2 * inner_cap * leaf_cap;
  std::vector<int> inserted;
  std::vector<int> vals;
  Insert(count, btree_, inserted, vals);

  // Sort the inserted keys and rearrange vals according to the sorted keys
  std::vector<std::pair<int, uint32_t>> paired;
  for (size_t i = 0; i < inserted.size(); ++i)
    paired.push_back({inserted[i], vals[i]});
  std::sort(paired.begin(), paired.end());
  // Split the pair back into the individual vectors
  inserted.clear();
  vals.clear();
  for (const auto &pair : paired) {
    inserted.push_back(pair.first);
    vals.push_back(pair.second);
  }

  fprintf(stdout, "Done inserting, start multi scanning\n");
  std::vector<std::thread> threads;
  int thread_num = 8;
  int seg = count / thread_num;
  for (int i = 0; i < thread_num; i++) {
    threads.push_back(std::thread(ThreadScan, seg*i, seg*(i+1),
                      btree_, std::ref(inserted), std::ref(vals)));
  }
  for (int i = 0; i < thread_num; i++) {
    threads[i].join();
  }
}

void RandomRead(int count, BtreeOLC* tree) {
  KeyBuffer kb, vb;
  for (int i = 0; i < count; i++) {
    int k = rand() % key_range;
    kb.Set(k);
    std::string value;
    tree->Query(kb.slice().ToString(), &value);
  }
}

void RandomInsert(int count, BtreeOLC* tree) {
  KeyBuffer kb, vb;
  for (int i = 0; i < count; i++) {
    int k = rand() % key_range;
    int v = rand() % val_range;
    kb.Set(k);
    vb.Set(v);
    tree->Insert(kb.slice(), vb.slice());
  }
}

void RandomScan(int count, BtreeOLC* tree) {
  KeyBuffer kb1, kb2, vb;
  int scan_len = rand() % 1000;
  for (int i = 0; i < count; i++) {
    int k = rand() % key_range;
    kb1.Set(k);
    kb2.Set(k + scan_len);
    auto* scanned = tree->BtreeOLCScanIterator(kb1.slice(), kb2.slice());
    scanned->Seek(kb1.slice());
    while (scanned->Valid()) {
      scanned->Next();
    }
    delete scanned;
  }
}

TEST_F(BtreeOLCTest, MultiThreadMixedTest) {
  std::vector<int> inserted;
  std::vector<int> vals;
  int count = 100000;
  int w_thread_num = 4;
  int r_thread_num = 4;
  int s_thread_num = 4;

  std::vector<std::thread> threads;
  int w_seg = count / w_thread_num;
  for (int i = 0; i < w_thread_num; i++) {
    threads.push_back(std::thread(RandomInsert, w_seg, btree_));
  }
  int r_seg = count / r_thread_num;
  for (int i = 0; i < r_thread_num; i++) {
    threads.push_back(std::thread(RandomRead, r_seg, btree_));
  }
  int s_seg = count / s_thread_num;
  for (int i = 0; i < s_thread_num; i++) {
    threads.push_back(std::thread(RandomScan, s_seg, btree_));
  }
  for (int i = 0; i < threads.size(); i++) {
    threads[i].join();
  }
}

} // namespace