#include "wot_page_iter.h"
#include "wot_index.h"

namespace WOT_NAMESPACE {

PageIterator::PageIterator(BplusTree* tree) 
: valid_(false),
  off_(0),
  tree_(tree),
  vec_index_(0) {
  // page_ = tree_->buffer_manager_->Pin(pid);
}

PageIterator::~PageIterator() {
  // if (((double) rand() / (RAND_MAX)) < 0.00001) {
  //   fprintf(stdout, "page item num %d\n", ((TreePageHeader) page_)->item_num_);
  // }
  tree_->buffer_manager_->UnpinAndRelease(((TreePageHeader) page_)->page_id_);
  pinned_page_ = -1;
  page_ids_.clear();
}

void PageIterator::AddPage(uint32_t pid) {
  page_ids_.push_back(pid);
}

void PageIterator::Seek(const Slice& k) {
  if (pinned_page_ != -1) {
    // If the iterator is reused, unpin the previous page
    tree_->buffer_manager_->UnpinAndRelease(pinned_page_);
  }
  vec_index_ = 0;
  page_ = tree_->buffer_manager_->Pin(page_ids_[vec_index_]);
  if (tree_->print_page_) {
    fprintf(stdout, "leaf-%d-%d\n", page_ids_[vec_index_], ((TreePageHeader) page_)->item_num_);
  }
  pinned_page_ = page_ids_[vec_index_];
  if (((TreePageHeader) page_)->item_num_ == 0) {
    valid_ = false;
    return;
  }
  off_ = PageFindOffsetBol(page_, k);
  if (off_ == ((TreePageHeader) page_)->item_num_) {
    valid_ = false;
    return;
  }
  if (off_ < 0) {
    off_ = 0;
  }
  valid_ = true;
}

void PageIterator::SeekToFirst() {
  if (pinned_page_ != -1) {
    // If the iterator is reused, unpin the previous page
    tree_->buffer_manager_->UnpinAndRelease(pinned_page_);
  }
  vec_index_ = 0;
  page_ = tree_->buffer_manager_->Pin(page_ids_[vec_index_]);
  if (tree_->print_page_) {
    fprintf(stdout, "leaf-%d-%d\n", page_ids_[vec_index_], ((TreePageHeader) page_)->item_num_);
  }
  pinned_page_ = page_ids_[vec_index_];
  if (((TreePageHeader) page_)->item_num_ == 0) {
    valid_ = false;
    Next();
    return;
  }
  off_ = 0;
  valid_ = true;
}

void PageIterator::SeekToLast() {
  if (pinned_page_ != -1) {
    tree_->buffer_manager_->UnpinAndRelease(pinned_page_);
  }
  page_ = tree_->buffer_manager_->Pin(page_ids_[vec_index_]);
  if (tree_->print_page_) {
    fprintf(stdout, "leaf-%d-%d\n", page_ids_[vec_index_], ((TreePageHeader) page_)->item_num_);
  }
  pinned_page_ = page_ids_[vec_index_];
  if (((TreePageHeader) page_)->item_num_ == 0) {
    valid_ = false;
    return;
  }
  off_ = ((TreePageHeader) page_)->item_num_ - 1;
  valid_ = true;
}

void PageIterator::Next() {
  off_++;
  valid_ = true;
  if (off_ >= ((TreePageHeader) page_)->item_num_) {
    valid_ = false;
  }
  if (!valid_ && vec_index_ + 1 < page_ids_.size()) {
    tree_->buffer_manager_->UnpinAndRelease(page_ids_[vec_index_]);
    vec_index_++;
    page_ = tree_->buffer_manager_->Pin(page_ids_[vec_index_]);
    if (tree_->print_page_) {
      fprintf(stdout, "leaf-%d-%d\n", page_ids_[vec_index_], ((TreePageHeader) page_)->item_num_);
    }
    pinned_page_ = page_ids_[vec_index_];
    off_ = 0;
    valid_ = true;
  }
}

void PageIterator::Prev() {
  if (off_ <= 0) {
    valid_ = false;
    return;
  }
  off_--;
  valid_ = true;
}

Slice PageIterator::key() const {
  return PageReadPivotAtOffset(page_, off_);
}

Slice PageIterator::value() const {
  return PageReadValueAtOffset(page_, off_);
}

Status PageIterator::status() const { return Status::OK(); }

} // namespace WOT_NAMESPACE