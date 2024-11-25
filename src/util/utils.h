#ifndef WOT_UTILS_H
#define WOT_UTILS_H

#include "wot_index.h"

namespace WOT_NAMESPACE {

inline char* NewPivot(ArenaWrapper& arena, Slice key) {
  char* buf = arena.Allocate(key.size());
  std::memcpy(buf, key.data(), key.size());
  return buf;
}

inline int CountNonDuplicates(const InternalKeyComparator* icmp,
                       Iterator* iter, size_t* key_size, size_t* val_size) {
  int non_dup = 0;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  iter->SeekToFirst();
  while (iter->Valid()) {
    auto key = iter->key();
    if (*key_size == 0)
      *key_size = key.size();
    if (*val_size == 0) {
      *val_size = iter->value().size();
    }
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          icmp->user_comparator()->Compare(ikey.user_key,
                                            Slice(current_user_key)) != 0) {
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }
      if (last_sequence_for_key < kMaxSequenceNumber) {
        drop = true;
      } else if (ikey.type == kTypeDeletion) {
        drop = true;
      }
      last_sequence_for_key = ikey.sequence;
    }
    if (!drop) {
      non_dup++;
    }
    iter->Next();
  }
  return non_dup;
}

// TODO: consider deleted value
inline SlicePageMap WriteToPage(BplusTree* tree, const InternalKeyComparator* icmp,
                                Iterator* leaf_iter, uint64_t entry_num) {
  SlicePageMap result;
  uint32_t new_pg_id = 0;
  Page new_page = nullptr;
  int page_item = 0;
  ParsedInternalKey ikey;
  std::string current_user_key;
  bool has_current_user_key = false;
  SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
  // Count the number of non-duplicates
  size_t key_size = 0, val_size = 0;
  int non_dup = CountNonDuplicates(icmp, leaf_iter, &key_size, &val_size);

  // Compute the number of pages needed
  assert(key_size > 0 && val_size > 0);
  PageSplitPolicy* policy = tree->GetPageSplitPolicy(key_size, val_size);
  policy->Init(non_dup);
  // Write the non-duplicates to the new page
  leaf_iter->SeekToFirst();
  while (leaf_iter->Valid()) {
    auto key = leaf_iter->key();
    auto val = leaf_iter->value();
    bool drop = false;
    if (!ParseInternalKey(key, &ikey)) {
      current_user_key.clear();
      has_current_user_key = false;
      last_sequence_for_key = kMaxSequenceNumber;
    } else {
      if (!has_current_user_key ||
          icmp->user_comparator()->Compare(ikey.user_key,
                                            Slice(current_user_key)) != 0) {
        current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
        has_current_user_key = true;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if (last_sequence_for_key < kMaxSequenceNumber) {
        drop = true;  // (A)
      } else if (ikey.type == kTypeDeletion) {
        drop = true;
      }

      last_sequence_for_key = ikey.sequence;
    }
    if (!drop) {
      // size_t needed = key.size() + val.size() + 3 * ITEMID_SIZE;
      // bool has_space = new_pg_id != 0 && PageGetFreeSpace(new_page) > 0 &&
      //                  PageGetFreeSpace(new_page) >= page_empty_size + needed;
      // if (new_pg_id == 0 || page_item >= page_entry_num) {
      // if (new_pg_id == 0 || page_item >= page_cap) {
      if (new_pg_id == 0 || policy->ShouldAllocPage(page_item)) {
        if (new_pg_id != 0) {
          tree->GetBufferManager()->UnpinAndRelease(new_pg_id);
        }
        new_pg_id = tree->GetBufferManager()->Allocate();
        page_item = 0;
        new_page = tree->GetBufferManager()->Pin(new_pg_id);
        ((TreePageHeader) new_page)->is_leaf_ = true;
        Slice piv = Slice(NewPivot(tree->GetArenaWrapper(), ExtractUserKey(key)),
                          ExtractUserKey(key).size());
        result[piv] = new_pg_id;
      }
      PageAppendLeafEntry(new_page, key, val);
      page_item++;
    }
    leaf_iter->Next();
  }
  // assert(result.size() == page_num);
  tree->GetBufferManager()->UnpinAndRelease(new_pg_id);
  delete leaf_iter;
  leaf_iter = nullptr;
  return result;
}

} // namespace WOT_NAMESPACE

#endif // WOT_UTILS_H