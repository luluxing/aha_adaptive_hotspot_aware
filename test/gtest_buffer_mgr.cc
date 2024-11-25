// #include "tree_buffer.h"
// #include <gtest/gtest.h>

// using namespace WOT_NAMESPACE;

// namespace {

// class BufferMgrTest : public testing::Test {
//  protected:
//   void SetUp() override {
//     mgr_ = new TreeBufferManager(7, "./wot/mgr_gtest");
//   }

//   void TearDown() override {
//     delete mgr_;
//   }

//   void InsertPages() {
//     std::vector<uint32_t> leafs;
//     for (int i = 0; i < 4; i++) {
//       Page p = mgr_->Allocate();
//       PageInsertLeafEntry(p, Slice(std::string(3, 'a'+(i%26))), 
//                           Slice(std::string(5, 'a'+(i%26)+1)));
//       ((TreePageHeader) p)->is_leaf_ = true;
//       leafs.push_back(((TreePageHeader) p)->page_id_);
//     }
//     int j = 0;
//     for (int i = 0; i < 2; i++) {
//       Page p = mgr_->Allocate();
//       PageInsertIndexEntry(p, Slice(std::string(3, 'a'+(j%26))), leafs[j++]);
//       PageInsertIndexEntry(p, Slice(std::string(3, 'a'+(j%26))), leafs[j++]);
//     }
//   }

//   TreeBufferManager* mgr_;
// };

// TEST_F(BufferMgrTest, EmptyTest) {
//   EXPECT_EQ(mgr_->GetRootPageId(), 0);
// }

// TEST_F(BufferMgrTest, InsertionTest) {
//   InsertPages();
//   for (int i = 0; i < 4; i++) {
//     Slice s = mgr_->GetPivotAtOffset(i+1, 0);
//     EXPECT_EQ(s.ToString(), std::string(3, 'a'+(i%26)));
//     Slice t = mgr_->GetValueAtOffset(i+1, 0);
//     EXPECT_EQ(t.ToString(), std::string(5, 'a'+(i%26)+1));
//   }
// }

// TEST_F(BufferMgrTest, LRUtest) {
//   InsertPages();
//   Page p = mgr_->Allocate();
//   PageInsertIndexEntry(p, Slice("zzz"), 1);
//   std::vector<uint32_t> expect{0,2,3,4,5,6,7};
//   std::vector<uint32_t> pages;
//   mgr_->GetExistingPageIds(&pages);
//   EXPECT_EQ(pages.size(), expect.size());
//   for (int i = 0; i < pages.size(); i++) {
//     EXPECT_EQ(pages[i], expect[i]);
//   }
// }

// TEST_F(BufferMgrTest, LRUandPinTest) {
//   InsertPages();
//   mgr_->Pin(1);
//   Page p = mgr_->Allocate();
//   PageInsertIndexEntry(p, Slice("zzz"), 1);
//   std::vector<uint32_t> expect{0,1,3,4,5,6,7};
//   std::vector<uint32_t> pages;
//   mgr_->GetExistingPageIds(&pages);
//   EXPECT_EQ(pages.size(), expect.size());
//   for (int i = 0; i < pages.size(); i++) {
//     EXPECT_EQ(pages[i], expect[i]);
//   }
// }

// TEST_F(BufferMgrTest, WriteAndReadFileTest) {
//   InsertPages();
//   // Modify page#1
//   mgr_->UpdateMinPivot(1, Slice("ZZZ"));
//   // Insert new page and evict page#1
//   Page p = mgr_->Allocate();
//   PageInsertIndexEntry(p, Slice("hhh"), 1);
//   std::vector<uint32_t> expect{0,2,3,4,5,6,7};
//   std::vector<uint32_t> pages;
//   mgr_->GetExistingPageIds(&pages);
//   EXPECT_EQ(pages.size(), expect.size());
//   for (int i = 0; i < pages.size(); i++) {
//     EXPECT_EQ(pages[i], expect[i]);
//   }
//   // Read page#1 again
//   Slice s = mgr_->GetPivotAtOffset(1, 0);
//   EXPECT_EQ(s.ToString(), "ZZZ");
// }

// TEST_F(BufferMgrTest, ArenaTest) {
//   InsertPages();
//   // Get pivot at page#1
//   Slice res = mgr_->GetPivotAtOffset(1, 0);
//   // Allocate new page and evict page#1
//   Page p = mgr_->Allocate();
//   PageInsertIndexEntry(p, Slice("zzz"), 1);
//   // The pivot is still valid to read
//   std::vector<uint32_t> expect{0,2,3,4,5,6,7};
//   std::vector<uint32_t> pages;
//   mgr_->GetExistingPageIds(&pages);
//   EXPECT_EQ(res, Slice("aaa"));
//   EXPECT_EQ(pages.size(), expect.size());
//   for (int i = 0; i < pages.size(); i++) {
//     EXPECT_EQ(pages[i], expect[i]);
//   }
// }

// TEST_F(BufferMgrTest, FileAcrossPivotsTest) {
//   const Comparator* cmp = BytewiseComparator();
//   Page p = mgr_->Allocate();
//   int num = 10;
//   for (int i = 1; i < num; i++) {
//     PageInsertIndexEntry(p, Slice(std::to_string(i*10)), i);
//   }
//   FileMetaData f1, f2, f3;
//   f1.smallest = InternalKey(Slice("15"), 0, ValueType::kTypeValue);
//   f1.largest =  InternalKey(Slice("19"), 0, ValueType::kTypeValue);

//   f2.smallest = InternalKey(Slice("35"), 0, ValueType::kTypeValue);
//   f2.largest =  InternalKey(Slice("37"), 0, ValueType::kTypeValue);
//   std::vector<FileMetaData*> fs{&f1, &f2};
//   bool r1 = mgr_->FileAcrossPivots(1, &fs, cmp);
//   EXPECT_FALSE(r1);

//   f3.smallest = InternalKey(Slice("39"), 0, ValueType::kTypeValue);
//   f3.largest =  InternalKey(Slice("89"), 0, ValueType::kTypeValue);
//   std::vector<FileMetaData*> fs2{&f1, &f2, &f3};
//   bool r2 = mgr_->FileAcrossPivots(1, &fs2, cmp);
//   EXPECT_TRUE(r2);
// }

// TEST_F(BufferMgrTest, UpdateAndOverwritePageTest) {
//   Page p = mgr_->Allocate();
//   int num = 10;
//   for (int i = 2; i < num; i++) {
//     PageInsertIndexEntry(p, Slice(std::to_string(i*10)), i);
//   }
//   SlicePageMap np;
//   np["11"] = 3;
//   mgr_->UpdatePage(1, &np, Slice("20"));
//   Slice s = mgr_->GetPivotAtOffset(1, 0);
//   EXPECT_EQ(s, Slice("11"));

//   np["99"] = 4;
//   mgr_->OverwritePage(1, &np);
//   EXPECT_EQ(((TreePageHeader) p)->item_num_, 2);
//   Slice t1 = mgr_->GetPivotAtOffset(1, 0);
//   Slice t2 = mgr_->GetPivotAtOffset(1, 1);
//   EXPECT_EQ(t1, Slice("11"));
//   EXPECT_EQ(t2, Slice("99"));
// }

// TEST_F(BufferMgrTest, SplitPageTest) {
//   Page p = mgr_->Allocate();
//   int num = 10;
//   for (int i = 0; i < num; i++) {
//     PageInsertIndexEntry(p, Slice(std::to_string(num + i)), i);
//   }
//   SlicePageMap new_pivots;
//   new_pivots["39"] = 39;
//   new_pivots["40"] = 40;
//   std::vector<SlicePageMap> np_vec{new_pivots};
//   std::vector<std::pair<Slice,uint32_t>> old_pivots{{"10", 0}, {"19", 9}};
//   Page p1 = mgr_->Allocate(); // pg_id=2
//   Page p2 = mgr_->Allocate(); // pg_id=3
//   std::vector<uint32_t> new_pages{2, 3};
//   std::vector<Slice> guards;
//   mgr_->SplitInternalPages(1, &np_vec, &old_pivots, &new_pages, &guards);
//   for (int off = 0; off < 5; off++) {
//     Slice s = mgr_->GetPivotAtOffset(2, off);
//     EXPECT_EQ(s, Slice(std::to_string(off+1+10)));
//     uint32_t c = mgr_->GetChildAtOffset(2, off);
//     EXPECT_EQ(c, off+1);
//   }
//   for (int off = 0; off < 3; off++) {
//     Slice s = mgr_->GetPivotAtOffset(3, off);
//     EXPECT_EQ(s, Slice(std::to_string(off+16)));
//     uint32_t c = mgr_->GetChildAtOffset(3, off);
//     EXPECT_EQ(c, (off+6));
//   }
//   for (int off = 3; off < 5; off++) {
//     Slice s = mgr_->GetPivotAtOffset(3, off);
//     EXPECT_EQ(s, Slice(std::to_string(39+off-3)));
//     uint32_t c = mgr_->GetChildAtOffset(3, off);
//     EXPECT_EQ(c, (39+off-3));
//   }
// }

// TEST_F(BufferMgrTest, SplitLeafTest) {
//   Page p = mgr_->Allocate();
//   ((TreePageHeader) p)->is_leaf_ = true;
//   int num = 20;
//   for (int i = 10; i < num; i++) {
//     PageInsertLeafEntry(p, Slice(std::to_string(i)), Slice(std::to_string(i)));
//   }
//   std::vector<uint32_t> new_pg_ids;
//   std::vector<Slice> new_pivots;
//   InternalKeyComparator icmp{InternalKeyComparator(BytewiseComparator())};
//   mgr_->InsertAndSplitLeafPages(1, Slice("20"), Slice("20"), &new_pg_ids,
//                                 &new_pivots, icmp);
//   EXPECT_EQ(new_pg_ids.size(), 2);
//   int s = 0;
//   for (auto const& pg_id : new_pg_ids) {
//     Page c = mgr_->Lookup(pg_id);
//     int num = ((TreePageHeader) c)->item_num_;
//     EXPECT_TRUE(num > 0);
//     EXPECT_TRUE(((TreePageHeader) c)->is_leaf_);
//     for (int i = 0; i < num; i++) {
//       EXPECT_EQ(mgr_->GetPivotAtOffset(pg_id, i), Slice(std::to_string(i + 10 + s)));
//       EXPECT_EQ(mgr_->GetValueAtOffset(pg_id, i), Slice(std::to_string(i + 10 + s)));
//     }
//     s += 5;
//   }
// }

// TEST_F(BufferMgrTest, SplitInternalTest) {
//   Page p = mgr_->Allocate();
//   int num = 20;
//   std::map<int, int> res;
//   for (int i = 10; i < num; i++) {
//     PageInsertIndexEntry(p, Slice(std::to_string(i*3)), i*3);
//     res[i*3] = i*3;
//   }
//   int old = 36;
//   res.erase(old);
//   std::vector<uint32_t> new_pg_ids{34, 35};
//   res[34] = 34;
//   res[35] = 35;
//   std::vector<Slice> new_pivots{Slice("34"), Slice("35")};
  
//   mgr_->InsertAndSplitInternalPages(1, old, &new_pg_ids,
//                                     &new_pivots);
//   auto it = res.begin();
//   for (auto const& pg_id : new_pg_ids) {
//     Page c = mgr_->Lookup(pg_id);
//     int num = ((TreePageHeader) c)->item_num_;
//     EXPECT_TRUE(num > 0);
//     EXPECT_FALSE(((TreePageHeader) c)->is_leaf_);
//     for (int i = 0; i < num; i++) {
//       EXPECT_EQ(mgr_->GetPivotAtOffset(pg_id, i), Slice(std::to_string(it->first)));
//       EXPECT_EQ(mgr_->GetChildAtOffset(pg_id, i), it->second);
//       it++;
//     }
//   }
// }

// } // namespace