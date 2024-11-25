#ifndef WOT_PAGE_ITER_H_
#define WOT_PAGE_ITER_H_

#include "leveldb/include/iterator.h"
#include "wot_buf_mgr/tree_page.h"

namespace WOT_NAMESPACE {

class BplusTree;
class TreeBufferManager;

// The iterator iterates over multiple pages that are sorted
class PageIterator : public Iterator {
 public:
  PageIterator(BplusTree* tree);

  PageIterator(const PageIterator&) = delete;
  PageIterator& operator=(const PageIterator&) = delete;

  ~PageIterator() override;

  void AddPage(uint32_t pid);

  bool Valid() const override { return valid_; }

  void Seek(const Slice& k) override;

  void SeekToFirst() override;

  // This function is ambiguous
  void SeekToLast() override;

  void Next() override;

  void Prev() override;

  Slice key() const override;

  Slice value() const override;

  Status status() const override;

 private:
  bool valid_;
  int off_;
  BplusTree* tree_;
  Page page_;
  std::vector<uint32_t> page_ids_;
  int pinned_page_ = -1;
  int vec_index_;
};


} // namespace WOT_NAMESPACE

#endif // WOT_PAGE_ITER_H_