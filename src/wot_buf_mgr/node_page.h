#ifndef NODE_PAGE_WOT_NAMESPACE_H_
#define NODE_PAGE_WOT_NAMESPACE_H_

#include "tree_page.h"

namespace WOT_NAMESPACE {

uint32_t _lower_bound(Page p, const Slice& k);
uint32_t _decode_int32_at(Page p, uint32_t pos);
void _decode_kvpair(Page page, uint32_t offset, char** key,
                      size_t* key_size, char** val, size_t* val_size);
void _decode_pivotpair(Page page, uint32_t offset, char** key,
                             size_t* key_size, uint32_t* child);
void _insert_pivotpair_at(uint32_t offset, Page page,
                          const Slice& key, uint32_t pg_id);
void _insert_kvpair_at(uint32_t offset, Page page,
                       const Slice& key, const Slice& v);
bool _update_val_at(uint32_t offset, Page page, const Slice& v);

// Return the position. 
// -1 means the child id at the tail.
int PageLowerBound(Page p, const Slice& k);
// offset is nonnegative.
void PageReadKeyAtOffset(Page p, const uint32_t offset, Slice& key);
// Page is marked dirty after insertion
void PageInsertKvpair(Page p, const Slice& k, const Slice& v);
// Page is marked dirty after insertion
void PageInsertPivotPair(Page p, const Slice& k, const uint32_t child);
void PageInsertFirstPivotPair(Page p, const Slice& k,
                              const uint32_t left_c, const uint32_t right_c);
// void PageReadValByKey(Page p, const Slice& k, Slice& v);
void PageReadValAt(Page p, const uint32_t offset, Slice& v);

uint32_t PageReadChildAt(Page p, int offset);
// uint32_t PageReadChildByKey(Page p, const Slice& k);

bool InnerPageIsFull(Page p, const Slice& k);
bool LeafPageIsFull(Page p, const Slice& k, const Slice& v);

void GetInnerPageSplitSep(Page p, Slice& s);
void GetLeafPageSplitSep(Page p, Slice& s);
void LeafPageSplit(Page p, Page new_page);
void InnerPageSplit(Page p, Page new_page);

////////////////////////////////////////////////////

int PageLowerBound(Page p, const Slice& k) {
  auto lower = _lower_bound(p, k);
  if (lower > 0) return lower - 1;
  return -1;
}

void PageReadKeyAtOffset(Page p, const uint32_t offset, Slice& key) {
  TreePageHeader h = (TreePageHeader) p;
  if (offset >= h->item_num_) {
    key = Slice();
    return;
  }
  bool leaf = h->is_leaf_;
  if (leaf) {
    char *k, *v;
    size_t ks, vs;
    _decode_kvpair(p, offset, &k, &ks, &v, &vs);
    key = Slice(k, ks);
  } else {
    char *k;
    size_t ks;
    uint32_t c;
    _decode_pivotpair(p, offset, &k, &ks, &c);
    key = Slice(k, ks);
  }
}

void PageInsertKvpair(Page p, const Slice& k, const Slice& v) {
  if (LeafPageIsFull(p, k, v)) {
    fprintf(stderr, "Error: no enough space to insert kv pair\n");
    std::abort();
  }
  int off = 0;
  if (((TreePageHeader) p)->item_num_ > 0) {
    off = _lower_bound(p, k);
  }
  Slice temp_k;
  PageReadKeyAtOffset(p, off, temp_k);
  if (temp_k.compare(k) == 0 && _update_val_at(off, p, v)) {
    // Update the value
  } else {
    _insert_kvpair_at(off, p, k, v);
  }
  ((TreePageHeader) p)->is_dirty_ = true;
  if (temp_k.compare(k) != 0)
    ((TreePageHeader) p)->item_num_++;
}

// Page is marked dirty after insertion
void PageInsertPivotPair(Page p, const Slice& k, const uint32_t child) {
  if (InnerPageIsFull(p, k)) {
    fprintf(stderr, "Error: no enough space to insert index entry\n");
    std::abort();
  }
  assert(((TreePageHeader) p)->item_num_ > 0);
  int off = _lower_bound(p, k);
  _insert_pivotpair_at(off, p, k, child);
  ((TreePageHeader) p)->is_dirty_ = true;
  ((TreePageHeader) p)->item_num_++;
}

void PageInsertFirstPivotPair(Page p, const Slice& k,
                              const uint32_t left_c, const uint32_t right_c) {
  assert(((TreePageHeader) p)->item_num_ == 0);
  assert(((TreePageHeader) p)->free_end_ == PAGE_SIZE);

  char* up = p + PAGE_SIZE - ITEMID_SIZE;
  EncodeFixed32(up, left_c);
  ((TreePageHeader) p)->free_end_ -= ITEMID_SIZE;

  _insert_pivotpair_at(0, p, k, right_c);
  ((TreePageHeader) p)->is_dirty_ = true;
  ((TreePageHeader) p)->item_num_ = 1;
}

uint32_t PageReadChildAt(Page p, int offset) {
  assert(!((TreePageHeader) p)->is_leaf_);
  if (offset < 0) {
    return _decode_int32_at(p, PAGE_SIZE - ITEMID_SIZE);
  }
  char *k;
  size_t ks;
  uint32_t child;
  _decode_pivotpair(p, offset, &k, &ks, &child);
  return child;
}

void PageReadValAt(Page p, const uint32_t offset, Slice& val) {
  assert(offset < ((TreePageHeader) p)->item_num_);
  char *k, *v;
  size_t ks, vs;
  _decode_kvpair(p, offset, &k, &ks, &v, &vs);
  val = Slice(v, vs);
}

void GetLeafPageSplitSep(Page p, Slice& s) {
  int item_num = ((TreePageHeader) p)->item_num_;
  int right_item = item_num - item_num / 2;
  int left_item = item_num - right_item;
  PageReadKeyAtOffset(p, left_item - 1, s);
}

void LeafPageSplit(Page p, Page new_page) {
  int item_num = ((TreePageHeader) p)->item_num_;
  int right_item = item_num - item_num / 2;
  int left_item = item_num - right_item;
  ((TreePageHeader) p)->item_num_ = left_item;

  // start item is included, end item is not included
  for (int off = left_item; off < item_num; off++) {
    char *k, *v;
    size_t ks, vs;
    _decode_kvpair(p, off, &k, &ks, &v, &vs);
    PageInsertKvpair(new_page, Slice(k, ks), Slice(v, vs));
  }

  // Cannot directly adjust the free space as the payload may not align
  // or have the same order as the offset
  Page temp = (Page) malloc(PAGE_SIZE);
  memcpy(temp, p, PAGE_SIZE);
  ((TreePageHeader) p)->item_num_ = 0;
  ((TreePageHeader) p)->free_start_ = PgHdSize;
  ((TreePageHeader) p)->free_end_ = PAGE_SIZE;
  for (int off = 0; off < left_item; off++) {
    char *k, *v;
    size_t ks, vs;
    _decode_kvpair(temp, off, &k, &ks, &v, &vs);
    PageInsertKvpair(p, Slice(k, ks), Slice(v, vs));
  }
  free(temp);
}

void GetInnerPageSplitSep(Page p, Slice& s) {
  int item_num = ((TreePageHeader) p)->item_num_;
  int right_item = item_num - item_num / 2;
  int left_item = item_num - right_item - 1;
  PageReadKeyAtOffset(p, left_item, s);
}

void InnerPageSplit(Page p, Page new_page) {
  int item_num = ((TreePageHeader) p)->item_num_;
  int right_item = item_num - item_num / 2;
  int left_item = item_num - right_item - 1;
  ((TreePageHeader) p)->item_num_ = left_item;

  uint32_t right_fst = PageReadChildAt(p, left_item);
  for (int off = left_item + 1; off < item_num; off++) {
    char *k;
    size_t ks;
    uint32_t child;
    _decode_pivotpair(p, off, &k, &ks, &child);
    if (off == left_item + 1) {
      PageInsertFirstPivotPair(new_page, Slice(k, ks), right_fst, child);
    } else {
      PageInsertPivotPair(new_page, Slice(k, ks), child);
    }
  }
  assert(((TreePageHeader) new_page)->item_num_ == right_item);

  // Cannot directly adjust the free space as the payload may not align
  // or have the same order as the offset
  Page temp = (Page) malloc(PAGE_SIZE);
  memcpy(temp, p, PAGE_SIZE);
  ((TreePageHeader) p)->item_num_ = 0;
  ((TreePageHeader) p)->free_start_ = PgHdSize;
  ((TreePageHeader) p)->free_end_ = PAGE_SIZE;
  for (int off = 0; off < left_item; off++) {
    char *k;
    size_t ks;
    uint32_t child;
    _decode_pivotpair(temp, off, &k, &ks, &child);
    if (off == 0) {
      uint32_t fst = _decode_int32_at(temp, PAGE_SIZE - ITEMID_SIZE);
      PageInsertFirstPivotPair(p, Slice(k, ks), fst, child);
    } else {
      PageInsertPivotPair(p, Slice(k, ks), child);
    }
  }
  free(temp);
}

bool InnerPageIsFull(Page p, const Slice& k) {
  // One entry requires one offset, the key length, the key and the child
  auto free_space = ((TreePageHeader) p)->free_end_ - ((TreePageHeader) p)->free_start_;
  if (free_space < k.size() + ITEMID_SIZE * 3) {
    return true;
  }
  return false;
}

bool LeafPageIsFull(Page p, const Slice& k, const Slice& v) {
  // One entry requires one offset, the key length, the key, the value length and the value
  auto free_space = ((TreePageHeader) p)->free_end_ - ((TreePageHeader) p)->free_start_;
  if (free_space < k.size() + v.size() + ITEMID_SIZE * 3) {
    return true;
  }
  return false;

}

uint32_t _lower_bound(Page p, const Slice& k) {
  uint32_t lower = 0;
  uint32_t upper = ((TreePageHeader) p)->item_num_;
  do {
    uint32_t mid = (upper - lower) / 2 + lower;
    Slice mid_key;
    PageReadKeyAtOffset(p, mid, mid_key);
    if (k.compare(mid_key) < 0) {
      upper = mid;
    } else if (k.compare(mid_key) > 0) {
      lower = mid + 1;
    } else {
      return mid;
    }
  } while (lower < upper);
  return lower;
}

uint32_t _decode_int32_at(Page p, uint32_t pos) {
  uint32_t num = DecodeFixed32(p + pos);
  return num;
}

void _decode_pivotpair(Page page, uint32_t offset, char** key,
                             size_t* key_size, uint32_t* child) {
  uint32_t pos = _decode_int32_at(page,
                  PgHdSize + offset * ITEMID_SIZE);
  *key_size = _decode_int32_at(page, pos);
  *key = page + pos + ITEMID_SIZE;
  *child = _decode_int32_at(page, pos + ITEMID_SIZE + (*key_size));
}

void _decode_kvpair(Page page, uint32_t offset, char** key,
                      size_t* key_size, char** val, size_t* val_size) {
  uint32_t pos = _decode_int32_at(page,
                  PgHdSize + offset * ITEMID_SIZE);
  *key_size = _decode_int32_at(page, pos);
  *key = page + pos + ITEMID_SIZE;
  *val_size = _decode_int32_at(page, pos + ITEMID_SIZE + (*key_size));
  *val = page + pos + 2 * ITEMID_SIZE +(*key_size);
}


void _insert_pivotpair_at(uint32_t offset, Page page,
                          const Slice& key, uint32_t pg_id) {
  TreePageHeader h = (TreePageHeader) page;
  bool need_shift = offset < h->item_num_;
  
  // Write IndexEntry at the end: key_len (4B); key (var len); pg_id (4B)
  uint32_t item_size = key.size();
  uint32_t encoded_len = item_size + ITEMID_SIZE * 2;
  char* up = page + (h->free_end_ - encoded_len);
  EncodeFixed32(up, item_size);
  char* p = up + ITEMID_SIZE;
  std::memcpy(p, key.data(), item_size);
  p += item_size;
  EncodeFixed32(p, pg_id);
  h->free_end_ -= encoded_len;
  assert(up + encoded_len == p + ITEMID_SIZE);

  // Shift the pointer part at the front
  char* low = page + PgHdSize + ITEMID_SIZE * offset;
  if (need_shift) {
    memmove(low + ITEMID_SIZE, low,
                 (h->item_num_ - offset) * ITEMID_SIZE);
  }
  EncodeFixed32(low, h->free_end_);
  h->free_start_ += sizeof(h->free_end_);
}

bool _update_val_at(uint32_t offset, Page page, const Slice& val) {
  TreePageHeader h = (TreePageHeader) page;
  h->is_dirty_ = true;
  uint32_t new_val_size = val.size();
  uint32_t old_pos = _decode_int32_at(page,
                                  PgHdSize + offset * ITEMID_SIZE);
  uint32_t key_size = _decode_int32_at(page, old_pos);
  uint32_t old_val_size = _decode_int32_at(page,
                                old_pos + ITEMID_SIZE + key_size);
  if (new_val_size == old_val_size) {
    char* p = page + old_pos + ITEMID_SIZE * 2 + key_size;
    std::memcpy(p, val.data(), new_val_size);
    return true;
  }
  return false;
}

void _insert_kvpair_at(uint32_t offset, Page page,
                       const Slice& key, const Slice& val) {
  TreePageHeader h = (TreePageHeader) page;
  Slice ori_k;
  PageReadKeyAtOffset(page, offset, ori_k);
  bool need_shift = offset < h->item_num_;
  if (need_shift) {
    need_shift = ori_k.compare(key) != 0;
  }
  
  // Write LeafEntry at the end:
  // key_len (4B); key (var len); val_len (4B); val (var len)
  uint32_t item_size = key.size();
  uint32_t val_size = val.size();
  uint32_t encoded_len = item_size + ITEMID_SIZE * 2 + val_size;
  char* up = page + (h->free_end_ - encoded_len);
  EncodeFixed32(up, item_size);
  char* p = up + ITEMID_SIZE;
  std::memcpy(p, key.data(), item_size);
  p += item_size;
  EncodeFixed32(p, val_size);
  p += ITEMID_SIZE;
  std::memcpy(p, val.data(), val_size);
  h->free_end_ -= encoded_len;
  assert(up + encoded_len == p + val_size);

  // Shift the pointer part at the front
  char* low = page + PgHdSize + ITEMID_SIZE * offset;
  if (need_shift) {
    memmove(low + ITEMID_SIZE, low,
                 (h->item_num_ - offset) * ITEMID_SIZE);
  }
  EncodeFixed32(low, h->free_end_);
  h->free_start_ += sizeof(h->free_end_);
}

} // namespace WOT_NAMESPACE

#endif