#include "leveldb/include/slice.h"
#include "wot_buf_mgr/tree_page.h"
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

class PageTest : public testing::Test {
 protected:
  void SetUp() override {
    p1_ = (char*) malloc(PAGE_SIZE);
    PageInit(p1_, 2);
    h1_ = (TreePageHeader) p1_;
    p2_ = (char*) malloc(PAGE_SIZE);
    PageInit(p2_, 3);
  }

  void TearDown() override {
    free(p1_); 
    free(p2_);
  }

  Page p1_;
  Page p2_;
  TreePageHeader h1_;
};

TEST_F(PageTest, EmptyConstructorHeader) {
  EXPECT_EQ(h1_->item_num_, 0);
  EXPECT_EQ(h1_->page_id_, 2);
  EXPECT_EQ(h1_->free_start_, PgHdSize);
  EXPECT_EQ(h1_->free_end_, PAGE_SIZE);
  EXPECT_EQ(h1_->is_leaf_, false);
  EXPECT_EQ(h1_->is_dirty_, true);
}

TEST_F(PageTest, ReadWriteIndexEntry) {
  size_t needed_space = 3 + 4 + 4;
  int i = 0, count = 10;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'a'+(i%26)*2)), 100-i);
    needed_space = 11;
    i++;
  }
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'b'+(i%26)*2)), 100-i);
    needed_space = 11;
    i++;
  }
  int j = 0;
  while (j < count) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k, Slice(std::string(3, 'a'+(j%26)*2)));
    int d = PageReadChildAtOffset(p1_, j);
    EXPECT_EQ(d, 100-j);
    j++;
  }
  while (j < count) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k, Slice(std::string(3, 'b'+(j%26)*2)));
    int d = PageReadChildAtOffset(p1_, j);
    EXPECT_EQ(d, 100-j);
    j++;
  }
}

TEST_F(PageTest, BinarySearchInsertLeafTest) {
  std::map<uint32_t, uint32_t> items;
  srand(1);
  size_t needed_space = 3 + 4 + 5 + 4;
  int i = 0, count = 28;
  h1_->is_leaf_ = true;
  KeyBuffer kb;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    int x = rand() % 100;
    kb.Set(x);
    PageInsertLeafEntry(p1_, kb.slice(), kb.slice());
    needed_space = 16;
    i++;
    items[x] = x;
  }
  int j = 0;
  auto it = items.begin();
  while (j < items.size()) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    kb.Set(it->first);
    EXPECT_EQ(k, kb.slice());
    Slice d = PageReadValueAtOffset(p1_, j);
    EXPECT_EQ(d, kb.slice());
    j++;
    it++;
  }
}

TEST_F(PageTest, BinarySearchLeafTest) {
  std::map<uint32_t, uint32_t> items;
  srand(1);
  size_t needed_space = 3 + 4 + 5 + 4;
  int i = 0, count = 48;
  h1_->is_leaf_ = true;
  KeyBuffer kb;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    int x = rand() % 100;
    kb.Set(x);
    PageInsertLeafEntry(p1_, kb.slice(), kb.slice());
    needed_space = 16;
    i++;
    items[x] = x;
  }
  int j = 0;
  auto it = items.begin();
  while (j < items.size()) {
    kb.Set(it->first);
    int k = PageFindOffset(p1_, kb.slice());    
    EXPECT_EQ(k, j);
    j++;
    it++;
  }
}

TEST_F(PageTest, ReadWriteLeafEntry) {
  size_t needed_space = 3 + 4 + 5 + 4;
  int i = 0, count = 2;
  h1_->is_leaf_ = true;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertLeafEntry(p1_, Slice(std::string(3, 'a'+(i%26))), 
                        Slice(std::string(5, 'f'+(i%26))));
    needed_space = 16;
    i++;
  }
  int j = 0;
  while (j < count) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k, Slice(std::string(3, 'a'+(j%26))));
    Slice d = PageReadValueAtOffset(p1_, j);
    EXPECT_EQ(d, Slice(std::string(5, 'f'+(j%26))));
    j++;
  }
}

TEST_F(PageTest, UpdateMinIndexEntry) {
  int i = 0;
  PageInsertIndexEntry(p1_, Slice(std::string("bbb")), i++);
  PageInsertIndexEntry(p1_, Slice(std::string("ccc")), i++);
  
  PageUpdateMinPivot(p1_, Slice("aaa"));

  Slice k = PageReadPivotAtOffset(p1_, 0);
  EXPECT_EQ(k, Slice("aaa"));
  int d = PageReadChildAtOffset(p1_, 0);
  EXPECT_EQ(d, 0);
    
  k = PageReadPivotAtOffset(p1_, 1);
  EXPECT_EQ(k, Slice("ccc"));
  d = PageReadChildAtOffset(p1_, 1);
  EXPECT_EQ(d, 1);
}

TEST_F(PageTest, RemoveIndexEntry) {
  size_t needed_space = 3 + 4 + 4;
  int i = 0, count = 10;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'a'+(i%26))), 100-i);
    needed_space = 11;
    i++;
  }
  i = 0;
  while (2 * i < 10) {
    PageRemoveEntry(p1_, Slice(std::string(3, 'b'+(i%26)*2)));
    i++;
  }
  int j = 0;
  while (j < count / 2) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k, Slice(std::string(3, 'a'+(j%26)*2)));
    int d = PageReadChildAtOffset(p1_, j);
    EXPECT_EQ(d, 100-j*2);
    j++;
  }
}

TEST_F(PageTest, RemoveThenInsertIndexEntry) {
  size_t needed_space = 3 + 4 + 4;
  int i = 0, count = 10, idx = 23;
  std::map<std::string, uint32_t> ids;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'a'+(i%26))), idx);
    ids[(std::string(3, 'a'+(i%26)))] = idx++;
    needed_space = 11;
    i++;
  }
  i = 0;
  while (2 * i < 10) {
    PageRemoveEntry(p1_, Slice(std::string(3, 'b'+(i%26)*2)));
    i++;
  }
  i = 0;
  while (i < 5) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'b'+(i%26)*2)), idx);
    ids[(std::string(3, 'b'+(i%26)*2))] = idx++;
    needed_space = 11;
    i++;
  }
  int j = 0;
  while (j < count) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k, Slice(std::string(3, 'a'+(j%26))));
    int d = PageReadChildAtOffset(p1_, j);
    EXPECT_EQ(d, ids[k.ToString()]);
    j++;
  }
}

TEST_F(PageTest, FindOffsetTest) {
  size_t needed_space = 3 + 4 + 4;
  int i = 0, count = 10;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'a'+(i%26)*2)), 100-i);
    needed_space = 11;
    i++;
  }
  EXPECT_EQ(PageGetOffsetByChild(p1_, 100), 0);
  EXPECT_EQ(PageGetOffsetByChild(p1_, 91), 9);
  EXPECT_EQ(PageGetOffsetByChild(p1_, 95), 5);
}

TEST_F(PageTest, MoveEntriesTest) {
  size_t needed_space = 3 + 4 + 4;
  int i = 0, count = 20;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    PageInsertIndexEntry(p1_, Slice(std::string(3, 'a'+(i%26))), i);
    needed_space = 11;
    i++;
  }
  PageMoveIndexEntries(p1_, p2_, 2, 12);
  int j = 0;
  while (j < 10) {
    Slice k = PageReadPivotAtOffset(p2_, j);
    EXPECT_EQ(k, Slice(std::string(3, 'c'+(j%26))));
    int d = PageReadChildAtOffset(p1_, j);
    EXPECT_EQ(d, j);
    j++;
  }
}

TEST_F(PageTest, InsertDuplicateTest) {
  ((TreePageHeader) p1_)->is_leaf_ = true;
  size_t needed_space = 3 + 4 + 4 + 5;
  int i = 0, count = 10;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    std::string ikey = std::string(3, 'a'+(i%26));
    // Convert an uint64_t number to a 64-bit string
    // ikey.append(std::string(8, '0'+i));
    PageInsertLeafEntry(p1_, Slice(ikey), Slice(std::string(5, 'f'+(i%26))));
    i++;
  }
  i = 0;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    std::string ikey = std::string(3, 'a'+(i%26));
    // Convert an uint64_t number to a 64-bit string
    // ikey.append(std::string(8, '1'+i));
    PageInsertLeafEntry(p1_, Slice(ikey), Slice(std::string(5, '1'+(i%26))));
    i++;
  }
  int j = 0;
  while (j < count) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k.ToString(), std::string(3, 'a'+(j%26)));
    Slice v = PageReadValueAtOffset(p1_, j);
    EXPECT_EQ(v.ToString(), std::string(5, '1'+(j%26)));
    j++;
  }
  PagePrint(p1_);
}

TEST_F(PageTest, InsertDuplicateWithSeqTest) {
  ((TreePageHeader) p1_)->is_leaf_ = true;
  size_t needed_space = 3 + 4 + 4 + 5;
  int i = 0, count = 10;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    std::string ikey = std::string(3, 'a'+(i%26));
    // Convert an uint64_t number to a 64-bit string
    ikey.append(std::string(8, '0'+i));
    PageInsertLeafEntryWithSeq(p1_, Slice(ikey), Slice(std::string(5, 'f'+(i%26))));
    i++;
  }
  i = 0;
  while (PageGetFreeSpace(p1_) >= needed_space && i < count) {
    std::string ikey = std::string(3, 'a'+(i%26));
    // Convert an uint64_t number to a 64-bit string
    ikey.append(std::string(8, '1'+i));
    PageInsertLeafEntryWithSeq(p1_, Slice(ikey), Slice(std::string(5, 'b'+(i%26))));
    i++;
  }
  int j = 0;
  while (j < count) {
    Slice k = PageReadPivotAtOffset(p1_, j);
    EXPECT_EQ(k.ToString().substr(0, 3), std::string(3, 'a'+(j%26)));
    Slice v = PageReadValueAtOffset(p1_, j);
    EXPECT_EQ(v.ToString(), std::string(5, 'b'+(j%26)));
    j++;
  }
}

} // namespace