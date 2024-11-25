#ifndef TREE_PAGE_WOT_NAMESPACE_H_
#define TREE_PAGE_WOT_NAMESPACE_H_

#define PAGE_SIZE 8192//16384 // 8kB=8*1024=8192
#define ITEMID_SIZE 4

#include <atomic>
#include <immintrin.h>

#include "leveldb/util/coding.h"

namespace WOT_NAMESPACE {

typedef char *Pointer;
typedef Pointer Page;

// Page structure: Lock + PageID + IsLeaf + ItemSize + LeftSiblingID + rightSiblingID +
//                 Key (left to right) + FreeSpace + Child/Value(right to left)

// Inner page: Header + child + off + child + ... + key (key-len + key)

typedef struct OptLock {
  std::atomic<uint64_t> type_version_lock_obsolete{0b100};

  void ClearLock() {
    type_version_lock_obsolete.store(0b100);
  }

  bool IsLocked(uint64_t version) {
    return ((version & 0b10) == 0b10);
  }

  bool IsObsolete(uint64_t version) {
    return (version & 1) == 1;
  }

  uint64_t ReadLockOrRestart(bool &needRestart) {
    uint64_t version;
    version = type_version_lock_obsolete.load();
    if (IsLocked(version) || IsObsolete(version)) {
      _mm_pause();
      needRestart = true;
    }
    return version;
  }

  void WriteLockOrRestart(bool &needRestart) {
    uint64_t version;
    version = ReadLockOrRestart(needRestart);
    if (needRestart) return;

    UpgradeToWriteLockOrRestart(version, needRestart);
    if (needRestart) return;
  }

  void UpgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart) {
    if (type_version_lock_obsolete.compare_exchange_strong(version, version + 0b10)) {
      version = version + 0b10;
    } else {
      _mm_pause();
      needRestart = true;
    }
  }

  void WriteUnlock() {
    type_version_lock_obsolete.fetch_add(0b10);
  }


  void CheckOrRestart(uint64_t startRead, bool &needRestart) const {
    ReadUnlockOrRestart(startRead, needRestart);
  }

  void ReadUnlockOrRestart(uint64_t startRead, bool &needRestart) const {
    needRestart = (startRead != type_version_lock_obsolete.load());
  }

  void WriteUnlockObsolete() {
    type_version_lock_obsolete.fetch_add(0b11);
  }
} OptLock;

typedef struct TreePageHeaderData {
  OptLock opt_lock_;
  uint32_t page_id_;
  uint32_t item_num_; // For inner page, it is the number of pivots
  uint32_t left_;
  uint32_t right_;
  uint32_t free_start_;
  uint32_t free_end_;
  bool is_leaf_;
  bool is_dirty_;

  uint64_t ReadLockOrRestart(bool &needRestart) {
    return opt_lock_.ReadLockOrRestart(needRestart);
  }

  void WriteLockOrRestart(bool &needRestart) {
    opt_lock_.WriteLockOrRestart(needRestart);
  }

  void UpgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart) {
    opt_lock_.UpgradeToWriteLockOrRestart(version, needRestart);
  }

  void WriteUnlock() {
    opt_lock_.WriteUnlock();
  }

  void ClearLock() {
    opt_lock_.ClearLock();
  }

  void CheckOrRestart(uint64_t startRead, bool &needRestart) const {
    opt_lock_.ReadUnlockOrRestart(startRead, needRestart);
  }

  void ReadUnlockOrRestart(uint64_t startRead, bool &needRestart) const {
    opt_lock_.ReadUnlockOrRestart(startRead, needRestart);
  }

  void WriteUnlockObsolete() {
    opt_lock_.WriteUnlockObsolete();
  }
} TreePageHeaderData;

typedef TreePageHeaderData *TreePageHeader;

const size_t PgHdSize = sizeof(TreePageHeaderData);

// The following functions are used publicly
void PageInit(Page page, uint32_t pg_id);

size_t PageGetFreeSpace(Page page);

Slice PageReadPivotAtOffset(Page page, uint32_t offset);

uint32_t PageReadChildAtOffset(Page page, uint32_t offset);

Slice PageReadValueAtOffset(Page page, uint32_t offset);

bool PageUpdateMinPivot(Page page, const Slice& key);

bool PageUpdatePivotAt(Page page, uint32_t offset, const Slice& val);

void PageInsertIndexEntry(Page page, const Slice& key, uint32_t pg_id);
void PageInsertLeafEntry(Page page, const Slice& key, const Slice& val);
bool PageInsertLeafEntryWithSeq(Page page, const Slice& key, const Slice& val);
// Append without binary search. Assume a sorted series of entries.
void PageAppendLeafEntry(Page page, const Slice& key, const Slice& val);

bool PageRemoveEntry(Page page, const Slice& key);

void PageRemoveOffset(Page page, uint32_t offset);

// Move all entries with offset between start and end to destination
// [start, end)
void PageMoveLeafEntries(Page src, Page dest, uint32_t start, uint32_t end);
void PageMoveIndexEntries(Page src, Page dest, uint32_t start, uint32_t end);

void PagePrint(Page page);

void PageClearContent(Page page);

bool PageIsRightMost(Page page);

int PageMinAndKey(Page page, const Slice& key);

void PagePrintStat(Page page);

uint32_t PageFindOffsetInsert(Page page, const Slice& key);

int PageFindOffset(Page page, const Slice& key);
int PageFindOffsetForScan(Page page, const Slice& key);

int PageGetOffsetByChild(Page page, uint32_t child);

void PageUpdateChild(Page page, const Slice& pivot, uint32_t child);

int PageFindOffsetBol(Page page, const Slice& key);
Slice PageLowkey(Page page);

/**
 * The following functions are helper ones
*/
int _page_compare(Page page, const Slice& key, uint32_t off);
// Binary search and we use user_key for comparison.
// We need to guarantee that key has not appeared in page before.
// E.g., 9, 11, 23, 34, 56. Key = 12. Return off=2
uint32_t _page_bin_search_insert(Page page, const Slice& key);

// Return the last pivot offset that key is greater than.
// E.g., 9, 11, 23, 34, 56. Key = 12. Return off=1
uint32_t _page_bin_search(Page page, const Slice& key);
uint32_t _page_bin_search_with_seq(Page page, const Slice& key);

uint32_t _page_decode_int32_at(Page p, uint32_t pos);

void _page_write_indexEntry_at(uint32_t offset, Page page,
                            const Slice& key, uint32_t pg_id);

void _page_write_leafEntry_at(uint32_t offset, Page page,
                          const Slice& key, const Slice& val);

void _page_decode_indexEntry(Page page, uint32_t offset, char** key,
                             size_t* key_size, uint32_t* child);

void _page_decode_leafEntry(Page page, uint32_t offset, char** key,
                      size_t* key_size, char** val, size_t* val_size);

////////////////////////////
// Blink-tree
uint32_t _page_bin_search_highkey(Page page, const Slice& key);

int PageFindOffsetForScanHighkey(Page page, const Slice& key);

int PageFindOffsetHighkey(Page page, const Slice& key);

Slice PageHighkey(Page page);

bool PageUpdateHighkey(Page page, const Slice& key);

bool PageUpdateKeyAt(Page page, uint32_t offset, const Slice& key);

} // namespace WOT_NAMESPACE

#endif // TREE_PAGE_WOT_NAMESPACE_H_