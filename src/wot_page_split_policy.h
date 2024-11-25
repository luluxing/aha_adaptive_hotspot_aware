#ifndef WOT_PAGE_SPLIT_POLICY_H_
#define WOT_PAGE_SPLIT_POLICY_H_

#include <random>
#include "wot_buf_mgr/tree_page.h"

namespace WOT_NAMESPACE {

class PageSplitPolicy {
 public:
  virtual ~PageSplitPolicy() {}
  virtual bool ShouldAllocPage(int existing_num) = 0;
  virtual void Init(int non_dup) = 0;
};

class DefaultSplitPolicy : public PageSplitPolicy {
 public:
  DefaultSplitPolicy(size_t ks, size_t vs) 
  : key_size_(ks),
    value_size_(vs) {}
  
  void Init(int non_dup) override {
    page_cap_ = (PAGE_SIZE - sizeof(TreePageHeader)) /
                    (key_size_ + value_size_ + 3 * ITEMID_SIZE);
  }

  bool ShouldAllocPage(int existing_num) override {
    return existing_num >= page_cap_;
  }

 private:
  size_t key_size_;
  size_t value_size_;
  int page_cap_;
};

class EvenSplitPolicy : public PageSplitPolicy {
 public:
  EvenSplitPolicy(size_t ks, size_t vs) 
  : key_size_(ks),
    value_size_(vs) {}
  
  void Init(int non_dup) {
    cur_page_num_ = 0;
    int page_cap = (PAGE_SIZE - sizeof(TreePageHeader)) /
                    (key_size_ + value_size_ + 3 * ITEMID_SIZE);
    int total_page_num = non_dup / page_cap + (non_dup % page_cap > 0 ? 1 : 0);
    cur_page_cap_ = non_dup / total_page_num + (non_dup % total_page_num > 0 ? 1 : 0);
    // cur_page_cap_ * x + (cur_page_cap_ - 1) * (page_num - x) = non_dup
    page_num_max_entry_ = non_dup - total_page_num * (cur_page_cap_ - 1);
    int page_with_lesser_entry = total_page_num - page_num_max_entry_;
    assert(cur_page_cap_ <= page_cap);
    assert(page_num_max_entry_ * cur_page_cap_ +
          page_with_lesser_entry * (cur_page_cap_ - 1) == non_dup);
  }

  bool ShouldAllocPage(int existing_num) override {
    bool ret = existing_num >= cur_page_cap_;
    if (ret) {
      cur_page_num_++;
      if (cur_page_num_ == page_num_max_entry_) {
        cur_page_cap_--;
      }
    }
    return ret;
  }

 private:
  size_t key_size_;
  size_t value_size_;
  uint64_t cur_page_num_;
  uint64_t cur_page_cap_;
  uint64_t page_num_max_entry_;
};

class SoundRemedySplitPolicy : public PageSplitPolicy {
 public:
  SoundRemedySplitPolicy(size_t ks, size_t vs) 
  : key_size_(ks),
    value_size_(vs),
    last_two_pages_(false) {
    InitializeCDF(); 
  }
  
  void Init(int non_dup) {
    non_dup_ = non_dup;
    last_two_pages_ = false;
    // If the remaining number of entries is less than (3B+1)/2 and at most B
    // put them in one leaf, otherwise use two leaf nodes
    if (non_dup_ <= max_cap_) {
      last_two_pages_ = true;
      cur_page_cap_ = non_dup_;
    } else if (non_dup_ <= (3 * max_cap_ + 1) / 2) {
      last_two_pages_ = true;
      cur_page_cap_ = non_dup_ / 2 + (non_dup_ % 2 > 0 ? 1 : 0);
    } else {
      cur_page_cap_ = GetOnePageCap();
    }
    non_dup_ -= cur_page_cap_;
    assert(cur_page_cap_ <= max_cap_);
  }

  bool ShouldAllocPage(int existing_num) override {
    bool ret = existing_num >= cur_page_cap_;
    if (ret) {
      assert(non_dup_ > 0);
      if (last_two_pages_) {
        cur_page_cap_ = non_dup_;
        assert(cur_page_cap_ <= max_cap_);
      } else {
        if (non_dup_ <= max_cap_) {
          last_two_pages_ = true;
          cur_page_cap_ = non_dup_;
        } else if (non_dup_ <= (3 * max_cap_ + 1) / 2) {
          last_two_pages_ = true;
          cur_page_cap_ = non_dup_ / 2 + (non_dup_ % 2 > 0 ? 1 : 0);
        } else {
          cur_page_cap_ = GetOnePageCap();
        }
      }
      non_dup_ -= cur_page_cap_;
    }
    return ret;
  }

 private:
  void InitializeCDF() {
    // Key includes sequence number
    max_cap_ = (PAGE_SIZE - sizeof(TreePageHeader)) /
                    (key_size_ + value_size_ + 3 * ITEMID_SIZE);
    min_cap_ = 0.5 * max_cap_;
    cdf_.push_back(0.0);
    double prop, cum_prop;
    for (int i = min_cap_; i <= max_cap_; i++) {
      prop = 1.0 * max_cap_ / ((i) * (i+1));
      cum_prop = cdf_[cdf_.size() - 1];
      cdf_.push_back(prop + cum_prop);
    }
  }

  double GetOnePageCap() {
    // Get a random double between 0 and 1
    double p = ((double) rand() / (double) RAND_MAX);
    int j;
    for (j = 1; j < cdf_.size(); j++) {
      if (p < cdf_[j])
        break;;
    }
    if (j >= cdf_.size()) {
      j = cdf_.size() - 1;
    }
    return j - 1 + min_cap_;
  }

  std::vector<double> cdf_;
  size_t key_size_;
  size_t value_size_;
  int max_cap_;
  int min_cap_;
  int non_dup_;
  bool last_two_pages_;
  uint64_t cur_page_cap_;
};

} // namespace WOT_NAMESPACE

#endif // WOT_PAGE_SPLIT_POLICY_H_


