#ifndef BTREE_OLC_WOT_NAMESPACE_H_
#define BTREE_OLC_WOT_NAMESPACE_H_

#include <atomic>
#include <map>
#include <stack>

#include "leveldb/include/iterator.h"
#include "leveldb/include/status.h"
#include "leveldb/dbformat.h"
#include "wot_buf_mgr/arena_wrapper.h"
#include "wot_buf_mgr/buffer_manager.h"

#include "wot_buf_mgr/node_page.h"

namespace WOT_NAMESPACE {

class ScanIterator;

// This has the same interface as the lock-coupled B+-tree.
class BtreeOLC {
 public:
  using KVType = std::pair<Slice, Slice>;

  BtreeOLC(std::map<std::string, std::string>* props)
  : root_page_id_(0),
    tree_height_(1),
    internal_comparator_(InternalKeyComparator(BytewiseComparator())),
    buf_mgr_(NewBufferManager(std::stoi((*props)["buffer_manager_num"]),
                                 (*props)["buffer_file"])) {
    Page root = buf_mgr_->Pin(root_page_id_);
    ((TreePageHeader) root)->is_leaf_ = true;
  }

  ~BtreeOLC() { delete buf_mgr_; }

  // If key exists, the old value is replaced with the new
  Status Insert(std::string key, std::string val) {
    Insert(Slice(key), Slice(val));
  }

  Status Insert(const Slice& key, const Slice& val) {
    int restart_count = 0;
  restart:
    if (restart_count++)
      yield(restart_count);
    bool need_restart = false;

    // Current node
    Page cur_page = buf_mgr_->Pin(root_page_id_);
    uint64_t version_node = ((TreePageHeader) cur_page)->ReadLockOrRestart(need_restart);
    if (need_restart || (((TreePageHeader) cur_page)->page_id_ != root_page_id_)) {
      goto restart;
    }

    uint64_t version_parent;
    Page parent_page = nullptr;

    // Need to pin both the parent and the child and unpin before restart
    while (!((TreePageHeader) cur_page)->is_leaf_) {
      // Split eagerly if full
      if (InnerPageIsFull(cur_page, key)) {
        // Lock
        if (parent_page) {
          ((TreePageHeader) parent_page)->UpgradeToWriteLockOrRestart(version_parent, need_restart);
          if (need_restart) {
            buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
            buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
            goto restart;
          }
        }
	      ((TreePageHeader) cur_page)->UpgradeToWriteLockOrRestart(version_node, need_restart);
        if (need_restart) {
          if (parent_page) {
            ((TreePageHeader) parent_page)->WriteUnlock();
            buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
          }
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
        if (!parent_page && (((TreePageHeader) cur_page)->page_id_ != root_page_id_)) {
          // there's a new parent
          ((TreePageHeader) cur_page)->WriteUnlock();
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
	      // Split
        Slice sep;
        uint32_t new_pg = SplitInnerNode(((TreePageHeader) cur_page)->page_id_, sep);
        if (parent_page) {
          PageInsertPivotPair(parent_page, sep, new_pg);
        } else {
          MakeRoot(sep, new_pg);
        }
        // Unlock and restart
        ((TreePageHeader) cur_page)->WriteUnlock();
        if (parent_page) {
          ((TreePageHeader) parent_page)->WriteUnlock();
          buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        }
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }

      if (parent_page) {
	      ((TreePageHeader) parent_page)->ReadUnlockOrRestart(version_parent, need_restart);
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
	      if (need_restart) {
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
      }
 
      parent_page = cur_page;
      version_parent = version_node;

      uint32_t child = PageReadChildAt(cur_page, PageLowerBound(cur_page, key));
      ((TreePageHeader) parent_page)->CheckOrRestart(version_node, need_restart);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        goto restart;
      }
      cur_page = buf_mgr_->Pin(child);
      version_node = ((TreePageHeader) cur_page)->ReadLockOrRestart(need_restart);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
    }

    // Split leaf if full
    if (LeafPageIsFull(cur_page, key, val)) {
      // Lock
      if (parent_page) {
        ((TreePageHeader) parent_page)->UpgradeToWriteLockOrRestart(version_parent, need_restart);
        if (need_restart) {
          if (parent_page) {
            buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
          }
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
      }
      ((TreePageHeader) cur_page)->UpgradeToWriteLockOrRestart(version_node, need_restart);
      if (need_restart) {
        if (parent_page) {
          ((TreePageHeader) parent_page)->WriteUnlock();
          buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        }
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
      if (!parent_page &&
          (((TreePageHeader) cur_page)->page_id_ != root_page_id_)) { // there's a new parent
        ((TreePageHeader) cur_page)->WriteUnlock();
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
      // Split
      Slice sep;
      uint32_t new_pg = SplitLeafNode(((TreePageHeader) cur_page)->page_id_, sep);
      if (parent_page)
        PageInsertPivotPair(parent_page, sep, new_pg);
      else
	      MakeRoot(sep, new_pg);
      // Unlock and restart
      ((TreePageHeader) cur_page)->WriteUnlock();
      if (parent_page) {
	      ((TreePageHeader) parent_page)->WriteUnlock();
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
      }
      buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
      goto restart;
    } else {
      // only lock leaf node
      ((TreePageHeader) cur_page)->UpgradeToWriteLockOrRestart(version_node, need_restart);
      if (need_restart) {
        if (parent_page) {
          buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        }
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
      if (parent_page) {
        ((TreePageHeader) parent_page)->ReadUnlockOrRestart(version_parent, need_restart);
        if (need_restart) {
          ((TreePageHeader) cur_page)->WriteUnlock();
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
          goto restart;
        }
      }
      PageInsertKvpair(cur_page, key, val);
      ((TreePageHeader) cur_page)->WriteUnlock();
      if (parent_page) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
      }
      buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
    }

    return Status::OK();
  }

  // If key does not exist, insert the pair.
  // Otherwise replace the old value with the new one
  Status Update(std::string key, std::string val) {
    return Insert(key, val);
  }

  Status Query(std::string key, std::string* val) {
    int restart_count = 0;
  restart:
    if (restart_count++)
      yield(restart_count);
    bool need_restart = false;

    Page cur_page = buf_mgr_->Pin(root_page_id_);
    uint64_t version_node = ((TreePageHeader) cur_page)->ReadLockOrRestart(need_restart);
    if (need_restart || (((TreePageHeader) cur_page)->page_id_ != root_page_id_)) {
      goto restart;
    }
    
    // Parent of current node
    Page parent_page = nullptr;
    uint64_t version_parent;

    while (!((TreePageHeader) cur_page)->is_leaf_) {
      if (parent_page) {
	      ((TreePageHeader) parent_page)->ReadUnlockOrRestart(version_parent, need_restart);
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
	      if (need_restart) {
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
      }

      parent_page = cur_page;
      version_parent = version_node;

      uint32_t child = PageReadChildAt(cur_page, PageLowerBound(cur_page, key));
      ((TreePageHeader) parent_page)->CheckOrRestart(version_node, need_restart);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        goto restart;
      }
      cur_page = buf_mgr_->Pin(child);
      version_node = ((TreePageHeader) cur_page)->ReadLockOrRestart(need_restart);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
    }

    // increment because of leaf
    uint32_t pos = PageLowerBound(cur_page, key) + 1;
    bool success = false;
    if ((pos < ((TreePageHeader) cur_page)->item_num_)) {
      Slice k;
      PageReadKeyAtOffset(cur_page, pos, k);
      if (k.compare(key) == 0) {
        success = true;
        Slice v;
        PageReadValAt(cur_page, pos, v);
        Slice nv = Slice(NewPivot(v), v.size());
        *val = nv.ToString();
      }
    }
    if (parent_page) {
      ((TreePageHeader) parent_page)->ReadUnlockOrRestart(version_parent, need_restart);
      buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
    }
    ((TreePageHeader) cur_page)->ReadUnlockOrRestart(version_node, need_restart);
    buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
    if (need_restart) {
      goto restart;
    }
    if (success) return Status::OK();
    return Status::NotFound("Key not found");
  }

  // void Print();

  // void PrintStat();

  Iterator* BtreeOLCScanIterator(const Slice& low, const Slice& up) {
    std::vector<KVType> results;
    int restart_count = 0;
  restart:
    if (restart_count++)
      yield(restart_count);
    bool need_restart = false;

    Page cur_page = buf_mgr_->Pin(root_page_id_);
    uint64_t version_node = ((TreePageHeader) cur_page)->ReadLockOrRestart(need_restart);
    if (need_restart || (((TreePageHeader) cur_page)->page_id_ != root_page_id_)) {
      goto restart;
    }

    // Parent of current node
    Page parent_page = nullptr;
    uint64_t version_parent;

    while (!((TreePageHeader) cur_page)->is_leaf_) {
      if (parent_page) {
        ((TreePageHeader) parent_page)->ReadUnlockOrRestart(version_parent, need_restart);
        if (need_restart) {
          buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
      }
      
      parent_page = cur_page;
      version_parent = version_node;

      uint32_t child = PageReadChildAt(cur_page, PageLowerBound(cur_page, low));
      ((TreePageHeader) parent_page)->CheckOrRestart(version_node, need_restart);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        goto restart;
      }
      cur_page = buf_mgr_->Pin(child);
      version_node = ((TreePageHeader) cur_page)->ReadLockOrRestart(need_restart);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
    }

    if (parent_page) {
      // We have successfully read the cur_page and it is safe to release parent
      ((TreePageHeader) parent_page)->ReadUnlockOrRestart(version_parent, need_restart);
      buf_mgr_->Unpin(((TreePageHeader) parent_page)->page_id_);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        goto restart;
      }
      parent_page = nullptr;
    }

    uint32_t pos = PageLowerBound(cur_page, low) + 1;
    Slice cur_key;
    PageReadKeyAtOffset(cur_page, pos, cur_key);
    while (cur_key.compare(up) <= 0) {
      int count = ((TreePageHeader) cur_page)->item_num_;
      for(unsigned i = pos; i < count; i++){
        PageReadKeyAtOffset(cur_page, i, cur_key);
        if (cur_key.compare(up) > 0) break;
        Slice val;
        PageReadValAt(cur_page, i, val);
        results.push_back(std::make_pair(Slice(NewPivot(cur_key), cur_key.size()),
                            Slice(NewPivot(val), val.size())));
      }
      uint32_t right_sibling = ((TreePageHeader) cur_page)->right_;
      Page sibling_page = right_sibling == 0 ? nullptr : buf_mgr_->Pin(right_sibling);
      
      if (cur_key.compare(up) > 0 || !sibling_page) {
        ((TreePageHeader) cur_page)->ReadUnlockOrRestart(version_node, need_restart);
        if (need_restart) {
          if (sibling_page)
            buf_mgr_->Unpin(((TreePageHeader) sibling_page)->page_id_);
          buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
          goto restart;
        }
        if (sibling_page)
          buf_mgr_->Unpin(((TreePageHeader) sibling_page)->page_id_);
        buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
        return new ScanIterator(results);
      }

      // Move to the sibling page
      uint64_t version_sibling = ((TreePageHeader) sibling_page)->ReadLockOrRestart(need_restart);
      
      ((TreePageHeader) cur_page)->ReadUnlockOrRestart(version_node, need_restart);
      buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
      if (need_restart) {
        buf_mgr_->Unpin(((TreePageHeader) sibling_page)->page_id_);
        goto restart;
      }
      cur_page = sibling_page;
      version_node = version_sibling;
      pos = 0;
    }
    buf_mgr_->Unpin(((TreePageHeader) cur_page)->page_id_);
    return new ScanIterator(results);
  }

  uint32_t RootPageId() { return root_page_id_; }

  uint32_t TreeHeight() { return tree_height_; }

  // void SetPrintPage() { print_page_ = true; }
  // void UnsetPrintPage() { print_page_ = false; }

 private:
  class ScanIterator : public Iterator {
   public:

    ScanIterator(std::vector<KVType>& results)
    : results_(std::move(results)) {}

    ~ScanIterator() {}

    void Seek(const Slice& target) { ret_ptr_ = 0; }
    void SeekToFirst() {}
    void SeekToLast() {}
    void Prev() {}
    void Next() { ret_ptr_++; }
    bool Valid() const { return ret_ptr_ < results_.size(); }
    Slice key() const { return results_[ret_ptr_].first; }
    Slice value() const { return results_[ret_ptr_].second; }
    Status status() const { return Status::OK(); }

   private:
    // To save space, we only store the first letter of the value
    std::vector<KVType> results_;
    int ret_ptr_;
  };

  char* NewPivot(Slice key) {
    size_t allocs = key.size();
    char* pivot = arena_wrapper_.Allocate(allocs);
    memcpy(pivot, key.data(), allocs);
    return pivot;
  }

  void yield(int count) {
    if (count>3)
      sched_yield();
    else
      _mm_pause();
  }

  void MakeRoot(Slice& sep, uint32_t root_sibling) {
    uint32_t new_id = buf_mgr_->AllocateRoot(root_page_id_);
    Page root = buf_mgr_->Pin(root_page_id_);
    Page l = buf_mgr_->Pin(new_id);
    Page r = buf_mgr_->Pin(root_sibling);

    if (((TreePageHeader) l)->is_leaf_)
      BuildSiblingConn(new_id, root_sibling);
    buf_mgr_->Unpin(new_id);
    buf_mgr_->Unpin(root_sibling);
    PageInsertFirstPivotPair(root, sep, new_id, root_sibling);
    tree_height_++;
  }

  // These pages are already pinned by the caller
  void BuildSiblingConn(uint32_t left, uint32_t right) {
    if (left == 0) {
      Page right_p = buf_mgr_->Lookup(right);
      ((TreePageHeader) right_p)->left_ = 0;
      return;
    }
    if (right == 0) {
      Page left_p = buf_mgr_->Lookup(left);
      ((TreePageHeader) left_p)->right_ = 0;
      return;
    }
    Page left_p = buf_mgr_->Lookup(left);
    Page right_p = buf_mgr_->Lookup(right);
    ((TreePageHeader) right_p)->right_ = ((TreePageHeader) left_p)->right_;
    ((TreePageHeader) left_p)->right_ = right;
    ((TreePageHeader) right_p)->left_ = left;
  }

  // Page is already pinned
  uint32_t SplitInnerNode(uint32_t pg_id, Slice& sep) {
    Page page = buf_mgr_->Lookup(pg_id);
    assert(((TreePageHeader) page)->is_leaf_ == false);
    assert(((TreePageHeader) page)->item_num_ > 0);

    uint32_t child_id = buf_mgr_->Allocate();
    Page child = buf_mgr_->Pin(child_id);
    PageInit(child, child_id);
    ((TreePageHeader) child)->is_leaf_ = false;

    Slice s;
    GetInnerPageSplitSep(page, s);
    sep = Slice(NewPivot(s), s.size());
    InnerPageSplit(page, child);    

    buf_mgr_->Unpin(child_id);
    return child_id;
  }

  // Page is already pinned
  uint32_t SplitLeafNode(uint32_t pg_id, Slice& sep) {
    Page page = buf_mgr_->Lookup(pg_id);
    assert(((TreePageHeader) page)->is_leaf_ == true);
    assert(((TreePageHeader) page)->item_num_ > 0);

    uint32_t child_id = buf_mgr_->Allocate();
    Page child = buf_mgr_->Pin(child_id);
    PageInit(child, child_id);
    ((TreePageHeader) child)->is_leaf_ = true;

    Slice s;
    GetLeafPageSplitSep(page, s);
    sep = Slice(NewPivot(s), s.size());
    LeafPageSplit(page, child);    

    BuildSiblingConn(pg_id, child_id);
    buf_mgr_->Unpin(child_id);
    return child_id;
  }
                              
  // void PrintNode(uint32_t page_id, int level);

  // void PrintNodeStat(uint32_t page_id, int level);

  uint32_t root_page_id_;

  uint32_t tree_height_;

  // We can use bytewise comparator here directly however in order to
  // be compatible with tree of LSMTs in the future.
  const InternalKeyComparator internal_comparator_;

  BufferManager* buf_mgr_;

  ArenaWrapper arena_wrapper_;

  bool print_page_ = false;

};

} // namespace WOT_NAMESPACE

#endif // BTREE_OLC_WOT_NAMESPACE_H_