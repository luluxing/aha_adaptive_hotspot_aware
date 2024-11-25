#include "tree_buffer.h"
#include "tree_page.h"

#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <iostream>

using namespace WOT_NAMESPACE;

void test_page_size() {
  Page p = (Page) malloc(PAGE_SIZE);
  TreePageHeader h = (TreePageHeader) p;
  h->page_id_ = 1;
  h->item_num_ = 1;
  fprintf(stdout, "Page %p, Header %p, data %p, %ld\n",
          p, h, h + sizeof(TreePageHeaderData), sizeof(TreePageHeaderData));
  delete p;
}

void test_lru() {
  TreeBufferManager* bm = new TreeBufferManager(3, "");
  bm->SetMaxNum(3);

  Page p1 = bm->Allocate();
  bm->Lookup(1);
  Page p2 = bm->Allocate();
  Page p3 = bm->Allocate();
  Page p4 = bm->Allocate();
  bm->ExistingPageIds();
}

void test_page_write_and_read() {
  Page p = (Page) malloc(PAGE_SIZE);
  PageInit(p, 3);
  
  for (int i = 10; i < 20; i++) {
    std::string s = std::to_string(90-i+1);
    std::cout << s << ", ";
    int off = PageBinarySearchInsert(p, Slice(s));
    PageWriteIndexEntryAt(off, p, Slice(s), i);
  }

  std::cout << "\nDone writing\n";

  for (int i = 0; i < 11; i++) {
    Slice s = PageReadPivotAtOffset(p, i);
    std::cout << s.ToString() << std::endl;
  }
}

void test_file() {
  TreeBufferManager* bm = new TreeBufferManager(3, "wot/indexfile");
  for (int i = 0; i < 4; i++) {
    Page p = bm->Allocate();
    ((TreePageHeader) p)->is_leaf_ = true;
    std::string k = std::to_string(i);
    PageInsertLeafEntry(p, Slice(k), Slice(k));
    std::cout << "Page id=" << ((TreePageHeader) p)->page_id_ << std::endl;
    PagePrint(p);
  }
  
  for (int i = 0; i < 4; i++) {
    Page p = bm->Lookup(i);
    if (p != nullptr) {
      std::cout << "Found page " << i << std::endl;
      PagePrint(p);
    }
  }
}

void test_buffer_mgr() {
  {
    TreeBufferManager* bm = new TreeBufferManager(5, "wot/indexfile");
    for (int i = 0; i < 10; i++) {
      bm->Allocate();
    }
    bm->ExistingPageIds();
  }

  {
    TreeBufferManager* bm = new TreeBufferManager(5, "wot/indexfile");
    for (int i = 0; i < 10; i++) {
      bm->Allocate();
    }
    for (int i = 10; i >= 0; i--) {
      bm->Lookup(i);
      bm->Release(i);
    }
    bm->ExistingPageIds();
  }

  {
    TreeBufferManager* bm = new TreeBufferManager(5, "wot/indexfile");
    for (int i = 0; i < 10; i++) {
      bm->Allocate();
    }
    bm->Delete(1);
    bm->Delete(5);
    bm->Delete(10);
    for (int i = 0; i < 3; i++) {
      bm->Allocate();
    }
    bm->ExistingPageIds();
  }
}

int main() {
  // test_empty_buffer();
  // test_empty_buffer();
  // test_lru();
  // test_page_write_and_read();
  // test_file();
  test_buffer_mgr();
  return 0;
}