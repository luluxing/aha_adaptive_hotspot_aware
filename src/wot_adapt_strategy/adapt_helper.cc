#include "wot_index.h"
#include "../util/utils.h"

namespace WOT_NAMESPACE {

SlicePageMap Node::SplitLeafAndLSMT() {
  SlicePageMap result;
  int split_num;
  // Page is already pinned
  Page this_page = tree_->buffer_manager_->Lookup(pg_id_);

  // Leaf node is split into more than 2
  // when root is leaf, it will never split
  assert(node_lsmt_ != nullptr); 
  uint64_t total_size = node_lsmt_->TotalFileSizeLSMT();
  int alloc_size = node_lsmt_->output_file_size;
  split_num = static_cast<int>(static_cast<double>(total_size) / alloc_size + 1);
  split_num = split_num < 2 ? 2 : split_num;

  assert(split_num >= 2);
  std::vector<std::shared_ptr<FileMetaData>> out_file_names;
  Status s;
  s = BufferCompactTree(&split_num, &out_file_names);
  // InstallNewBuffer();
  BufferClear();
  SetBufferStatus(LSMTStatus::kSmallLeaf);
  SetBufferLevelLimit(1);

  std::vector<uint32_t> new_pages;
  for (int i = 0; i < split_num - 1; i++) {
    uint32_t p_id = tree_->buffer_manager_->Allocate();
    Page p = tree_->buffer_manager_->Pin(p_id);
    ((TreePageHeader) p)->is_leaf_ = true;
    ((TreePageHeader) p)->is_dirty_ = true;
    new_pages.push_back(((TreePageHeader) p)->page_id_);
    tree_->buffer_manager_->Unpin(p_id);
  }
  
  if (out_file_names.size() == 1) {
    // LOCK 
    // tree_->BGObtainLock();
    tree_->lock_manager_->EscalateLock(pg_id_);
    BufferAddFiles(&out_file_names, this_page); // we do not check overflow or not
    InstallNewBuffer();
    tree_->lock_manager_->AlleviateLock(pg_id_);
    // tree_->BGReleaseLock();
    // UNLOCK
    for (auto const& page : new_pages) {
      tree_->buffer_manager_->Delete(page);
    }
    out_file_names.clear();
    return result;
  }  

  for (auto const & new_page : new_pages) {
    LSMTStatus lsmt_status = LSMTStatus::kSmallLeaf;
    int lsmt_level_lim = 1;
    Node* node = new Node(tree_, true, lsmt_level_lim,
                          tree_->env_, tree_->root_lsmt_path_,
                          tree_->flush_id_, &tree_->leveldb_options_,
                          tree_->internal_comparator_, tree_->table_cache_,
                          lsmt_status, tree_->leaf_limit_, true);
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

  if (out_file_names.size() > 0) {
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
  tree_->nodes_in_progress.push_back(pg_id_);
  return result;
}


} // namespace WOT_NAMESPACE