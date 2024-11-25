#include "leveldb/include/slice.h"
#include "wot_buf_mgr/node_page.h"
#include "gtest/gtest.h"

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

class NodePageTest : public testing::Test {
 protected:
  void SetUp() override {
    p1_ = (char*) malloc(PAGE_SIZE);
    PageInit(p1_, 2);
    h1_ = (TreePageHeader) p1_;
    p2_ = (char*) malloc(PAGE_SIZE);
    PageInit(p2_, 3);
    h2_ = (TreePageHeader) p2_;
    h2_->is_leaf_ = true;
  }

  void TearDown() override {
    free(p1_); 
    free(p2_);
  }

  Page p1_;
  Page p2_;
  TreePageHeader h1_;
  TreePageHeader h2_;
};

TEST_F(NodePageTest, EmptyConstructorHeader) {
  EXPECT_EQ(h1_->item_num_, 0);
  EXPECT_EQ(h1_->page_id_, 2);
  EXPECT_EQ(h1_->free_start_, PgHdSize);
  EXPECT_EQ(h1_->free_end_, PAGE_SIZE);
  EXPECT_EQ(h1_->is_leaf_, false);
  EXPECT_EQ(h1_->is_dirty_, true);
}

TEST_F(NodePageTest, InitialWriteInner) {
  EXPECT_EQ(h1_->item_num_ , 0);
  KeyBuffer kb;
  kb.Set(100);
  PageInsertFirstPivotPair(p1_, kb.slice(), 4, 6);
  EXPECT_EQ(h1_->item_num_ , 1);
  Slice k;
  PageReadKeyAtOffset(p1_, 0, k);
  EXPECT_EQ(k.compare(kb.slice()), 0);
  kb.Set(1);
  EXPECT_EQ(PageLowerBound(p1_, kb.slice()) , -1);
  kb.Set(102);
  EXPECT_EQ(PageLowerBound(p1_, kb.slice()) , 0);
}

TEST_F(NodePageTest, SequentialWriteInner) {
  EXPECT_EQ(h1_->item_num_ , 0);
  KeyBuffer kb;
  kb.Set(100);
  PageInsertFirstPivotPair(p1_, kb.slice(), 4, 6);
  std::vector<int> keys{90, 101};
  std::vector<int> pivots{100};
  std::vector<uint32_t> cids{4, 6};
  for (int i = 0; i < 10; i++) {
    kb.Set(200 + i*2);
    PageInsertPivotPair(p1_, kb.slice(), 2 * (i+4));
    keys.push_back(201 + i*2);
    cids.push_back(2 * (i+4));
    pivots.push_back(200 + i*2);
  }
  EXPECT_EQ(h1_->item_num_ , 11);
  for (int i = 0; i < keys.size(); i++) {
    kb.Set(keys[i]);
    int off = PageLowerBound(p1_, kb.slice());
    auto id = PageReadChildAt(p1_, off);
    EXPECT_EQ(id, cids[i]);
    if (i < keys.size() - 1) {
      Slice k;
      PageReadKeyAtOffset(p1_, off + 1, k);
      kb.Set(pivots[off + 1]);
      EXPECT_EQ(k.compare(kb.slice()), 0);
    }
  }
}

TEST_F(NodePageTest, NonSequentialWriteInner) {
  EXPECT_EQ(h1_->item_num_ , 0);
  KeyBuffer kb;
  kb.Set(100);
  PageInsertFirstPivotPair(p1_, kb.slice(), 4, 6);
  std::vector<int> keys{90, 101};
  std::vector<uint32_t> cids{4, 6};
  for (int i = 10; i > 0; i--) {
    kb.Set(200 + i*2);
    PageInsertPivotPair(p1_, kb.slice(), 2 * (i+4));
    keys.push_back(201 + i*2);
    cids.push_back(2 * (i+4));
  }
  std::sort(keys.begin(), keys.end());
  std::sort(cids.begin(), cids.end());
  EXPECT_EQ(h1_->item_num_ , 11);
  for (int i = 0; i < keys.size(); i++) {
    kb.Set(keys[i]);
    auto id = PageReadChildAt(p1_, PageLowerBound(p1_, kb.slice()));
    EXPECT_EQ(id, cids[i]);
  }
}

TEST_F(NodePageTest, SequentialWriteLeaf) {
  EXPECT_EQ(h2_->item_num_, 0);
  KeyBuffer kb;
  int count = 20;
  std::vector<int> keys;
  std::vector<std::string> cids;
  for (int i = 0; i < count; i++) {
    kb.Set(200 + i*2);
    PageInsertKvpair(p2_, kb.slice(), kb.slice());
    keys.push_back(201 + i*2);
    cids.push_back(kb.slice().ToString());
  }
  EXPECT_EQ(h2_->item_num_, count);
  for (int i = 0; i < keys.size(); i++) {
    kb.Set(keys[i]);
    auto pos = PageLowerBound(p2_, kb.slice());
    assert(pos >= 0);
    Slice v;
    PageReadValAt(p2_, pos, v);
    EXPECT_EQ(v.compare(cids[i]), 0);
  }
}

TEST_F(NodePageTest, NonSequentialWriteLeaf) {
  EXPECT_EQ(h2_->item_num_, 0);
  KeyBuffer kb;
  int count = 20;
  std::vector<int> keys;
  std::vector<std::string> cids;
  for (int i = count; i > 0; i--) {
    kb.Set(200 + i*2);
    PageInsertKvpair(p2_, kb.slice(), kb.slice());
    keys.push_back(201 + i*2);
    cids.push_back(kb.slice().ToString());
  }
  EXPECT_EQ(h2_->item_num_, count);
  for (int i = 0; i < keys.size(); i++) {
    kb.Set(keys[i]);
    auto pos = PageLowerBound(p2_, kb.slice());
    assert(pos >= 0);
    Slice v;
    PageReadValAt(p2_, pos, v);
    EXPECT_EQ(v.compare(cids[i]), 0);
  }
}

TEST_F(NodePageTest, FullInnerPage) {
  size_t avail_space = PAGE_SIZE - PgHdSize - ITEMID_SIZE/*tail_id*/;
  KeyBuffer kb;
  kb.Set(10);
  size_t pivot_pair_size = kb.slice().size() + ITEMID_SIZE * 3;
  int cap = avail_space / pivot_pair_size;
  PageInsertFirstPivotPair(p1_, kb.slice(), 4, 5);
  for (int i = 0; i < cap - 1; i++) {
    kb.Set(11 + i);
    PageInsertPivotPair(p1_, kb.slice(), i+6);
  }
  EXPECT_TRUE(InnerPageIsFull(p1_, kb.slice()));
  EXPECT_EQ(PageReadChildAt(p1_, -1), 4);
  for (int i = 0; i < cap; i++) {
    EXPECT_EQ(PageReadChildAt(p1_, i), i+5);
  }
}

TEST_F(NodePageTest, FullLeafPage) {
  size_t avail_space = PAGE_SIZE - PgHdSize;
  KeyBuffer kb;
  kb.Set(10);
  size_t kv_size = kb.slice().size() * 2 + ITEMID_SIZE * 3;
  int cap = avail_space / kv_size;
  for (int i = 0; i < cap; i++) {
    kb.Set(100 + i);
    PageInsertKvpair(p2_, kb.slice(), kb.slice());
  }
  EXPECT_TRUE(LeafPageIsFull(p2_, kb.slice(), kb.slice()));
  for (int i = 0; i < cap; i++) {
    Slice v;
    PageReadValAt(p2_, i, v);
    kb.Set(100 + i);
    EXPECT_EQ(v.compare(kb.slice()), 0);
  }
}

TEST_F(NodePageTest, SplitInnerPage) {
  KeyBuffer kb;
  kb.Set(10);
  PageInsertFirstPivotPair(p1_, kb.slice(), 4, 5);
  int count = 27;
  for (int i = 0; i < count; i++) {
    kb.Set(11 + i);
    PageInsertPivotPair(p1_, kb.slice(), i + 6);
  }
  EXPECT_EQ(h1_->item_num_, ++count);
  Page new_page = (Page) malloc(PAGE_SIZE);
  PageInit(new_page, 101);
  Slice sep;
  GetInnerPageSplitSep(p1_, sep);
  InnerPageSplit(p1_, new_page);
  int new_page_count = count - count / 2;
  EXPECT_EQ(h1_->item_num_, count - new_page_count - 1);
  EXPECT_EQ(((TreePageHeader) new_page)->item_num_, new_page_count);
  kb.Set(10 + count - new_page_count - 1);
  EXPECT_EQ(sep.compare(kb.slice()), 0);
}

TEST_F(NodePageTest, SplitLeafPage) {
  KeyBuffer kb;
  int count = 27;
  for (int i = 0; i < count; i++) {
    kb.Set(11 + i);
    PageInsertKvpair(p2_, kb.slice(), kb.slice());
  }
  EXPECT_EQ(h2_->item_num_, count);
  Page new_page = (Page) malloc(PAGE_SIZE);
  PageInit(new_page, 101);
  Slice sep;
  GetLeafPageSplitSep(p2_, sep);
  LeafPageSplit(p2_, new_page);
  int new_page_count = count - count / 2;
  EXPECT_EQ(h2_->item_num_, count - new_page_count);
  EXPECT_EQ(((TreePageHeader) new_page)->item_num_, new_page_count);
  kb.Set(11 + count - new_page_count - 1);
  EXPECT_EQ(sep.compare(kb.slice()), 0);
}

TEST_F(NodePageTest, DuplicateKey) {
  KeyBuffer kb;
  int count = 27;
  for (int i = 0; i < count; i++) {
    kb.Set(11 + i);
    PageInsertKvpair(p2_, kb.slice(), kb.slice());
  }
  EXPECT_EQ(h2_->item_num_, 27);
  kb.Set(14);
  int pos = PageLowerBound(p2_, kb.slice());
  Slice k;
  PageReadKeyAtOffset(p2_, pos+1, k);
  EXPECT_EQ(k.compare(kb.slice()), 0);

  KeyBuffer v;
  v.Set(102);
  PageInsertKvpair(p2_, kb.slice(), v.slice());
  EXPECT_EQ(h2_->item_num_, 27);
  PageReadKeyAtOffset(p2_, pos+1, k);
  EXPECT_EQ(k.compare(kb.slice()), 0);
  Slice val;
  PageReadValAt(p2_, pos + 1, val);
  EXPECT_EQ(val.compare(v.slice()), 0);
}

} // namespace