#include "leveldb/builder.h"
#include "util/state.h"
#include "buffer_manager.h"
#include "arena_wrapper.h"

namespace WOT_NAMESPACE {
  
inline void GetChildPageId(BufferManager* mgr, uint32_t pg_id,
                              std::vector<uint32_t>* child_pgids) {
  Page page = mgr->Pin(pg_id);
  TreePageHeader h = (TreePageHeader) page;
  bool leaf = h->is_leaf_;
  if (leaf) {
    mgr->Unpin(pg_id);
    return;
  }
  for (int i = 0; i < h->item_num_; i++) {
    uint32_t kid = PageReadChildAtOffset(page, i);
    child_pgids->push_back(kid);
  }
  mgr->Unpin(pg_id);
}

inline size_t GetUserUsedSpace(BufferManager* mgr, uint32_t pg_id) {
  Page page = mgr->Pin(pg_id);
  size_t space = ((TreePageHeader) page)->free_end_ - 
                    ((TreePageHeader) page)->free_start_;
  auto r = PAGE_SIZE - space - sizeof(TreePageHeaderData);
  mgr->Unpin(pg_id);
  return r;
}


inline const Slice GetPivotAtOffset(BufferManager* mgr, uint32_t pg_id,
                                    ArenaWrapper& arena, uint32_t offset) {
  Page page = mgr->Pin(pg_id);
  // MutexLock l(&mutex_);
  Slice pivot = PageReadPivotAtOffset(page, offset);
  if (pivot.size() < 0) {
    fprintf(stdout, "%d\n", pg_id);
    PagePrint(page);
  }
  char* p = arena.Allocate(pivot.size());
  std::memcpy(p, pivot.data(), pivot.size());
  auto s = Slice(p, pivot.size());
  mgr->Unpin(pg_id);
  return s;
}


inline const Slice GetValueAtOffset(BufferManager* mgr, uint32_t pg_id,
                                    ArenaWrapper& arena, uint32_t offset) {
  Page page = mgr->Pin(pg_id);
  Slice val = PageReadValueAtOffset(page, offset);

  char* p = arena.Allocate(val.size());
  std::memcpy(p, val.data(), val.size());
  auto s = Slice(p, val.size());
  mgr->Unpin(pg_id);
  return s;
}

inline const Slice GetValueWithKey(BufferManager* mgr, uint32_t pg_id, 
                                  ArenaWrapper& arena, const Slice& key) {
  Page page = mgr->Pin(pg_id);
  assert(((TreePageHeader) page)->is_leaf_ == true);
  int off = PageFindOffset(page, key);
  mgr->Unpin(pg_id);
  if (off == -1) {
    return Slice();
  }
  auto s = GetValueAtOffset(mgr, pg_id, arena, off);
  return s;
}

inline bool FileAcrossPivots(BufferManager* mgr, uint32_t pg_id,
                      std::vector<std::shared_ptr<FileMetaData>>* files,
                      const Comparator* cmp) {
  // First check if the files are in order
  std::string prev_key;
  for (int i = 0; i < files->size(); i++) {
    auto f = files->at(i);
    std::string file_start = f->smallest.user_key().ToString();
    std::string file_limit = f->largest.user_key().ToString();
    if ((!prev_key.empty()) && strcmp(file_start.c_str(), prev_key.c_str()) < 0) {
      // fprintf(stderr, "Files are not sorted\n");
      return true;
    }
    prev_key = file_limit;
  }
  Page page = mgr->Pin(pg_id);
  // timer.Resume();
  uint32_t offset = 0;
  for (int i = 0; i < files->size(); i++) {
    auto f = files->at(i);
    const Slice& file_start = f->smallest.user_key();
    const Slice& file_limit = f->largest.user_key();
    if (offset >= ((TreePageHeader) page)->item_num_) {
      break;
    }
    const Slice& pivot = PageReadPivotAtOffset(page, offset);
    if (offset == 0) {
      // if (icmp->user_comparator()->Compare(file_start, pivot) < 0) {
      if (cmp->Compare(file_start, pivot) < 0) {
        return true;
        // std::cerr << "Error: min key is not properly updated\n";
        // std::abort();
      }
      offset++;
      i = -1;
      continue;
    }
    // if (icmp->user_comparator()->Compare(file_limit, pivot) <= 0) {
    if (cmp->Compare(file_limit, pivot) <= 0) {
      /* file lies completely left to the guard */
    // } else if (icmp->user_comparator()->Compare(file_start, pivot) < 0) {
    } else if (cmp->Compare(file_start, pivot) < 0) {
      /* this guard is within the file range */
      // timer.Pause();
      mgr->Unpin(pg_id);
      return true;
    // } else if (icmp->user_comparator()->Compare(file_start, pivot) >= 0) {
    } else if (cmp->Compare(file_start, pivot) >= 0) {
      /* file lies completely right to the guard */
      offset++;
      i -= 1;
    }
  }
  // timer.Pause();
  mgr->Unpin(pg_id);
  return false;
}


// Page is already pinned
inline Page SplitInternalPages(BufferManager* mgr, ArenaWrapper& arena,
                uint32_t pg_id,
                std::vector<SlicePageMap>* new_pivots,
                std::vector<std::pair<Slice,uint32_t>>* old_pivots,
                std::vector<uint32_t>* new_pages,
                std::vector<Slice>* guards) {
  SlicePageMap temp_pivots;
  Page page = mgr->Lookup(pg_id);
  uint32_t item_num = ((TreePageHeader) page)->item_num_;
  for (int i = 0 ; i < item_num; i++) {
    bool found = false;
    auto pivot = PageReadPivotAtOffset(page, i);
    for (auto const old_pair: *old_pivots) {
      if (pivot.compare(old_pair.first) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      temp_pivots[pivot] = PageReadChildAtOffset(page, i);
    }
  }
  for (auto new_pivot_map : *new_pivots) {
    temp_pivots.merge(new_pivot_map);
  }
  int new_pg_num = new_pages->size() + 1;
  int page_item_size = (temp_pivots.size() + new_pg_num - 1) / new_pg_num;
  auto it = temp_pivots.begin();
  int offset = 0;
  // This extra page will stay in memory until it can be installed
  void* buf = malloc(PAGE_SIZE);
  memset(buf, 0, PAGE_SIZE);
  Page extra_one = (Page) buf;
  PageInit(extra_one, pg_id);
  while (it != temp_pivots.end()) {
    PageInsertIndexEntry(extra_one, it->first, it->second);
    if (offset == 0) {
      guards->push_back(PageReadPivotAtOffset(extra_one, 0));
    }
    offset++;
    it++;
    if (offset >= page_item_size) break;
  }
  offset = 0;
  int cur_page_idx = 0; 
  uint32_t cur_pg_id = new_pages->at(cur_page_idx);
  Page cur_page = mgr->Pin(cur_pg_id);
  while (it != temp_pivots.end()) {
    PageInsertIndexEntry(cur_page, it->first, it->second);
    if (offset == 0) {
      // guards->push_back(it->first);
      guards->push_back(GetPivotAtOffset(mgr, cur_pg_id, arena, 0));
    }
    offset++;
    if (offset >= page_item_size && cur_page_idx + 1 < new_pages->size()) {
      cur_page_idx++;
      mgr->Unpin(cur_pg_id);
      cur_pg_id = new_pages->at(cur_page_idx);
      cur_page = mgr->Pin(cur_pg_id);
      offset = 0;
    }
    it++;
  }
  mgr->Unpin(cur_pg_id);
  temp_pivots.clear();
  // if (pg_id != root_page_id_) {
  //   Delete(pg_id);
  // }

  // timer.Pause();
  return extra_one;
}

inline Status DeleteEntry(BufferManager* mgr, uint32_t pg_id, const Slice& key) {
  Page page = mgr->Lookup(pg_id);
  if (!PageRemoveEntry(page, key)) {
    return Status::NotFound("Entry not found");
  }
  return Status::OK();
}


inline bool IsLeafPage(BufferManager* mgr, uint32_t pg_id) {
  Page page = mgr->Pin(pg_id);
  bool r = ((TreePageHeader) page)->is_leaf_;
  mgr->Unpin(pg_id);
  return r;
}


inline void AddNewPivots(BufferManager* mgr, uint32_t pg_id, SlicePageMap* new_pivots) {
  Page page = mgr->Lookup(pg_id);
  for (auto it = new_pivots->begin(); it != new_pivots->end(); it++) {
    PageInsertIndexEntry(page, it->first, it->second);
  }
}


inline void UpdateChildbyPivot(BufferManager* mgr, uint32_t pg_id,
                        const Slice& pivot, uint32_t child) {
  Page page = mgr->Lookup(pg_id);
  PageUpdateChild(page, pivot, child);
}


} // namespace WOT_NAMESPACE
