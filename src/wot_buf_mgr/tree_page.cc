#include <iostream>
#include <sstream>
#include "tree_page.h"


namespace WOT_NAMESPACE {


void PageInit(Page page, uint32_t pg_id) {
  TreePageHeader h = (TreePageHeader) page;
  h->page_id_ = pg_id;
  h->item_num_ = 0;
  h->left_ = 0;
  h->right_ = 0;
  h->free_start_ = sizeof(TreePageHeaderData);
  h->free_end_ = PAGE_SIZE;
  h->is_leaf_ = false;
  h->is_dirty_ = true;
}

size_t PageGetFreeSpace(Page page) {
  size_t space;
  space = ((TreePageHeader) page)->free_end_ -
              ((TreePageHeader) page)->free_start_;

  if (space < ITEMID_SIZE)
      return 0;
  return space;
}

uint32_t _page_decode_int32_at(Page p, uint32_t pos) {
  uint32_t num = DecodeFixed32(p + pos);
  return num;
}

Slice PageReadPivotAtOffset(Page page, uint32_t offset) {
  TreePageHeader h = (TreePageHeader) page;
  if (offset >= h->item_num_) {
    return Slice();
  }
  bool leaf = h->is_leaf_;
  if (leaf) {
    char *k, *v;
    size_t ks, vs;
    _page_decode_leafEntry(page, offset, &k, &ks, &v, &vs);
    return Slice(k, ks);
  } else {
    char *k;
    size_t ks;
    uint32_t c;
    _page_decode_indexEntry(page, offset, &k, &ks, &c);
    return Slice(k, ks);
  }
}

Slice PageReadValueAtOffset(Page page, uint32_t offset) {
  TreePageHeader h = (TreePageHeader) page;
  bool leaf = h->is_leaf_;
  if (offset >= h->item_num_ || !leaf) return Slice();
  char *k, *v;
  size_t ks, vs;
  _page_decode_leafEntry(page, offset, &k, &ks, &v, &vs);
  return Slice(v, vs);
}

int _page_compare(Page page, const Slice& key, uint32_t off) {
  assert(off >= 0 && off < ((TreePageHeader) page)->item_num_);
  Slice target = PageReadPivotAtOffset(page, off);
  return key.compare(target);
}

// Binary search and we use user_key for comparison.
// We need to guarantee that key has not appeared in page before.
uint32_t _page_bin_search_insert(Page page, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  if (h->item_num_ == 0) return 0;
  uint32_t high = h->item_num_ - 1;
  uint32_t low = 0;
  int result;
  while (low < high) {
    int mid = low + ((high - low) / 2);
    result = _page_compare(page, key, mid);
    if (result == 0) {
      return mid;
    } else if (result > 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (_page_compare(page, key, low) > 0) {
    return low + 1;
  }
  return low;
}

uint32_t _page_bin_search_with_seq(Page page, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  if (h->item_num_ == 0) return 0;
  uint32_t high = h->item_num_ - 1;
  uint32_t low = 0;
  int result;
  Slice pure_key = Slice(key.data(), key.size() - 8);
  while (low < high) {
    int mid = low + ((high - low) / 2);
    Slice mid_key = PageReadPivotAtOffset(page, mid);
    Slice pure_mid_key = Slice(mid_key.data(), mid_key.size() - 8);
    result = pure_key.compare(pure_mid_key);
    if (result == 0) {
      return mid;
    } else if (result > 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  Slice low_key = PageReadPivotAtOffset(page, low);
  Slice pure_low_key = Slice(low_key.data(), low_key.size() - 8);
  if (pure_key.compare(pure_low_key) > 0) {
    return low + 1;
  }
  return low;
}

uint32_t _page_bin_search(Page page, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  if (h->item_num_ == 0) return 0;
  uint32_t high = h->item_num_ - 1;
  uint32_t low = 0;
  int result;
  while (low < high) {
    int mid = low + ((high - low) / 2);
    result = _page_compare(page, key, mid);
    if (result == 0) {
      return mid;
    } else if (result > 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  assert(low < h->item_num_);
  if (_page_compare(page, key, low) >= 0) {
    // key is greater than the largest pivot in the page
    return low;
  }
  if (low == 0) {
    // key is smaller than the smallest pivot in the page
    return low;
  }
  assert(low > 0 && low < h->item_num_);
  return low - 1;
}

// Assume that the new value fits into the old slot
bool PageUpdatePivotAt(Page page, uint32_t offset, const Slice& val) {
  TreePageHeader h = (TreePageHeader) page;
  
  h->is_dirty_ = true;
  uint32_t new_val_size = val.size();
  uint32_t old_pos = _page_decode_int32_at(page,
                                  PgHdSize + offset * ITEMID_SIZE);
  uint32_t key_size = _page_decode_int32_at(page, old_pos);
  uint32_t old_val_size = _page_decode_int32_at(page,
                                old_pos + ITEMID_SIZE + key_size);
  if (new_val_size == old_val_size) {
    char* p = page + old_pos + ITEMID_SIZE * 2 + key_size;
    std::memcpy(p, val.data(), new_val_size);
    return true;
  }
  std::cerr << "Error: new value cannot fit in\n";
  return false;
}

// Used to update the minimum pivot in the page as it does
// not compare with the other pivots for a correct slot
bool PageUpdateMinPivot(Page page, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  
  h->is_dirty_ = true;
  uint32_t new_item_size = key.size();
  uint32_t old_pos = _page_decode_int32_at(page, PgHdSize);
  uint32_t old_item_size = _page_decode_int32_at(page, old_pos);
  if (new_item_size == old_item_size) {
    char* p = page + old_pos + ITEMID_SIZE;
    std::memcpy(p, key.data(), new_item_size);
    return true;
  }
  // Otherwise we have to shift the items to make items compact
  uint32_t child_pg = _page_decode_int32_at(page,
                      old_pos + ITEMID_SIZE + old_item_size);
  
  uint32_t new_encoded_len = new_item_size + ITEMID_SIZE * 2;
  uint32_t old_encoded_len = old_item_size + ITEMID_SIZE * 2;
  uint32_t chunk_size = PAGE_SIZE - h->free_end_ - old_encoded_len;
  uint32_t old_free_end = h->free_end_;
  h->free_end_ = PAGE_SIZE - chunk_size - new_encoded_len;
  memmove(page + h->free_end_,
          page + old_free_end, chunk_size);
  char* new_pos = page + h->free_end_ + chunk_size;
  EncodeFixed32(new_pos, new_item_size);
  new_pos += ITEMID_SIZE;
  std::memcpy(new_pos, key.data(), new_item_size);
  EncodeFixed32(new_pos + new_item_size, child_pg);
  // Update the item id
  EncodeFixed32(page + PgHdSize, h->free_end_ + chunk_size);
  return true;
}

void _page_write_indexEntry_at(uint32_t offset, Page page,
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

void _page_write_leafEntry_at(uint32_t offset, Page page,
                          const Slice& key, const Slice& val) {
  TreePageHeader h = (TreePageHeader) page;
  bool need_shift = offset < h->item_num_;
  
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

void PageInsertIndexEntry(Page page, const Slice& key, uint32_t pg_id) {
  if (PageGetFreeSpace(page) <= key.size() + ITEMID_SIZE * 3) {
    fprintf(stderr, "Error: no enough space to insert index entry\n");
    std::abort();
  }
  int off = _page_bin_search_insert(page, key);
  _page_write_indexEntry_at(off, page, key, pg_id);
  ((TreePageHeader) page)->is_dirty_ = true;
  ((TreePageHeader) page)->item_num_++;
}

void PageInsertLeafEntry(Page page, const Slice& key, const Slice& val) {
  if (PageGetFreeSpace(page) <= key.size() + val.size() + ITEMID_SIZE * 3) {
    fprintf(stderr, "Error: no enough space to insert leaf entry\n");
    std::abort();
  }
  int off = _page_bin_search_insert(page, key);
  if (key.compare(PageReadPivotAtOffset(page, off)) == 0) {
    PageUpdatePivotAt(page, off, val);
    return;
  }
  _page_write_leafEntry_at(off, page, key, val);
  ((TreePageHeader) page)->is_dirty_ = true;
  ((TreePageHeader) page)->item_num_++;
}

bool PageInsertLeafEntryWithSeq(Page page, const Slice& key, const Slice& val) {
  if (PageGetFreeSpace(page) <= key.size() + val.size() + ITEMID_SIZE * 3) {
    fprintf(stderr, "Error: no enough space to insert leaf entry w/ seq\n");
    std::abort();
  }
  // Check existence first
  int off2 = _page_bin_search_with_seq(page, key);
  uint32_t item_num = ((TreePageHeader) page)->item_num_;
  if (off2 >= 0 && off2 < ((TreePageHeader) page)->item_num_) {
    Slice k = PageReadPivotAtOffset(page, off2);
    if (Slice(key.data(), key.size() - 8).compare(Slice(k.data(), k.size() - 8)) == 0) {
      PageUpdatePivotAt(page, off2, val);
      PageUpdateKeyAt(page, off2, key);
      ((TreePageHeader) page)->is_dirty_ = true;
      assert(item_num == ((TreePageHeader) page)->item_num_);
      return false;
    }
  }
  int off = _page_bin_search_insert(page, key);
  _page_write_leafEntry_at(off, page, key, val);
  ((TreePageHeader) page)->is_dirty_ = true;
  ((TreePageHeader) page)->item_num_++;
  return true;
}

void PageAppendLeafEntry(Page page, const Slice& key, const Slice& val) {
  if (PageGetFreeSpace(page) <= key.size() + val.size() + ITEMID_SIZE * 3) {
    fprintf(stderr, "Error: no enough space to insert leaf entry w/ seq\n");
    std::abort();
  }
  int off = ((TreePageHeader) page)->item_num_;
  _page_write_leafEntry_at(off, page, key, val);
  ((TreePageHeader) page)->is_dirty_ = true;
  ((TreePageHeader) page)->item_num_++;
}

uint32_t PageFindOffsetInsert(Page page, const Slice& key) {
  return _page_bin_search_insert(page, key);
}

int PageFindOffset(Page page, const Slice& key) {
  int offset = _page_bin_search(page, key);
  if (((TreePageHeader) page)->is_leaf_ && 
        key.compare(PageReadPivotAtOffset(page, offset)) != 0)
    return -1;
  return offset;
}

// In btree.cc
int PageFindOffsetForScan(Page page, const Slice& key) {
  int offset = _page_bin_search(page, key);
  assert(((TreePageHeader) page)->is_leaf_ == true);
  if (key.compare(PageReadPivotAtOffset(page, offset)) != 0) {
    int item_num = ((TreePageHeader) page)->item_num_;
    offset = offset + 1 >= item_num ? offset : offset + 1;
  }
  return offset;
}

uint32_t PageReadChildAtOffset(Page page, uint32_t offset) {
  TreePageHeader h = (TreePageHeader) page;
  bool leaf = h->is_leaf_;
  if (leaf || offset >= h->item_num_) return 0;
  char *k;
  size_t ks;
  uint32_t c;
  _page_decode_indexEntry(page, offset, &k, &ks, &c);
  return c;
}

void PageMoveLeafEntries(Page src, Page dest, uint32_t start, uint32_t end) {
  TreePageHeader dest_h = (TreePageHeader) dest;
  // Move the value part.
  // As values may not be contiguous, we have to move them one by one
  size_t total_val_size = 0;
  for (int off = start; off < end; off++) {
    char *k, *v;
    size_t ks, vs;
    _page_decode_leafEntry(src, off, &k, &ks, &v, &vs);
    PageInsertLeafEntry(dest, Slice(k, ks), Slice(v, vs));
  }
  ((TreePageHeader) dest)->is_dirty_ = true;
}

void PageClearContent(Page page) {
  TreePageHeader h = (TreePageHeader) page;
  h->item_num_ = 0;
  h->free_start_ = sizeof(TreePageHeaderData);
  h->free_end_ = PAGE_SIZE;
  h->is_dirty_ = true;
}

void PageMoveIndexEntries(Page src, Page dest, uint32_t start, uint32_t end) {
  TreePageHeader dest_h = (TreePageHeader) dest;
  // Move the value part.
  // As values may not be contiguous, we have to move them one by one
  size_t total_val_size = 0;
  for (int off = start; off < end; off++) {
    char *k;
    size_t ks;
    uint32_t child;
    _page_decode_indexEntry(src, off, &k, &ks, &child);
    PageInsertIndexEntry(dest, Slice(k, ks), child);
  }
  ((TreePageHeader) dest)->is_dirty_ = true;
}

bool PageRemoveEntry(Page page, const Slice& key) {
  int offset = _page_bin_search(page, key);
  if (key.compare(PageReadPivotAtOffset(page, offset)) != 0)
    return false; // not exist
  PageRemoveOffset(page, offset);
  ((TreePageHeader) page)->is_dirty_ = true;
  return true;
}

void PageRemoveOffset(Page page, uint32_t offset) {
  TreePageHeader h = (TreePageHeader) page;
  bool leaf_page = h->is_leaf_;
  uint32_t pos = _page_decode_int32_at(page, PgHdSize + offset * ITEMID_SIZE);

  // Move item ids
  if (offset + 1 < h->item_num_) {
    char* low = page + PgHdSize +
                    (offset + 1) * ITEMID_SIZE;
    memmove(low - ITEMID_SIZE, low,
                  (h->item_num_ - offset - 1) * ITEMID_SIZE);
  }
  h->free_start_ -= ITEMID_SIZE;
  // Update the value part
  char* up = page + h->free_end_;
  size_t gap_size = 0;
  if (leaf_page) {
    char *k, *v;
    size_t ks, vs;
    _page_decode_leafEntry(page, offset, &k, &ks, &v, &vs);
    gap_size = ks + vs + 2 * ITEMID_SIZE;
  } else {
    char *k;
    size_t ks;
    uint32_t c;
    _page_decode_indexEntry(page, offset, &k, &ks, &c);
    gap_size = ks + 2 * ITEMID_SIZE;
  }
  memmove(up + gap_size, up, pos - h->free_end_);
  h->free_end_ += gap_size;
  
  // Update Item ids
  h->item_num_--;
  for (int i = 0; i < h->item_num_; i++) {
    uint32_t p = _page_decode_int32_at(page, PgHdSize + i * ITEMID_SIZE);
    if (p < pos)
      p += gap_size;
    EncodeFixed32(page + PgHdSize + i * ITEMID_SIZE, p);
  }
  h->is_dirty_ = true;
}

int PageGetOffsetByChild(Page page, uint32_t child) {
  TreePageHeader h = (TreePageHeader) page;
  assert(h->is_leaf_ == false);
  for (int offset = 0; offset < h->item_num_; offset++) {
    char *k;
    size_t ks;
    uint32_t c;
    _page_decode_indexEntry(page, offset, &k, &ks, &c);
    if (c == child)
      return offset;
  }
  return -1;
}

void PageUpdateChild(Page page, const Slice& pivot, uint32_t child) {
  TreePageHeader h = (TreePageHeader) page;
  assert(h->is_leaf_ == false);
  int offset = PageFindOffset(page, pivot);
  assert(offset >= 0 && offset < h->item_num_);
  // _page_write_indexEntry_at(offset, page, pivot, child);
  uint32_t pos = _page_decode_int32_at(page,
                  PgHdSize + offset * ITEMID_SIZE);
  size_t key_size = _page_decode_int32_at(page, pos);
  char* child_p = page + pos + key_size + ITEMID_SIZE;
  EncodeFixed32(child_p, child);
  ((TreePageHeader) page)->is_dirty_ = true;
}

void _page_decode_indexEntry(Page page, uint32_t offset, char** key,
                             size_t* key_size, uint32_t* child) {
  uint32_t pos = _page_decode_int32_at(page,
                  PgHdSize + offset * ITEMID_SIZE);
  *key_size = _page_decode_int32_at(page, pos);
  *key = page + pos + ITEMID_SIZE;
  *child = _page_decode_int32_at(page, pos + ITEMID_SIZE + (*key_size));
}

void _page_decode_leafEntry(Page page, uint32_t offset, char** key,
                      size_t* key_size, char** val, size_t* val_size) {
  uint32_t pos = _page_decode_int32_at(page,
                  PgHdSize + offset * ITEMID_SIZE);
  *key_size = _page_decode_int32_at(page, pos);
  *key = page + pos + ITEMID_SIZE;
  *val_size = _page_decode_int32_at(page, pos + ITEMID_SIZE + (*key_size));
  *val = page + pos + 2 * ITEMID_SIZE +(*key_size);
}

void PagePrint(Page page) {
  TreePageHeader h = (TreePageHeader) page;
  bool leaf = h->is_leaf_;
  // Use sstream to print the page as a whole
  std::stringstream ss;
  ss << (leaf ? "leaf " : "non-leaf ") << h->page_id_
            << " has " << h->item_num_ << " entries\n";
  for (int i = 0; i < h->item_num_; i++) {
    if (leaf) {
      char *k, *v;
      size_t ks, vs;
      _page_decode_leafEntry(page, i, &k, &ks, &v, &vs);
      ss << Slice(k, ks-8).ToString() << "=>"
                << Slice(v, vs).ToString().substr(0, 1);
    } else {
      char *k;
      size_t ks;
      uint32_t c;
      _page_decode_indexEntry(page, i, &k, &ks, &c);
      ss << Slice(k, ks).ToString() << "=>"
                << c;
    }
    ss << "; ";
  }
  ss << "\n";
  std::cout << ss.str();
}

void PagePrintStat(Page page) {
  TreePageHeader h = (TreePageHeader) page;
  bool leaf = h->is_leaf_;
  std::cout << (leaf ? "leaf " : "non-leaf ");
  int item_num = h->item_num_;
  Slice min_key = PageReadPivotAtOffset(page, 0);
  Slice max_key = PageReadPivotAtOffset(page, item_num - 1);
  std::cout << h->page_id_ << " has " << item_num << " entries: ["
            << min_key.ToString() << ", " << max_key.ToString()
            << "]\n";
}

bool PageIsRightMost(Page page) {
  TreePageHeader h = (TreePageHeader) page;
  return h->right_ == 0;
}

int PageMinAndKey(Page page, const Slice& key) {
  Slice minkey = PageReadPivotAtOffset(page, 0);
  return minkey.compare(key);
}

uint32_t _page_bin_search_highkey(Page page, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  if (h->item_num_ == 0) return 0;
  uint32_t high = h->item_num_ - 1;
  uint32_t low = 0;
  int result;
  while (low < high) {
    int mid = low + ((high - low) / 2);
    result = _page_compare(page, key, mid);
    if (result == 0) {
      return mid;
    } else if (result > 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

int PageFindOffsetForScanHighkey(Page page, const Slice& key) {
  int offset = _page_bin_search_highkey(page, key);
  assert(((TreePageHeader) page)->is_leaf_ == true);
  return offset;
}

int PageFindOffsetHighkey(Page page, const Slice& key) {
  int offset = _page_bin_search_highkey(page, key);
  if (((TreePageHeader) page)->is_leaf_ && 
        key.compare(PageReadPivotAtOffset(page, offset)) != 0)
    return -1;
  return offset;
}

Slice PageHighkey(Page page) {
  uint32_t item_num = ((TreePageHeader) page)->item_num_;
  return PageReadPivotAtOffset(page, item_num - 1);
}

Slice PageLowkey(Page page) {
  assert(((TreePageHeader) page)->item_num_ > 0);
  return PageReadPivotAtOffset(page, 0);
}

// Used to update the highkey in the page as it does
// not compare with the other pivots for a correct slot
bool PageUpdateHighkey(Page page, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  int offset = h->item_num_ - 1;
  
  uint32_t new_item_size = key.size();
  uint32_t old_pos = _page_decode_int32_at(page, PgHdSize + offset * ITEMID_SIZE);
  uint32_t old_item_size = _page_decode_int32_at(page, old_pos);
  // Assume both old and new keys are of equal size
  if (new_item_size == old_item_size) {
    char* p = page + old_pos + ITEMID_SIZE;
    std::memcpy(p, key.data(), new_item_size);
    h->is_dirty_ = true;
    return true;
  }
  return false;
}

bool PageUpdateKeyAt(Page page, uint32_t offset, const Slice& key) {
  TreePageHeader h = (TreePageHeader) page;
  assert(offset >= 0 && offset < h->item_num_);
  uint32_t new_item_size = key.size();
  uint32_t old_pos = _page_decode_int32_at(page,
                                  PgHdSize + offset * ITEMID_SIZE);
  uint32_t old_item_size = _page_decode_int32_at(page, old_pos);
  if (new_item_size == old_item_size) {
    char* p = page + old_pos + ITEMID_SIZE;
    std::memcpy(p, key.data(), new_item_size);
    h->is_dirty_ = true;
    return true;
  }
  fprintf(stderr, "Error: new key cannot fit in\n");
  return false;
}

// Assumes page is packed with internal keys (user key + seq no)
// with values and the search key is also internal key
int PageFindOffsetBol(Page page, const Slice& key) {
  int offset = _page_bin_search(page, key);
  Slice k = PageReadPivotAtOffset(page, offset);
  if (Slice(key.data(), key.size() - 8).compare(Slice(k.data(), k.size() - 8)) > 0) {
    offset++;
  }
  return offset;
}

} // namespace WOT_NAMESPACE
