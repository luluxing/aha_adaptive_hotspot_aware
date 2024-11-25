#include <atomic>
#include <chrono>
#include <cmath>
#include <math.h>
#include <functional> 
#include <fstream>
#include <queue>
#include <random>
#include <stack>
#include <thread>

#include "wot_index.h"
#include "leveldb/builder.h"
#include "leveldb/util/mutexlock.h"
#include "leveldb/table/merger.h"
#include "leveldb/write_batch_internal.h"
#include "wot_buf_mgr/buffer_pool_helper.h"
#include "util/utils.h"

namespace WOT_NAMESPACE {

size_t CalculateNewSize(std::vector<SlicePageMap>* new_pivots,
                        std::vector<std::pair<Slice,uint32_t>>* old_pivots) {
  size_t result = 0;
  for (int i = 0; i < old_pivots->size(); i++) {
    for (auto const& m : (*new_pivots)[i]) {
      // Store key_size, key, child page, item_id
      result += m.first.size() + 3 * ITEMID_SIZE;
    }
    // TODO: need to remove the actual char* array
    // of the old key
    result -= ITEMID_SIZE;  
  }
  return result;
}

size_t CalculateNumber(std::vector<SlicePageMap>* new_pivots,
                        std::vector<std::pair<Slice,uint32_t>>* old_pivots) {
  int result = 0;
  for (int i = 0; i < new_pivots->size(); i++) {
    for (auto const& m : (*new_pivots)[i]) {
      result++;
    }
    if (old_pivots->size() > 0) result--;
  }
  return result;
}

int BplusTree::EqualSplitInternalPage(Page p, std::vector<SlicePageMap>* new_pivots,
                        std::vector<std::pair<Slice,uint32_t>>* old_pivots) {
  if (old_pivots->size() == 0) {
    return 1;
  }
  int split_num;
  int new_entry_num = CalculateNumber(new_pivots, old_pivots);
  int cur_entry_num = ((TreePageHeader) p)->item_num_;
  size_t raw = PAGE_SIZE - sizeof(TreePageHeaderData);
  size_t entry_size = old_pivots->at(0).first.size() + 3 * ITEMID_SIZE;
  int capacity = raw / entry_size;
  int total_entry_num = new_entry_num + cur_entry_num;
  split_num = (total_entry_num + capacity - 1) / capacity;
  return split_num;
}

void Node::NodePrint(int level) {
  if (level == 1) {
    std::cout << " Node#" << pg_id_ << "::";
    Page page = tree_->buffer_manager_->Pin(pg_id_);
    if (((TreePageHeader) page)->is_leaf_) {
      std::string leaf_cato = "large-leaf";
      if (node_lsmt_->GetLSMTStatus() == LSMTStatus::kSmallLeaf) {
        leaf_cato = "small-leaf";
      }
      std::cout << leaf_cato << ":";
    }
    PagePrint(page);
    tree_->buffer_manager_->Unpin(pg_id_);
    if (node_lsmt_ != nullptr) {
      node_lsmt_->Print();
      std::cout << "\n";
    }
  } else {
    Page page = tree_->buffer_manager_->Pin(pg_id_);
    uint32_t item_num = ((TreePageHeader) page)->item_num_;
    for (int i = 0; i < item_num; i++) {
      ReadTbb a;
      if (tree_->node_table_.find(a, PageReadChildAtOffset(page, i))) {
        a->second->NodePrint(level - 1);
      }
      a.release();
    }
    tree_->buffer_manager_->Unpin(pg_id_);
  }
}

void Node::NodePrintStat(int level) {
  if (level == 1) {
    std::cout << " Node#" << pg_id_ << "::";
    Page page = tree_->buffer_manager_->Pin(pg_id_);
    // if (!((TreePageHeader) page)->is_leaf_)
      PagePrintStat(page);
    tree_->buffer_manager_->Unpin(pg_id_);
    if (node_lsmt_ != nullptr) {
      node_lsmt_->PrintStat();
    }
  } else {
    Page page = tree_->buffer_manager_->Pin(pg_id_);
    uint32_t item_num = ((TreePageHeader) page)->item_num_;
    for (int i = 0; i < item_num; i++) {
      ReadTbb a;
      if (tree_->node_table_.find(a, PageReadChildAtOffset(page, i))) {
        a->second->NodePrintStat(level - 1);
      }
      a.release();
    }
    tree_->buffer_manager_->Unpin(pg_id_);
  }
}

// Node is already read-locked
SlicePageMap Node::MaybeSplitOrFlush() {
  std::vector<SlicePageMap> new_pivots;
  std::vector<std::pair<Slice, uint32_t>> old_pivots;
  Page page = tree_->buffer_manager_->Pin(pg_id_);
  bool leaf_page = ((TreePageHeader) page)->is_leaf_;
  // ->buffer_manager_->Release(pg_id_);
  if (leaf_page) {
    node_overflow_ = true;
  } else {
    // Method 1: the bottom level was compacted and ready to be
    //           merged with all children nodes.
    //           Need to modify LSMT::AddFile(s)
    CompactAndFlushToChildren(&new_pivots, &old_pivots);
    // Method 2: Compact the parent buffer with all its children
    //           Need to modify LSMT::AddFile(s)
    // CompactWithChildren(&new_pivots, &old_pivots);
    // Method 3: Pick one child (round-robin) and compact with
    //           the parent buffer
    //           Need to modify LSMT::AddFile(s)
    // CompactWithOneChild(&new_pivots, &old_pivots);
    // Method 4: the bottom level was compacted and only pick
    //           some children nodes to be merged
    //           Need to modify LSMT::AddFile(s)
    // CompactAndFlushToSomeChildren(&new_pivots, &old_pivots);
    UpdatePivots(&new_pivots, &old_pivots);
  }
  SlicePageMap result;
  if (node_overflow_) {
    // Until now, no modifications have been done. During
    // split, we will split the node and apply updates 
    result = SplitNodeAndLSMT(&new_pivots, &old_pivots);
  }
  new_pivots.clear();
  old_pivots.clear();
  if (leaf_page) {
    tree_->buffer_manager_->UnpinAndRelease(pg_id_);
  } else {
    tree_->buffer_manager_->Unpin(pg_id_);
  }
  return result;
}

// Node is already write-locked
OpState Node::BufferCompactTopLevel(Page page) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  if (!((TreePageHeader) page)->is_leaf_ && tree_->btree_state_->has_buffer_page) {
    uint32_t new_level = tree_->node_lsmt_level_limit_ > 1 ? tree_->node_lsmt_level_limit_ - 1 : 1;
    lsmt4compact_->SetLevelLimit(new_level);
  }
  OpState s = lsmt4compact_->CompactTopLevel(page);
  return s;
}

bool Node::BufferTopLevelOverflows() {
  if (lsmt4compact_ != nullptr) {
    return lsmt4compact_->TopLevelOverflows();
  } else if (node_lsmt_ != nullptr) {
    return node_lsmt_->TopLevelOverflows();
  }
  return false;
}

int Node::GetNodeLSMTFileSizeInLevel(int level) {
  if (lsmt4compact_ != nullptr) {
    return lsmt4compact_->GetLSMTFileSizeInLevel(level);
  } else if (node_lsmt_ != nullptr) {
    return node_lsmt_->GetLSMTFileSizeInLevel(level);
  }
  return 0;
}

OpState Node::BufferAppendFiles(std::vector<std::shared_ptr<FileMetaData>>* files) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  OpState s = lsmt4compact_->AppendFiles(0, files);
  if (tmp_min_.empty() || files->at(0)->smallest.user_key().ToString() < tmp_min_) {
    tmp_min_ = files->at(0)->smallest.user_key().ToString();
  }
  return s;
}

OpState Node::BufferAddFiles(std::vector<std::shared_ptr<FileMetaData>>* files,
                             Page p) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  OpState s = lsmt4compact_->AddFiles(0, files, p);
  return s;
}

// LOCKED
void Node::GetObsoleteFilesAndInstallNewBuffer(std::set<uint64_t>& files) {
  if (lsmt4compact_ == nullptr) return;
  delete node_lsmt_;
  node_lsmt_ = lsmt4compact_;
  node_lsmt_->GetObsoleteFiles(files);
  lsmt4compact_ = nullptr;
  InstallNewPage();
  if (!tmp_min_.empty()) {
    Page self_page = tree_->buffer_manager_->Pin(pg_id_);
    MayUpdateMinPivot(self_page, Slice(tmp_min_));
    tmp_min_.clear();
    tree_->buffer_manager_->Unpin(pg_id_);
  }
}

// LOCKED
void Node::InstallNewBuffer() {
  if (lsmt4compact_ == nullptr) return;
  delete node_lsmt_;
  node_lsmt_ = lsmt4compact_;
  node_lsmt_->RemoveObsoleteFiles();
  lsmt4compact_ = nullptr;
  InstallNewPage();
  if (!tmp_min_.empty()) {
    Page self_page = tree_->buffer_manager_->Pin(pg_id_);
    MayUpdateMinPivot(self_page, Slice(tmp_min_));
    tmp_min_.clear();
    tree_->buffer_manager_->Unpin(pg_id_);
  }
}

void Node::InstallNewPage() {
  if (extra_page_ != nullptr) {
    assert(tmp_min_.empty());
    Page self_page = tree_->buffer_manager_->Pin(pg_id_);
    ((TreePageHeader) extra_page_)->page_id_ = pg_id_;
    ((TreePageHeader) extra_page_)->is_leaf_ = ((TreePageHeader) self_page)->is_leaf_;
    ((TreePageHeader) extra_page_)->is_dirty_ = true;
    ((TreePageHeader) extra_page_)->left_ = ((TreePageHeader) self_page)->left_;
    ((TreePageHeader) extra_page_)->right_ = ((TreePageHeader) self_page)->right_;
    memcpy(self_page, extra_page_, PAGE_SIZE);
    free(extra_page_);
    extra_page_ = nullptr;
    tree_->buffer_manager_->Unpin(pg_id_);
  }
}

// Page is pinned
void Node::MayUpdateMinPivot(Page p, const Slice& k) {
  if (p != nullptr && ((TreePageHeader) p)->item_num_ > 0) {
    const Slice& minpivot = PageReadPivotAtOffset(p, 0);
    if (icmp_->user_comparator()->Compare(k, minpivot) < 0) {
      PageUpdateMinPivot(p, k);
    }
  }
}

// LOCKED
void Node::SetBufferStatus(LSMTStatus status) {
  if (lsmt4compact_ != nullptr) {
    lsmt4compact_->SetLSMTStatus(status);
  } else if (node_lsmt_ != nullptr) {
    node_lsmt_->SetLSMTStatus(status);
  }
}

// LOCKED
void Node::SetBufferLevelLimit(int level) {
if (lsmt4compact_ != nullptr) {
    lsmt4compact_->SetLevelLimit(level);
  } else if (node_lsmt_ != nullptr) {
    node_lsmt_->SetLevelLimit(level);
  }
}

// LOCKED
void Node::BufferClearBottomLevelFiles() {
  if (lsmt4compact_ != nullptr) {
    lsmt4compact_->ClearBottomLevelFiles();
  } else if (node_lsmt_ != nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
    lsmt4compact_->ClearBottomLevelFiles();
  }
}

Status Node::BufferCompactTree(std::vector<Slice>* guards,
                              std::vector<std::shared_ptr<FileMetaData>>* outfs) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  Status s = lsmt4compact_->CompactTree(guards, outfs);
  return s;
}

Status Node::BufferCompactTree(int* sn, std::vector<std::shared_ptr<FileMetaData>>* outfs,
                               Iterator* pi) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  Status s = lsmt4compact_->CompactTree(sn, outfs, pi);
  return s;
}

// Removes all file metadata in lsmt but not removing files
void Node::BufferClear() {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  lsmt4compact_->Clear();
}

void Node::BufferClearAll() {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  lsmt4compact_->ClearAll();
}

void Node::BufferFinalizeMergeLeaf(int level_of_files,
                                    std::vector<std::shared_ptr<FileMetaData>>* files) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  // These files should be deleted from the lsmt
  lsmt4compact_->FinalizeRetrievalAndDelete(level_of_files, files);
}

void Node::BufferCompactFilesInRange(int level,
                                  std::vector<std::shared_ptr<FileMetaData>>* files,
                                  const Page page) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  lsmt4compact_->CompactFilesInRange(level, files, page);
}

void Node::BufferCompactBottomLevel(Page page) {
  if (lsmt4compact_ == nullptr) {
    lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
  }
  lsmt4compact_->CompactBottomLevel(page);
}

const std::vector<std::shared_ptr<FileMetaData>>* 
      Node::BufferGetAndCompactBottomLevel(Page page) {
  BufferCompactBottomLevel(page);
  return lsmt4compact_->GetBottomLevelFiles(nullptr);
}

// Merge the leaf page with the existing buffer and write into pages
SlicePageMap Node::SplitSmallLeaf() {
  SlicePageMap result;
  std::vector<Iterator*> iter_vec;
  uint64_t entry_num = 0;
  if (node_lsmt_->GetBottomLevel() >= 0) {
    ReadOptions options;
    options.fill_cache = false;
    iter_vec.emplace_back(node_lsmt_->NewMergedIterator(options));
    entry_num += node_lsmt_->TotalEntryNum();
  }
  Page this_page = tree_->buffer_manager_->Lookup(pg_id_);
  assert(((TreePageHeader) this_page)->item_num_ == 0);
  // auto pi = tree_->NewPageIterator();
  // if (((TreePageHeader) this_page)->item_num_ > 0) {
  //   ((PageIterator*) pi)->AddPage(pg_id_);
  //   // iter_vec.push_back(tree_->NewPageIterator(pg_id_));
  //   entry_num += ((TreePageHeader) this_page)->item_num_;
  //   iter_vec.push_back(pi);
  // }
  Iterator* leaf_iter = NewMergingIterator(
                &tree_->internal_comparator_, &iter_vec[0], iter_vec.size());
  result = WriteToPage(tree_, icmp_, leaf_iter, entry_num);
  BufferClearAll();
  if (result.size() == 1) {
#ifdef BREAKDOWN
    uint64_t small_start = _rdtsc();
#endif
    tree_->lock_manager_->EscalateLock(pg_id_);
    Page new_page = tree_->buffer_manager_->Pin(result.begin()->second);
    memcpy(this_page, new_page, PAGE_SIZE);
    // tree_->buffer_manager_->UnpinAndRelease(result.begin()->second);
    tree_->buffer_manager_->Delete(((TreePageHeader) new_page)->page_id_);
    ((TreePageHeader) this_page)->page_id_ = pg_id_;
    ((TreePageHeader) this_page)->is_leaf_ = true;
    ((TreePageHeader) this_page)->is_dirty_ = true;
    std::set<uint64_t> dead_files;
    GetObsoleteFilesAndInstallNewBuffer(dead_files);
    // InstallNewBuffer();
    if (node_lsmt_ != nullptr) delete node_lsmt_;
    if (lsmt4compact_ != nullptr) delete lsmt4compact_;
    node_lsmt_ = nullptr;
    lsmt4compact_ = nullptr;
    result.clear();
    if (!tree_->node_table_.erase(pg_id_)) {
      fprintf(stdout, "Error: cannot erase in node_table: %d!", pg_id_);
    }
    tree_->lock_manager_->AlleviateLock(pg_id_);
#ifdef BREAKDOWN
    uint64_t small_end = _rdtsc();
    tree_->writer_stat_.split_small_leaf_ltime.fetch_add(small_end - small_start);
#endif
    LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead_files);
  } else {
    tree_->nodes_in_progress.push_back(pg_id_);
  }
  // delete leaf_iter;
  return result;
}

// Node is already read-locked
SlicePageMap Node::SplitNodeAndLSMT(
                          std::vector<SlicePageMap>* new_pivots,
                          std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  SlicePageMap result;
  int split_num = 2;
  // Page is already pinned
  Page this_page = tree_->buffer_manager_->Lookup(pg_id_);
  bool leaf_page = ((TreePageHeader) this_page)->is_leaf_;
  if (!leaf_page) {
    split_num = tree_->EqualSplitInternalPage(this_page, new_pivots, old_pivots);
    split_num = split_num < 2 ? 2 : split_num;
  } else {
    // Leaf node is split into more than 2
    // when root is leaf, it will never split
    assert(node_lsmt_ != nullptr); 
    uint64_t total_size = node_lsmt_->TotalFileSizeLSMT();
    // int alloc_size = 1024 * 1024;
    int alloc_size = node_lsmt_->output_file_size;
    split_num = static_cast<int>(static_cast<double>(total_size) / alloc_size + 1);
    split_num = split_num < 2 ? 2 : split_num;
  }

  assert(split_num >= 2);
  std::vector<std::shared_ptr<FileMetaData>> out_file_names;
  Status s;
  if (leaf_page) {
    if (((TreePageHeader) this_page)->item_num_ > 0) {
      void* buf = malloc(PAGE_SIZE);
      memset(buf, 0, PAGE_SIZE);
      extra_page_ = (Page) buf;
      memcpy(extra_page_, this_page, PAGE_SIZE);
      ((TreePageHeader) extra_page_)->item_num_ = 0;
      ((TreePageHeader) extra_page_)->is_dirty_ = true;
      auto pi = tree_->NewPageIterator();
      ((PageIterator*) pi)->AddPage(pg_id_);
      s = BufferCompactTree(&split_num, &out_file_names, pi);
      BufferClear();
    } else {
      s = BufferCompactTree(&split_num, &out_file_names);
      BufferClear();
    }
  }

  std::vector<uint32_t> new_pages;
  for (int i = 0; i < split_num - 1; i++) {
    uint32_t p_id = tree_->buffer_manager_->Allocate();
    Page p = tree_->buffer_manager_->Pin(p_id);
    ((TreePageHeader) p)->is_leaf_ = leaf_page;
    ((TreePageHeader) p)->is_dirty_ = true;
    new_pages.push_back(((TreePageHeader) p)->page_id_);
    tree_->buffer_manager_->Unpin(p_id);
  }
  std::vector<Slice> guards;
  // TODO: there is a potential optimization: we can choose from flush the 
  // biggest chunk, or flush all chunks
  if (!leaf_page) {
    assert(extra_page_ == nullptr);
    extra_page_ = tree_->TreeSplitInternalPage(pg_id_, new_pivots,
                                  old_pivots, &new_pages, &guards,
                                  &out_file_names);
  }
  
  if (leaf_page && out_file_names.size() == 1) {
#ifdef BREAKDOWN
    uint64_t split_start = _rdtsc();
#endif
    // LOCK 
    tree_->lock_manager_->EscalateLock(pg_id_);
    BufferAddFiles(&out_file_names, this_page); // we do not check overflow or not
    std::set<uint64_t> dead_files;
    GetObsoleteFilesAndInstallNewBuffer(dead_files);
    // InstallNewBuffer();
    tree_->lock_manager_->AlleviateLock(pg_id_);
    // UNLOCK
    LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead_files);
    for (auto const& page : new_pages) {
      tree_->buffer_manager_->Delete(page);
    }
    out_file_names.clear();
#ifdef BREAKDOWN
    uint64_t split_end = _rdtsc();
    tree_->writer_stat_.split_ltime.fetch_add(split_end - split_start);
#endif
    return result;
  }  

  for (auto const & new_page : new_pages) {
    LSMTStatus lsmt_status = leaf_page ? LSMTStatus::kLeaf : LSMTStatus::kBuffer;
    auto lsmt_level_lim = tree_->node_lsmt_level_limit_;
    if (node_lsmt_ != nullptr && node_lsmt_->GetLSMTStatus() == LSMTStatus::kSmallLeaf) {
      lsmt_status = LSMTStatus::kSmallLeaf;
      lsmt_level_lim = 1;
    }
    Node* node = new Node(tree_, leaf_page, lsmt_level_lim,
                          tree_->env_, tree_->root_lsmt_path_,
                          tree_->flush_id_, &tree_->leveldb_options_,
                          tree_->internal_comparator_, tree_->table_cache_,
                          lsmt_status, tree_->leaf_limit_, true);
    // if (node_lsmt_ != nullptr && node_lsmt_->GetLSMTStatus() == LSMTStatus::kSmallLeaf) {
    //   node->node_lsmt_->output_file_size = node_lsmt_->output_file_size / tree_->scale_in_;
    // }
    WriteTbb a;
    if (tree_->node_table_.find(a, new_page)) {
      delete a->second;
    }
    tree_->node_table_.insert(a, new_page);
    a->second = node;
    a.release();
    // tree_->node_table_[new_page] = node;
    node->pg_id_ = new_page;
  }

  if (!leaf_page) {
    if (out_file_names.size() > 0) {
      DistributeFilesToNodes(&out_file_names, &new_pages, &guards,
                            nullptr, nullptr);
    }
    assert(guards.size() == new_pages.size() + 1);
    for (int i = 0 ; i < guards.size(); i++) {
      Slice guard = Slice(NewPivot(tree_->arena_wrapper_, guards[i]), guards[i].size());
      if (i == 0) {
        result[guard] = pg_id_;
        // assert(guard.compare(PageReadPivotAtOffset(this_page, 0)) == 0);
      } else {
        result[guard] = new_pages[i - 1];
      }
    }
  } else if (out_file_names.size() > 0 && leaf_page) {
    assert(out_file_names.size() == split_num);
    for (int fi = 0; fi < out_file_names.size(); fi++) {
      Slice tmp = out_file_names[fi]->smallest.user_key();
      Slice fr = Slice(NewPivot(tree_->arena_wrapper_, tmp), tmp.size());
      std::vector<std::shared_ptr<FileMetaData>> fvec(out_file_names.begin() + fi,
                                        out_file_names.begin() + fi + 1);
      if (fi == 0) {
        BufferAddFiles(&fvec, nullptr);
        result[fr] = pg_id_;
      } else {
        Node *nn;
        ReadTbb a;
        tree_->node_table_.find(a, new_pages[fi-1]);
        nn = a->second;
        a.release();
        Page pg = tree_->buffer_manager_->Pin(new_pages[fi-1]);
        // No need to lock this new node
        // nn->node_lsmt_->AddFiles(0, &fvec, pg);
        nn->BufferAddFiles(&fvec, pg);
        nn->InstallNewBuffer();
        tree_->buffer_manager_->UnpinAndRelease(new_pages[fi-1]);
        result[fr] = new_pages[fi-1];
      }  
    }
  }
  out_file_names.clear();
  new_pages.clear();
  guards.clear();
  if (node_lsmt_ != nullptr) {
    tree_->nodes_in_progress.push_back(pg_id_);
  }
  return result;
}

// Page is already pinned and read-locked
void Node::UpdatePivots(std::vector<SlicePageMap>* new_pivots,
                        std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  Page page = tree_->buffer_manager_->Lookup(pg_id_);

  int split_num = tree_->EqualSplitInternalPage(page, new_pivots, old_pivots);
  if (split_num > 1) {
    node_overflow_ = true;
  } else {
    // LOCK
#ifdef BREAKDOWN
    uint64_t ustart = _rdtsc();
#endif
    tree_->lock_manager_->EscalateLock(pg_id_);
    node_overflow_ = false;
    tree_->UpdateOrRewritePivots(pg_id_, new_pivots, old_pivots);

    // We install new buffer in the nodes whose buffer have been modified.
    // These nodes are not locked but their ancestors are write-locked,
    // which stops other threads from reading the entire subtree.
    // It is safe to install new buffer without locking the nodes.
#ifdef BREAKDOWN
    tree_->writer_stat_.installed_buffer.fetch_add(tree_->nodes_in_progress.size());
#endif
    std::set<uint64_t> dead;
    for (int j = 0; j < tree_->nodes_in_progress.size(); j++) {
      ReadTbb a;
      tree_->node_table_.find(a, tree_->nodes_in_progress[j]);
      a->second->GetObsoleteFilesAndInstallNewBuffer(dead);
      // a->second->InstallNewBuffer();
      a->second->node_overflow_ = false;
      a->second->node_above_leaf = false;
    }
    tree_->nodes_in_progress.clear();
    tree_->lock_manager_->AlleviateLock(pg_id_);
    // UNLOCK
#ifdef BREAKDOWN
    uint64_t uend = _rdtsc();
    tree_->writer_stat_.update_pivot_ltime.fetch_add(uend - ustart);
#endif
    LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead);
  }
}

void Node::AppendFilesToChild(uint32_t child_pg_id, std::vector<Node*>* tmp_nodes,
                std::vector<std::shared_ptr<FileMetaData>>* remains, 
                std::vector<std::shared_ptr<FileMetaData>>* flushed,
                const Slice& pivot, SlicePageMap* overflow_children,
                SlicePageMap* children_need_compaction) {
  tree_->lock_manager_->ReadLock(child_pg_id);
  ReadTbb a;
  Node* node = nullptr;
  if (tree_->node_table_.find(a, child_pg_id)) {
    node = a->second;
  }
  a.release();
  tmp_nodes->push_back(node);
  if (node != nullptr && node->GetNodeLSMT() != nullptr && remains->size() > 0) {
    // Page child_page = tree_->buffer_manager_->Pin(child_pg_id);
    assert(node->lsmt4compact_ == nullptr);
    // OpState s = node->GetNodeLSMT()->AppendFiles(0, remains, child_page);
    OpState s = node->BufferAppendFiles(remains);
    flushed->insert(flushed->end(), remains->begin(), remains->end());
    if (s == OpState::kOverflow) {
      (*children_need_compaction)[pivot] = child_pg_id;
    }
    // tree_->buffer_manager_->UnpinAndRelease(child_pg_id);
  } else if (node == nullptr || node->GetNodeLSMT() == nullptr) {
    // // Files need to be added to small leaf page
    // (*overflow_children)[pivot] = child_pg_id;
    // If this leaf page is no longer inside a hotspot, we need to transform
    // it to a leaf node by adding a nodeLSM and writing page entries to file
    if (pivot.compare(tree_->GetLowHotKey()) > 0 && pivot.compare(tree_->GetHighHotKey()) < 0) {
      // This leaf page is still within hotspot range
      (*overflow_children)[pivot] = child_pg_id;
    } else {
      LSMTStatus lsmt_status = LSMTStatus::kLeaf;
      auto lsmt_level_lim = tree_->node_lsmt_level_limit_;
      node = new Node(tree_, true, lsmt_level_lim,
                            tree_->env_, tree_->root_lsmt_path_,
                            tree_->flush_id_, &tree_->leveldb_options_,
                            tree_->internal_comparator_, tree_->table_cache_,
                            lsmt_status, tree_->leaf_limit_, true);
      WriteTbb a;
      if (tree_->node_table_.find(a, child_pg_id)) {
        delete a->second;
      }
      tree_->node_table_.insert(a, child_pg_id);
      a->second = node;
      a.release();
      node->pg_id_ = child_pg_id;
      if (remains->size() > 0) {
        OpState s = node->GetNodeLSMT()->AppendFiles(0, remains);
        flushed->insert(flushed->end(), remains->begin(), remains->end());
        if (s == OpState::kOverflow) {
          (*children_need_compaction)[pivot] = child_pg_id;
        }
      }
    }
  }
  tree_->lock_manager_->ReadUnlock(child_pg_id);
}

// Node is already write-locked
void Node::FinalizeSubtree(Page this_page, int level_of_files,
            std::vector<Node*>* tmp_nodes,
            std::vector<std::shared_ptr<FileMetaData>>* flushed,
            int locked_node_id) {
  uint32_t item_num = ((TreePageHeader) this_page)->item_num_;
  LevelDBLSMT* lsmt = nullptr;
  if (locked_node_id == -1) {
    // Remove lsmt's bottom files
    lsmt = tree_->leveldb_lsmt_;
  } else {
    // remove buffer lsmt's bottom files
    lsmt = node_lsmt_;
  }
  for (int j = 0; j < item_num; j++) {
    if (j >= tmp_nodes->size()) break;
    uint32_t child_pg_id = PageReadChildAtOffset(this_page, j);
    // Page child_page = tree_->buffer_manager_->Pin(child_pg_id);
    if (tmp_nodes->at(j) != nullptr) {
      // tree_->lock_manager_->WriteLock(child_pg_id);
      tmp_nodes->at(j)->InstallNewBuffer();
      if (lsmt->HasHotspot(tree_->GetLowHotKey(), tree_->GetHighHotKey())) {
        tmp_nodes->at(j)->GetNodeLSMT()->SetHotspot(
          tree_->GetLowHotKey(), tree_->GetHighHotKey(), true);
      }
      // tree_->lock_manager_->WriteUnlock(child_pg_id);
    }
  }
  lsmt->FinalizeRetrieval(level_of_files, flushed);
}

void Node::CompactChildren(SlicePageMap* children_need_compaction,
                           SlicePageMap* overflow_children) {
  for (auto it = children_need_compaction->begin(); it != children_need_compaction->end(); it++) {
    // tree_->lock_manager_->WriteLock(it->second);
    tree_->lock_manager_->ReadLock(it->second);
    ReadTbb a;
    Node* node = nullptr;
    if (tree_->node_table_.find(a, it->second)) {
      node = a->second;
    }
    a.release();
    // assert(node != nullptr && node->lsmt4compact_ == nullptr);
    assert(node != nullptr);
    Page p = tree_->buffer_manager_->Pin(it->second);
    OpState s = node->BufferCompactTopLevel(p);
    // tree_->lock_manager_->EscalateLock(it->second);
    // node->InstallNewBuffer();
    // node->GetNodeLSMT()->SetHotspot(true);
    tree_->buffer_manager_->UnpinAndRelease(it->second);
    // if (tree_->NodeTopLevelOverflows(node)) {
    if (s == OpState::kOverflow) {
      (*overflow_children)[it->first] = it->second;
    }
    // tree_->lock_manager_->AlleviateLock(it->second);
    tree_->lock_manager_->ReadUnlock(it->second);
  }
}

SlicePageMap Node::FlushFilesToChildrenNoSplit(int level_of_files,
    std::vector<std::shared_ptr<FileMetaData>>* files) {
  int i = 0, file_idx = 0;
  // We MUST pin the buffer otherwise guards will become obsolete
  // Page is already pinned
  Page this_page = tree_->buffer_manager_->Lookup(pg_id_);
  uint32_t item_num = ((TreePageHeader) this_page)->item_num_;
  SlicePageMap overflow_children, children_need_compaction;
  std::vector<Node*> tmp_nodes;
  std::vector<std::shared_ptr<FileMetaData>> flushed;
  while (i < item_num && file_idx < files->size()) {
    const Slice& pivot = PageReadPivotAtOffset(this_page, i);
    std::vector<std::shared_ptr<FileMetaData>> remains;
    if (i + 1 >= item_num /*guards->size()*/) {
      // This is the last pivot. Flush all remaining files
      assert(icmp_->user_comparator()->Compare(pivot,
                          files->at(file_idx)->smallest.user_key()) <= 0);      
      remains = {files->begin() + file_idx, files->end()};
      file_idx = files->size();
    } else {
      const Slice& next_pivot = PageReadPivotAtOffset(this_page, i+1);
      int fi = file_idx;
      while (fi < files->size()) {
        const Slice file_start = files->at(fi)->smallest.user_key();
        if (icmp_->user_comparator()->Compare(next_pivot, file_start) <= 0) {
          break;
        }
        fi++;
      }
      remains = {files->begin() + file_idx, files->begin() + fi};
      file_idx = fi;
    }
    uint32_t child_pg_id = PageReadChildAtOffset(this_page, i);
    AppendFilesToChild(child_pg_id, &tmp_nodes, &remains, &flushed, pivot, 
                        &overflow_children, &children_need_compaction);
    i++;
  }
  CompactChildren(&children_need_compaction, &overflow_children);
  // Iterate over children again (index locked) and make children's buffer visible
  // and remove parent's buffer
  int lock_node_id = pg_id_ == tree_->root_pg_id_ ?
                               tree_->GetLockedRoot() : pg_id_;
#ifdef BREAKDOWN
  uint64_t start = _rdtsc();
#endif
  tree_->lock_manager_->EscalateLock(lock_node_id);
  FinalizeSubtree(this_page, level_of_files, &tmp_nodes, &flushed, lock_node_id);
  flushed.clear();
  tree_->lock_manager_->AlleviateLock(lock_node_id);
#ifdef BREAKDOWN
  uint64_t end = _rdtsc();
  if (lock_node_id == -1)
    tree_->writer_stat_.lsmt_ltime.fetch_add(end - start);
  else
    tree_->writer_stat_.flush_ltime.fetch_add(end - start);
#endif
  if (lock_node_id == -1) {
    tree_->lock_manager_->ReadUnlock(-1);
  }
  // CompactChildren(&children_need_compaction, &overflow_children);
  return overflow_children;
}

// There are 3 situations where this function is called:
// 1. CompactAndFlushToChildren when node buffer overflows. Page is pinned
// 2. Root calls it to flush files from lsmt to tree nodes. Root is always pinned
// 3. During WotAdapt, files are flushed to lower nodes. Page is pinned
// Node is already read-locked
void Node::FlushFilesToChildren(int level_of_files,
                                std::vector<std::shared_ptr<FileMetaData>>* files,
                                std::vector<SlicePageMap>* new_pivots,
                                std::vector<std::pair<Slice, uint32_t>>* old_pivots,
                                bool one_level_only) {
  if (tree_->AllChildrenLeaf(pg_id_)) {
    return tree_->MergeWithAllChildrenLeaf(pg_id_, level_of_files, files, new_pivots, old_pivots);
  }
  SlicePageMap overflow_children = FlushFilesToChildrenNoSplit(level_of_files, files);
  bool small_leaf_op = false;
  // files vector has been changed. Those that are not flushed are still in the vector.
  for (auto it = overflow_children.begin(); it != overflow_children.end(); it++) {
    tree_->lock_manager_->ReadLock(it->second);
    ReadTbb a;
    Node* node = nullptr;
    if (tree_->node_table_.find(a, it->second)) {
      node = a->second;
    }
    a.release();
    SlicePageMap new_kids;
    if (node != nullptr) {
      if (!one_level_only)
        new_kids = node->MaybeSplitOrFlush();
    } else {
      assert(pg_id_ != tree_->root_pg_id_ || tree_->leveldb_lsmt_ == nullptr);
      new_kids = FlushAndMergeSmallLeaf(it->second, level_of_files, files);
      if (new_kids.size() > 1) {
        small_leaf_op = true;
      }
    }
    if (!new_kids.empty() && old_pivots != nullptr && new_pivots != nullptr) {
      old_pivots->emplace_back(std::make_pair(it->first, it->second));
      new_pivots->emplace_back(new_kids);
      // assert(it->first.compare(new_kids.begin()->first) == 0);
    }
    tree_->lock_manager_->ReadUnlock(it->second);
  }
  if (small_leaf_op) {
    tree_->nodes_in_progress.push_back(pg_id_);
  }
}

// This child node is already read-locked
SlicePageMap Node::FlushAndMergeSmallLeaf(uint32_t child_id, int level_of_files,
                                  std::vector<std::shared_ptr<FileMetaData>>* files) {
  SlicePageMap result;
  // First find which files belong to the child_id
  Page this_page = tree_->buffer_manager_->Lookup(pg_id_);
  std::vector<std::shared_ptr<FileMetaData>> flushed;
  int offset = PageGetOffsetByChild(this_page, child_id);
  assert(offset > -1);
  Slice pivot = PageReadPivotAtOffset(this_page, offset);
  bool last_one = offset == ((TreePageHeader) this_page)->item_num_ - 1;
  Slice next_pivot = last_one ? Slice() : PageReadPivotAtOffset(this_page, offset + 1);
  for (auto const& f : *files) {
    if (f->smallest.user_key().compare(pivot) >= 0) {
      if (last_one) {
        flushed.push_back(f);
      } else if (f->largest.user_key().compare(next_pivot) < 0) {
        flushed.push_back(f);
      }
    }
  }

  if (flushed.size() == 0) {
    return result;
  }
  uint64_t entry_num = 0;
  // Merge file with leaf page
  std::vector<Iterator*> iter_vec;
  for (auto const& f : flushed) {
    iter_vec.emplace_back(tree_->table_cache_->NewIterator(ReadOptions(),
                                          f->number, f->file_size));
    entry_num += f->file_entry;
  }

  Page child_page = tree_->buffer_manager_->Pin(child_id);
  assert(((TreePageHeader) child_page)->is_leaf_);
  auto pi = tree_->NewPageIterator();
  if (((TreePageHeader) child_page)->item_num_ > 0) {
    ((PageIterator*) pi)->AddPage(child_id);
    iter_vec.push_back(pi);
    entry_num += ((TreePageHeader) child_page)->item_num_;
  }
  Iterator* leaf_iter = NewMergingIterator(
                &tree_->internal_comparator_, &iter_vec[0], iter_vec.size());
  result = WriteToPage(tree_, icmp_, leaf_iter, entry_num);
  BufferFinalizeMergeLeaf(level_of_files, &flushed);
  if (result.size() == 1) {
    tree_->lock_manager_->EscalateLock(pg_id_);
    // tree_->lock_manager_->EscalateLock(child_id);
    Page new_page = tree_->buffer_manager_->Pin(result.begin()->second);
    memcpy(child_page, new_page, PAGE_SIZE);
    // tree_->buffer_manager_->UnpinAndRelease(result.begin()->second);
    tree_->buffer_manager_->Delete(((TreePageHeader) new_page)->page_id_);
    ((TreePageHeader) child_page)->page_id_ = child_id;
    ((TreePageHeader) child_page)->is_leaf_ = true;
    ((TreePageHeader) child_page)->is_dirty_ = true;
    // BufferFinalizeMergeLeaf(level_of_files, &flushed);
    std::set<uint64_t> dead_files;
    GetObsoleteFilesAndInstallNewBuffer(dead_files);
    // InstallNewBuffer();
    result.clear();
    tree_->buffer_manager_->UnpinAndRelease(child_id);
    // tree_->lock_manager_->AlleviateLock(child_id);
    tree_->lock_manager_->AlleviateLock(pg_id_);
    LevelDBLSMT::RemoveFiles(tree_->env_, tree_->root_lsmt_path_, dead_files);
    return result;
  }
  tree_->buffer_manager_->UnpinAndRelease(child_id);
  return result;
}

// Page is already pinned
// Require external synchronization
void Node::DistributeFilesToNodes(std::vector<std::shared_ptr<FileMetaData>>* files,
                            std::vector<uint32_t>* new_pages,
                            std::vector<Slice>* guards,
                            std::vector<SlicePageMap>* new_pivots,
                            std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  int i = 0, file_idx = 0;
  // We MUST pin the buffer otherwise guards will become obsolete
  tree_->buffer_manager_->Lookup(pg_id_);
  // SlicePageMap overflow_children;
  while (i < guards->size() && file_idx < files->size()) {
    const Slice& pivot = guards->at(i);
    std::vector<std::shared_ptr<FileMetaData>> remains;
    if (i + 1 >= guards->size()) {
      // This is the last pivot. Flush all remaining files
      assert(icmp_->user_comparator()->Compare(pivot,
                          files->at(file_idx)->smallest.user_key()) <= 0);      
      remains = {files->begin() + file_idx, files->end()};
      file_idx = files->size();
    } else {
      const Slice& next_pivot = guards->at(i+1);
      int fi = file_idx;
      while (fi < files->size()) {
        const Slice file_start = files->at(fi)->smallest.user_key();
        if (icmp_->user_comparator()->Compare(next_pivot, file_start) < 0) {
          break;
        }
        fi++;
      }
      remains = {files->begin() + file_idx, files->begin() + fi};
      file_idx = fi;
    }
    if (i == 0) {
      // Add these files to self node's buffer
      BufferAddFiles(&remains, extra_page_);
    } else {
      uint32_t child_pg_id = new_pages->at(i - 1);
      Page p = tree_->buffer_manager_->Pin(child_pg_id);
      ReadTbb a;
      tree_->node_table_.find(a, child_pg_id);
      a->second->BufferAddFiles(&remains, p);
      a->second->InstallNewBuffer();
      a.release();
      tree_->buffer_manager_->Unpin(child_pg_id);
    }
    i++;
  }
}

static int TableCacheSize(const Options& sanitized_options) {
  // Reserve ten files or so for other uses and give the rest to TableCache.
  // We do not have other open files
  return sanitized_options.max_open_files - 10;
}

// Constructor for pure LSMT and WOT
BplusTree::BplusTree(Options options, const std::string& dbname,
                     std::atomic<uint64_t>& flush_id,
                     bool is_lsmt, bool is_buffer_tree)
  : mem_(nullptr),
    imm_(nullptr),
    shutting_down_(false),
    background_work_finished_signal_(&mutex_),
    background_compaction_scheduled_(false),
    background_flush_scheduled_(false),
    has_imm_(false),
    // read_counter_(0),
    write_active_(false),
    pushdown_active_(false),
    compact_active_(false),
    write_wait_(0),
    bg_working_(false),
    write_finished_signal_(&mutex_),
    tmp_batch_(new WriteBatch),
    internal_comparator_(InternalKeyComparator(BytewiseComparator())),
    internal_filter_policy_(InternalFilterPolicy(NewBloomFilterPolicy(10))),
    leveldb_options_(SanitizeOptions(options, &internal_comparator_, &internal_filter_policy_)),
    root_lsmt_path_(dbname),
    node_lsmt_level_limit_(options.node_lsmt_level_limit),
    lsmt_level_limit_(options.lsmt_level_limit),
    leaf_limit_(options.leaf_limit),
    tree_height_(1),
    btree_state_(new BplusTreeWriteOptState()),
    memtable_size_(options.write_buffer_size),
    max_node_id_(1),
    flush_id_(flush_id),
    scale_in_(options.buffer_shrink_ratio),
    env_(Env::Default()),
    table_cache_(new TableCache(root_lsmt_path_, leveldb_options_,
                                TableCacheSize(leveldb_options_))),
    root_(is_buffer_tree ? new Node(this, true, node_lsmt_level_limit_,
                              env_, root_lsmt_path_, flush_id_,
                              &leveldb_options_, internal_comparator_, table_cache_,
                              LSMTStatus::kRootBuffer, leaf_limit_, true) :
                           new Node(this, true, flush_id_, internal_comparator_)),
    leveldb_lsmt_(is_buffer_tree ? nullptr :
                    new LevelDBLSMT(env_, root_lsmt_path_, flush_id_, &leveldb_options_,
                                  internal_comparator_, table_cache_, lsmt_level_limit_,
                                  LSMTStatus::kStandalone, leaf_limit_)),
    buffer_manager_(NewBufferManager(options.buffer_manager_num, dbname + "/index")),
    lock_manager_(new TreeLockManager(options.buffer_manager_num)),
    root_pg_id_(0),
    seed_(0),
    flush_file_num_(options.flush_file_num),
    adapt_strategy_choice_(options.adapt_strategy),
    seq_no_(1),
    mem_empty_(true),
    mem_cold_only_(true),
    imm_cold_only_(true),
    pushdown_mem_scheduled(false),
    pushdown_imm_scheduled(false),
    pushdown_lsmt_scheduled(false),
    scheduled_flush_num_(0),
    scheduled_compaction_num_(0),
    page_split_policy_choice_(options.first_page_split_policy) {
      mutex_.Lock();
      mem_ = new MemTable(internal_comparator_);
      mem_->Ref();
      root_->pg_id_ = root_pg_id_;
      Page root_pg = buffer_manager_->Pin(root_pg_id_);
      ((TreePageHeader) root_pg)->is_leaf_ = true;
      ((TreePageHeader) root_pg)->is_dirty_ = true;
      WriteTbb a;
      node_table_.insert(a, root_pg_id_);
      a->second = root_;
      a.release();
      
      mutex_.Unlock();
    }

BplusTree::~BplusTree() {}

void BplusTree::DeleteNode(uint32_t page_id) {
  ReadTbb a;
  if (!node_table_.find(a, page_id)) {
    return;
  }
  Node* node = a->second;
  a.release();
  std::vector<uint32_t> child_ids;
  GetChildPageId(buffer_manager_, page_id, &child_ids);
  for (auto it = child_ids.begin(); it != child_ids.end(); it++) {
    DeleteNode(*it);
  }
  delete node;
}

// db_impl.cc:94
Options BplusTree::SanitizeOptions(const Options& src, const InternalKeyComparator* icmp,
                                  const InternalFilterPolicy* ipolicy) {
  Options result = src;
  result.comparator = icmp;
  result.filter_policy = ipolicy;

  if (result.block_cache == nullptr) {
    result.block_cache = NewLRUCache(8 << 20);
    fprintf(stdout, "Use 8MB block cache\n");
  }
  return result;
}

size_t BplusTree::MemoryUsage() {
  size_t total_size = 0;

  // Memtable
  total_size += mem_->ApproximateMemoryUsage();
  std::cout << "After adding memtable: " << total_size;
  // Table cache
  // total_size += table_cache_->MemoryUsage();
  // std::cout << "; after adding table_cache: " << total_size;
  // Block cache
  // As we opt to fill_cache = false, there will be consumption

  // Btree nodes (buffer manager)
  // total_size += buffer_manager_->TotalMemoryUsage();
  // std::cout << "; after adding buffer manager: " << total_size << "\n";
  return total_size;
}

void BplusTree::Print() {
  int h = tree_height_.load();
  std::cout << "Tree height is " << h << std::endl;
  if (leveldb_lsmt_ != nullptr) {
    std::cout << "Top LSMT\n";
    leveldb_lsmt_->Print();
  }
  for (int i = 1; i <= h; i++) {
    std::cout << " Tree-" << i << std::endl;
    root_->NodePrint(i);
  }
  std::cout << "Node table: " << node_table_.size() << std::endl;
}

void BplusTree::PrintStat() {
  if (mem_ != nullptr) {
    fprintf(stdout, "Memtable not null\n");
  }
  if (imm_ != nullptr) {
    fprintf(stdout, "Imm not null\n");
  }
  if (leveldb_lsmt_ != nullptr) {
    std::cout << "Top LSMT\n";
    leveldb_lsmt_->PrintStat();
  } 
  int h = tree_height_.load();
  std::cout << "Tree height is " << h << std::endl;
  if (root_->node_lsmt_ != nullptr) {
    std::cout << "Buffer Tree Root Node\n";
  }
  for (int i = 1; i < h; i++) {
    std::cout << " Tree-" << i << std::endl;
    root_->NodePrintStat(i);
  }
  std::cout << "Node table: " << node_table_.size() << std::endl;
}

Status BplusTree::Delete(std::string key) {
  return Status::NotSupported("Delete is not supported currently\n");
}

Page BplusTree::DistributePivots(uint32_t pg_id, SlicePageMap* temp_pivots,
    std::vector<uint32_t>* new_pages, std::vector<Slice>* guards) {
  int new_pg_num = new_pages->size() + 1;
  int page_item_size = (temp_pivots->size() + new_pg_num - 1) / new_pg_num;
  auto it = temp_pivots->begin();
  int offset = 0;
  // This extra page will stay in memory until it can be installed
  void* buf = malloc(PAGE_SIZE);
  memset(buf, 0, PAGE_SIZE);
  Page extra_one = (Page) buf;
  PageInit(extra_one, pg_id);
  while (it != temp_pivots->end()) {
    PageInsertIndexEntry(extra_one, it->first, it->second);
    if (offset == 0) {
      guards->emplace_back(PageReadPivotAtOffset(extra_one, 0));
    }
    offset++;
    it++;
    if (offset >= page_item_size) break;
  }
  offset = 0;
  int cur_page_idx = 0; 
  uint32_t cur_pg_id = new_pages->at(cur_page_idx);
  Page cur_page = buffer_manager_->Pin(cur_pg_id);
  while (it != temp_pivots->end()) {
    PageInsertIndexEntry(cur_page, it->first, it->second);
    if (offset == 0) {
      // guards->push_back(it->first);
      guards->emplace_back(GetPivotAtOffset(buffer_manager_, cur_pg_id, arena_wrapper_, 0));
    }
    offset++;
    if (offset >= page_item_size && cur_page_idx + 1 < new_pages->size()) {
      cur_page_idx++;
      buffer_manager_->Unpin(cur_pg_id);
      cur_pg_id = new_pages->at(cur_page_idx);
      cur_page = buffer_manager_->Pin(cur_pg_id);
      offset = 0;
    }
    it++;
  }
  buffer_manager_->Unpin(cur_pg_id);
  return extra_one;
}

// Page is already pinned
Page BplusTree::TreeSplitInternalPage(
    uint32_t pg_id,
    std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice,uint32_t>>* old_pivots,
    std::vector<uint32_t>* new_pages,
    std::vector<Slice>* guards,
    std::vector<std::shared_ptr<FileMetaData>>* out_file_names) {
  SlicePageMap temp_pivots;
  Page page = buffer_manager_->Lookup(pg_id);
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
  Page extra_page = DistributePivots(pg_id, &temp_pivots, new_pages, guards);
  temp_pivots.clear();
  ReadTbb a;
  Node* node = nullptr;
  if (node_table_.find(a, pg_id)) {
    node = a->second;
  }
  a.release();
  if (node->GetNodeLSMT() != nullptr) {
    node->BufferCompactTree(guards, out_file_names);
    node->BufferClear();
  }
  assert(!node->node_above_leaf);
  return extra_page;
}

// This node is already write-locked
void BplusTree::UpdateOrRewritePivots(
    uint32_t pg_id, std::vector<SlicePageMap>* new_pivots,
    std::vector<std::pair<Slice, uint32_t>>* old_pivots) {
  SlicePageMap tmp;
  for (size_t i = 0; i < old_pivots->size(); i++) {
    auto it = new_pivots->at(i).begin();
    if (old_pivots->at(i).first.compare(it->first) != 0) {
      fprintf(stdout, "%s != %s\n",
                      old_pivots->at(i).first.ToString().c_str(),
                      it->first.ToString().c_str());
      fprintf(stdout, "Child: %u?%u\n", old_pivots->at(i).second, it->second);
      fflush(stdout);
    }
    assert(old_pivots->at(i).first.compare(it->first) == 0);
    // Clean the old page and node
    uint32_t old_child = old_pivots->at(i).second;
    // assert(old_child == it->second);
    if (old_child != it->second) {
      UpdateChildbyPivot(buffer_manager_, pg_id, it->first, it->second);
    }
    it++;
    for (; it != new_pivots->at(i).end(); it++) {
      tmp[it->first] = it->second;
    }
  }
  AddNewPivots(buffer_manager_, pg_id, &tmp);
}

Status BplusTree::Insert(WriteBatch* updates) {
  Writer w(&mutex_);
  w.batch = updates;
  w.sync = false; // hard-coded to not sync
  w.done = false;

  MutexLock l(&mutex_);
  writers_.push_back(&w);
  while (!w.done && &w != writers_.front()) {
    w.cv.Wait();
  }
  if (w.done) {
    return w.status;
  }

  // May temporarily unlock and wait.
  Status status = MakeRoomForWrite(updates == nullptr);
  uint64_t last_sequence = seq_no_.load();
  Writer* last_writer = &w;
  if (status.ok() && updates != nullptr) {  // nullptr batch is for compactions
    WriteBatch* write_batch = BuildBatchGroup(&last_writer);
    WriteBatchInternal::SetSequence(write_batch, last_sequence + 1);
    seq_no_.fetch_add(WriteBatchInternal::Count(write_batch));
    if (mem_empty_.load()) mem_empty_.store(false, std::memory_order_release);
    if (mem_cold_only_.load() && updates->HasHotspot()) {
      mem_cold_only_.store(false, std::memory_order_release);
    }

    {
      mutex_.Unlock();
      if (status.ok()) {
        status = WriteBatchInternal::InsertInto(write_batch, mem_);
      }
      mutex_.Lock();
    }
    if (write_batch == tmp_batch_) tmp_batch_->Clear();

    // versions_->SetLastSequence(last_sequence);
  }

  while (true) {
    Writer* ready = writers_.front();
    writers_.pop_front();
    if (ready != &w) {
      ready->status = status;
      ready->done = true;
      ready->cv.Signal();
    }
    if (ready == last_writer) break;
  }

  // Notify new head of write queue
  if (!writers_.empty()) {
    writers_.front()->cv.Signal();
  }

  // write_active_.store(false);
  // write_finished_signal_.SignalAll();
  return status;
}

// REQUIRES: Writer list must be non-empty
// REQUIRES: First writer must have a non-null batch
WriteBatch* BplusTree::BuildBatchGroup(Writer** last_writer) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  Writer* first = writers_.front();
  WriteBatch* result = first->batch;
  assert(result != nullptr);

  size_t size = WriteBatchInternal::ByteSize(first->batch);

  size_t max_size = 1 << 20;
  if (size <= (128 << 10)) {
    max_size = size + (128 << 10);
  }

  *last_writer = first;
  std::deque<Writer*>::iterator iter = writers_.begin();
  ++iter;  // Advance past "first"
  for (; iter != writers_.end(); ++iter) {
    Writer* w = *iter;
    if (w->sync && !first->sync) {
      // Do not include a sync write into a batch handled by a non-sync write.
      break;
    }

    if (w->batch != nullptr) {
      size += WriteBatchInternal::ByteSize(w->batch);
      if (size > max_size) {
        // Do not make batch too big
        break;
      }

      // Append to *result
      if (result == first->batch) {
        // Switch to temporary batch instead of disturbing caller's batch
        result = tmp_batch_;
        assert(WriteBatchInternal::Count(result) == 0);
        WriteBatchInternal::Append(result, first->batch);
      }
      WriteBatchInternal::Append(result, w->batch);
    }
    *last_writer = w;
  }
  return result;
}

void BplusTree::BGWork(void* db) {
  reinterpret_cast<BplusTree*>(db)->BackgroundCall();
}

// BG pushdown thread
// Assume LSMT bottom level files are compacted based root pivot
void BplusTree::InstallLSMTfilesToBol() {
  if (IsLeafPage(buffer_manager_, root_pg_id_)) {
    BulkloadLSMTFiles();
  } else {
    std::vector<std::shared_ptr<FileMetaData>> files;
    lock_manager_->ReadLock(-1);
    lock_manager_->ReadLock(root_pg_id_);
    // int bottom_level = leveldb_lsmt_->GetBottomCompactedFiles(&files);
    int bottom_level;
    if (flush_file_num_ <= 0) {
      bottom_level = leveldb_lsmt_->GetAllBottomLevelFiles(&files);
    } else {
      bottom_level = leveldb_lsmt_->GetSomeBottomLevelFiles(&files, flush_file_num_);
    }
    // int bottom_level = leveldb_lsmt_->GetAllBottomLevelFiles(&files);
    if (bottom_level < 0) {
      lock_manager_->ReadUnlock(root_pg_id_);
      lock_manager_->ReadUnlock(-1);
      return;
    }
    if (FileAcrossPivots(buffer_manager_, root_pg_id_, &files,
                         internal_comparator_.user_comparator())) {
      // If the file ranges do not match, compact again with guards
      files.clear();
      Page rt = buffer_manager_->Lookup(root_pg_id_);
      std::set<uint64_t> dead_files;
      lock_manager_->EscalateLock(-1);
      leveldb_lsmt_->CompactBottomLevel(rt);
      leveldb_lsmt_->GetObsoleteFiles(dead_files);
      lock_manager_->AlleviateLock(-1);
      LevelDBLSMT::RemoveFiles(env_, root_lsmt_path_, dead_files);
      // If the bottom level is compacted, then we flush the entire level
      bottom_level = leveldb_lsmt_->GetAllBottomLevelFiles(&files);
    }
    if (files.size() > 0) {
      // Flushed files may not include the min pivot
      lock_manager_->EscalateLock(root_pg_id_);
      Page rt = buffer_manager_->Lookup(root_pg_id_);
      root_->MayUpdateMinPivot(rt, files[0]->smallest.user_key());
      lock_manager_->AlleviateLock(root_pg_id_);
      FlushLSMTfilesToBol(bottom_level, &files);
      files.clear();
    } else {
      lock_manager_->ReadUnlock(root_pg_id_);
      lock_manager_->ReadUnlock(-1);
    }
  }
  lock_manager_->WriteLock(-1);
  leveldb_lsmt_->Finalize();
  lock_manager_->WriteUnlock(-1);
  MaybeScheduleCompaction();
}

// BG pushdown thread
// Root node has been read-locked
void BplusTree::FlushLSMTfilesToBol(int level_of_files,
                                  std::vector<std::shared_ptr<FileMetaData>>* files,
                                  bool one_level_only) {
  if (FileAcrossPivots(buffer_manager_, root_pg_id_, files,
                        internal_comparator_.user_comparator())) {
    Page rt = buffer_manager_->Lookup(root_pg_id_);
    PagePrint(rt);
    fprintf(stderr, "Error: files across pivots\n");
    std::abort();
  }
  std::vector<SlicePageMap> new_pivots;
  std::vector<std::pair<Slice, uint32_t>> old_pivots;
  // Root pivots should have already been updated!!!
  root_->FlushFilesToChildren(level_of_files, files, &new_pivots, &old_pivots, one_level_only);
  if (new_pivots.size() > 0) {
    root_->UpdatePivots(&new_pivots, &old_pivots);
  }
  // Remove filemetadata in the old LSMT
  // files->clear();

  if (root_->node_overflow_) {
    auto result = root_->SplitNodeAndLSMT(
                                &new_pivots, &old_pivots);
#ifdef BREAKDOWN
    uint64_t start = _rdtsc();
#endif
    lock_manager_->EscalateLock(root_pg_id_);
    UpdateRoot(result);
    lock_manager_->AlleviateLock(root_pg_id_);
#ifdef BREAKDOWN
    uint64_t end = _rdtsc();
    writer_stat_.lsmt_ltime.fetch_add(end - start);
#endif
  }
  lock_manager_->ReadUnlock(root_pg_id_);
}

void BplusTree::StopAdaptToRead() {
  MutexLock l(&mutex_);
  bool hybrid = btree_state_->has_buffer_page;
  if (btree_state_ != nullptr) delete btree_state_;
  auto state = new BplusTreeWriteOptState();
  state->has_buffer_page = hybrid;
  ChangeState(state);
  delete page_split_policy_;
  page_split_policy_ = nullptr;
  page_split_policy_choice_ = leveldb_options_.second_page_split_policy;
  // Reduce the level limit of rootLSM
  if (hybrid) {
    uint32_t new_level = lsmt_level_limit_ > 1 ? lsmt_level_limit_ - 1 : 1;
    leveldb_lsmt_->SetLevelLimit(new_level);
  }
}

void BplusTree::AdaptToRead() {
  MutexLock l(&mutex_);
  if (btree_state_ != nullptr) delete btree_state_;
  auto state = new BplusTreeReadOptState(leveldb_options_.proactive_validation);
  state->has_buffer_page = true;
  ChangeState(state);
}

bool BplusTree::BGCompactionIdle() {
  MutexLock l(&mutex_);
  if (!env_->CompactionWorkRunning()) {
    return true;
  }
  return false;
}

bool BplusTree::BGFlushIdle() {
  MutexLock l(&mutex_);
  if (!env_->PushdownWorkRunning()) {
    return true;
  }
  return false;
}

void BplusTree::SetHotspotRange(std::string low, std::string up) {
  assert(low < up);
  low_hot_key_ = low;
  up_hot_key_ = up;
  GetAdaptStrategy()->SetHotKeys(low, up);
}

void BplusTree::AddMemCompactionWork() {
  mutex_.AssertHeld();
  if (has_imm_ || pushdown_mem_scheduled.load()) {
    return;
  }
  pushdown_mem_scheduled.store(true, std::memory_order_release);
  scheduled_flush_num_.fetch_add(1);
  env_->SchedulePushdown(&BplusTree::BGAdaptMem, this);
}

void BplusTree::BGAdaptMem(void* db) {
  reinterpret_cast<BplusTree*>(db)->AdaptMem();
}

void BplusTree::AddImmCompactionWork() {
  mutex_.AssertHeld();
  if (!has_imm_ || pushdown_imm_scheduled.load()) {
    return;
  }
  pushdown_imm_scheduled.store(true, std::memory_order_release);
  scheduled_flush_num_.fetch_add(1);
  env_->SchedulePushdown(&BplusTree::BGAdaptImm, this);
}

void BplusTree::BGAdaptImm(void* db) {
  reinterpret_cast<BplusTree*>(db)->AdaptImm();
}

void BplusTree::AddLSMTFlushWork() {
  mutex_.AssertHeld();
  // scheduled_flush_num_.fetch_add(1);
  env_->SchedulePushdown(&BplusTree::BGFlushLSMT, this);
}

void BplusTree::BGFlushLSMT(void* db) {
  reinterpret_cast<BplusTree*>(db)->FlushLSMT();
}

void BplusTree::FlushLSMT() {
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    // scheduled_flush_num_.fetch_sub(1);
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  {
    mutex_.Unlock();
    InstallLSMTfilesToBol();
    mutex_.Lock();
  }
  bg_working_.store(false, std::memory_order_release);
  background_flush_scheduled_.store(false, std::memory_order_release);
  // scheduled_flush_num_.fetch_sub(1);
  write_finished_signal_.SignalAll();
}

void BplusTree::AddLSMTCompactionWork() {
  // MutexLock l(&mutex_);
  mutex_.AssertHeld();
  if (!mem_empty_.load() || has_imm_.load() || pushdown_lsmt_scheduled.load()) {
    return;
  }
  pushdown_lsmt_scheduled.store(true, std::memory_order_release);
  scheduled_flush_num_.fetch_add(1);
  env_->SchedulePushdown(&BplusTree::BGAdaptLSMT, this);
}

void BplusTree::BGAdaptLSMT(void* db) {
  reinterpret_cast<BplusTree*>(db)->AdaptLSMT();
}

void BplusTree::AdaptLSMT() {
#ifdef BREAKDOWN
  writer_stat_.lsmt_cnt.fetch_add(1);
  uint64_t start = __rdtsc(), end;
#endif 
  MutexLock l(&mutex_);
  if (shutting_down_.load()) {
    scheduled_flush_num_.fetch_sub(1);
    pushdown_lsmt_scheduled.store(false, std::memory_order_release);
#ifdef BREAKDOWN
    end = _rdtsc();
    writer_stat_.lsmt_ltime.fetch_add(end - start);
#endif 
    return;
  }
  bg_working_.store(true, std::memory_order_release);
  {
    mutex_.Unlock();
    GetAdaptStrategy()->AdaptLSMT();
    mutex_.Lock();
  }
  pushdown_lsmt_scheduled.store(false, std::memory_order_release);
  bg_working_.store(false, std::memory_order_release);
  scheduled_flush_num_.fetch_sub(1);
  write_finished_signal_.SignalAll();
#ifdef BREAKDOWN
  end = _rdtsc();
  writer_stat_.lsmt_ltime.fetch_add(end - start);
#endif 
}

void BplusTree::BulkloadLSMTFiles() {
  // Get bottom level files
  lock_manager_->ReadLock(-1);
  int level = leveldb_lsmt_->GetBottomLevel();
  assert(level > 0);
  const std::vector<std::shared_ptr<FileMetaData>>* bottom_files =
                          leveldb_lsmt_->GetBottomLevelFiles(nullptr);
  assert(bottom_files->size() > 1);
  std::vector<uint32_t> top_level;
  int add_height = BulkloadFiles(bottom_files, &top_level, false);

  lock_manager_->EscalateLock(-1);
  lock_manager_->WriteLock(root_pg_id_);
  tree_height_.fetch_add(add_height);
  ReadTbb a;
  node_table_.find(a, top_level[0]);
  delete a->second;
  a.release();
  node_table_.erase(top_level[0]);

  lock_manager_->WriteLock(top_level[0]);
  Page new_root = buffer_manager_->Pin(top_level[0]);
  Page old_root = buffer_manager_->Lookup(root_pg_id_);
  memcpy(old_root, new_root, PAGE_SIZE);

  ((TreePageHeader) old_root)->page_id_ = root_pg_id_;//top_level[0];
  ((TreePageHeader) old_root)->is_leaf_ = false;
  ((TreePageHeader) old_root)->is_dirty_ = true;
  // buffer_manager_->UnpinAndRelease(top_level[0]);
  lock_manager_->WriteUnlock(top_level[0]);
  buffer_manager_->Delete(top_level[0]);

  // root_->SetNodeLsmt(LSMTStatus::kRootBuffer);
  // lock_manager_->EscalateLock(-1);
  leveldb_lsmt_->ClearBottomLevelFiles();

  // Let lsmt decide which level needs to be compacted next
  // leveldb_lsmt_->Finalize();
  lock_manager_->WriteUnlock(root_pg_id_);
  lock_manager_->AlleviateLock(-1);
  lock_manager_->ReadUnlock(-1);
}

void BplusTree::AddNewNode(uint32_t pg_id, bool leaf_node,
                           std::shared_ptr<FileMetaData> meta, bool small_leaf,
                           size_t file_size) {
  int level_lim = leaf_node ? (small_leaf ? 1 : node_lsmt_level_limit_) : node_lsmt_level_limit_;
  LSMTStatus lsmt_status = leaf_node ?
                  (small_leaf ? LSMTStatus::kSmallLeaf : LSMTStatus::kLeaf) : LSMTStatus::kBuffer;
  int leaf_lim = /*small_leaf ? 1 : */leaf_limit_;
  Node* node = new Node(this, leaf_node, level_lim, env_, root_lsmt_path_,
                        flush_id_, &leveldb_options_, internal_comparator_,
                        table_cache_, lsmt_status, leaf_lim, true);
  if (file_size != 0) {
    node->node_lsmt_->output_file_size = file_size;
  }
  WriteTbb a;
  if (node_table_.find(a, pg_id)) {
    delete a->second;
  }
  node_table_.insert(a, pg_id);
  a->second = node;
  // node_table_[pg_id] = node;
  node->pg_id_ = pg_id;

  if (meta != nullptr) {
    node->node_lsmt_->AddFile(0, *meta.get(), nullptr);
  }
}

int BplusTree::BulkloadFiles(
          const std::vector<std::shared_ptr<FileMetaData>>* bottom_files,
          std::vector<uint32_t>* top_level, bool small_leaf) {
  // Get one user key to know its key size
  auto f = bottom_files->at(0);
  size_t key_size = f->smallest.user_key().size();
  int add_height = 0;
  int top_level_size = bottom_files->size();
  bool leaf_level = true;
  std::vector<uint32_t> new_nodes;
  
  while (top_level_size > 1) {
    uint32_t insert_to_id = buffer_manager_->Allocate();
    AddNewNode(insert_to_id, false, nullptr, small_leaf);
    Page insert_to_page = buffer_manager_->Pin(insert_to_id);
    // ((TreePageHeader) insert_to_page)->is_dirty_ = true;
    new_nodes.push_back(insert_to_id);
    int top_idx = 0;
    while (top_idx < top_level_size) {
      Slice min_k;
      uint32_t inserted;
      if (leaf_level) {
        inserted = buffer_manager_->Allocate();
        AddNewNode(inserted, true, bottom_files->at(top_idx), small_leaf);
        Page leaf_page = buffer_manager_->Pin(inserted);
        ((TreePageHeader) leaf_page)->is_leaf_ = true;
        ((TreePageHeader) leaf_page)->is_dirty_ = true;
        min_k = bottom_files->at(top_idx)->smallest.user_key();
      } else {
        inserted = top_level->at(top_idx);
        Page inserted_page = buffer_manager_->Pin(inserted);
        min_k = PageReadPivotAtOffset(inserted_page, 0);       
      }
      size_t free_space = PageGetFreeSpace(insert_to_page);
      if (free_space < key_size + 3 * ITEMID_SIZE) {
        buffer_manager_->UnpinAndRelease(insert_to_id);
        insert_to_id = buffer_manager_->Allocate();
        AddNewNode(insert_to_id, false, nullptr, small_leaf);
        insert_to_page = buffer_manager_->Pin(insert_to_id);
        new_nodes.push_back(insert_to_id);
      }
      PageInsertIndexEntry(insert_to_page, min_k, inserted);
      buffer_manager_->UnpinAndRelease(inserted);
      top_idx++;
    }
    buffer_manager_->UnpinAndRelease(insert_to_id);
    if (!leaf_level) {
      top_level->clear();
    }
    leaf_level = false;
    new_nodes.swap(*top_level);
    top_level_size = top_level->size();
    add_height++;
  }
  return add_height;
}

void BplusTree::AddWOTCompactionWork() {
  // MutexLock l(&mutex_);
  mutex_.AssertHeld();
  if (!mem_empty_.load() || has_imm_.load() ||
      leveldb_lsmt_->HasHotspot(low_hot_key_, up_hot_key_)) {
    return;
  }
  scheduled_flush_num_.fetch_add(1);
  env_->SchedulePushdown(&BplusTree::BGAdaptWOT, this);
}

void BplusTree::BGAdaptWOT(void* db) {
  reinterpret_cast<BplusTree*>(db)->AdaptWOT();
  // reinterpret_cast<BplusTree*>(db)->AdaptWOTNoSplit();
}

void BplusTree::ChangeState(BplusTreeState* state) {
  btree_state_ = state;
}

Status BplusTree::Insert(std::string key, std::string value) {
  std::string ikey;
  AppendInternalKey(&ikey,
                    ParsedInternalKey(key, seq_no_.fetch_add(1), kTypeValue));
  ReadTbb a;
  node_table_.find(a, root_pg_id_);
  Node* root = a->second;
  a.release();
  assert(!IsLeafPage(buffer_manager_, root->pg_id_));
  TreeInsert(std::make_pair(ikey, value));
  return Status::OK();
}

// void BplusTree::BGTreeInsert(void* db) {
//   reinterpret_cast<BplusTree*>(db)->TreeInsert();
// }

Status BplusTree::Query(std::string key, std::string* value) {
  return Status::NotSupported("Operation not supported");
}

SortedTreeIterator* BplusTree::NewSortedTreeIterator() {
  auto iter = new SortedTreeIterator(this, seed_.fetch_add(1),
                                     user_comparator(),
                                     internal_comparator_);
  return iter;
}

void BplusTree::RecordReadSample(Slice key) {
  // Root should be locked. Potential issue here.
  if (TreeHeight() == 1) {
    MutexLock l(&mutex_);
    // leveldb_lsmt_ is already read-locked
    if (leveldb_lsmt_->RecordReadSample(key)) {
      MaybeScheduleCompaction();
    }
  }
}

Iterator* BplusTree::NewPageIterator() {
  return new PageIterator(this);
}

DefaultAdapt* BplusTree::GetAdaptStrategy() {
  if (adapt_strategy_ == nullptr) {
    // Strategies in use:
    // DefaultAdapt: default strategy where queue size is limited
    // PrioritySizeLimitAdapt: priority-based strategy where queue size is limited
    // PriorityNoDupAdapt: priority-based strategy where tasks are not duplicated
    // HotOnlyAdapt: only hot items are pushed down
    switch (adapt_strategy_choice_) {
      case 2:
        adapt_strategy_ = new PrioritySizeLimitAdapt(this);
        break;
      case 3:
        adapt_strategy_ = new PriorityNoDupAdapt(this);
        break;
      case 4:
        adapt_strategy_ = new HotOnlyAdapt(this);
        break;
      case 5:
        adapt_strategy_ = new BalancedAdapt(this);
        break;
      default:
        adapt_strategy_ = new DefaultAdapt(this);
        break;
    }
  }
  return adapt_strategy_;
}

// If the selectivity is fixed, up is an empty string
bool BplusTree::RangeWithinHotspot(const Slice& low, const Slice& up) {
  if (low_hot_key_.empty() && up_hot_key_.empty()) {
    // When the entire key space is hotspot
    return true;
  }
  if (up.empty()) {
    // Fixed selectivity where only start key (low key) is given
    if (low.compare(low_hot_key_) >= 0 && low.compare(up_hot_key_) <= 0) {
      return true;
    }
    return false;
  }
  if (low.compare(low_hot_key_) >= 0 && up.compare(up_hot_key_) <= 0) {
    // Hard limit. This hotspot range already includes the query range
    return true;
  }
  return false;
}

namespace {

struct IteratorState {
  port::Mutex* const mu;
  MemTable* const mem GUARDED_BY(mu);
  MemTable* const imm GUARDED_BY(mu);

  IteratorState(port::Mutex* mutex, MemTable* mem, MemTable* imm)
      : mu(mutex), mem(mem), imm(imm) {}
};

static void CleanupIteratorState(void* arg1, void* arg2) {
  IteratorState* state = reinterpret_cast<IteratorState*>(arg1);
  state->mu->Lock();
  if (state->mem != nullptr) state->mem->Unref();
  if (state->imm != nullptr) state->imm->Unref();
  state->mu->Unlock();
  delete state;
}

}  // anonymous namespace

void SortedTreeIterator::InitInnerIter(const Slice& low, const Slice& up) {
  MemTable* mem = nullptr;
  MemTable* imm = nullptr;
  std::vector<Iterator*> iter_vec;
  int non_page_num = 0;
  tree_->btree_state_->NewIterator(tree_, &iter_vec, low, up, &mem,
                                        &imm, &locked_nodes_,
                                        user_comparator_, &non_page_num);
  inner_iter_ = NewMergingIterator(&internal_comparator_, &iter_vec[0],
                                   iter_vec.size());
  if (non_page_num == 0) {
    all_page_ = true;
  }
  IteratorState* cleanup = new IteratorState(&tree_->mutex_, mem, imm);
  inner_iter_->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);
}

void SortedTreeIterator::Seek(const Slice& target) {}

void SortedTreeIterator::SeekBothEnds(const Slice& low, const Slice& up) {
  InitInnerIter(low, up);
  saved_key_.clear();
  AppendInternalKey(&saved_key_,
                    ParsedInternalKey(low, tree_->seq_no_.load(), kValueTypeForSeek));
  inner_iter_->Seek(saved_key_);
  valid_ = true;
  if (inner_iter_->Valid()) {
    FindNextUserEntry(false, &saved_key_);
  } else {
    valid_ = false;
  }
}

void SortedTreeIterator::FindNextUserEntry(bool skipping, std::string* skip) {
  // Loop until we hit an acceptable entry to yield
  assert(inner_iter_->Valid());
  do {
    ParsedInternalKey ikey;
    if (ParseKey(&ikey) && ikey.sequence <= tree_->seq_no_.load() ) {
      switch (ikey.type) {
        case kTypeDeletion:
          // Arrange to skip all upcoming entries for this key since
          // they are hidden by this deletion.
          SaveKey(ikey.user_key, skip);
          skipping = true;
          break;
        case kTypeValue:
          if (skipping &&
              user_comparator_->Compare(ikey.user_key, *skip) <= 0) {
            // Entry hidden
          } else {
            valid_ = true;
            saved_key_.clear();
            return;
          }
          break;
      }
    }
    inner_iter_->Next();
  } while (inner_iter_->Valid());
  saved_key_.clear();
  valid_ = false;
}

void SortedTreeIterator::Next() {
  assert(valid_);
  if (all_page_) {
    // If iterator contains page only, then there cannot be duplicates or deletions
    inner_iter_->Next();
    if (!inner_iter_->Valid()) {
      valid_ = false;
    }
    return;
  }
  SaveKey(ExtractUserKey(inner_iter_->key()), &saved_key_);
  inner_iter_->Next();
  if (!inner_iter_->Valid()) {
    valid_ = false;
    saved_key_.clear();
    return;
  }

  FindNextUserEntry(true, &saved_key_);
}

bool SortedTreeIterator::Valid() { return valid_; }

std::string SortedTreeIterator::key() {
  assert(valid_);
  return ExtractUserKey(inner_iter_->key()).ToString();
}

std::string SortedTreeIterator::value() {
  assert(valid_);
  return inner_iter_->value().ToString();
}

} // namespace WOT_NAMESPACE