#include "wot_tree_node_refactor_.h"
#include "wot_btree_refactor_.h"
#include "leveldb/builder.h"
#include "leveldb/table/merger.h"
#include "wot_buf_mgr/buffer_pool_helper.h"
#include "util/utils.h"
#include <iostream>

namespace WOT_NAMESPACE {

Node::Node(BplusTree* tree, bool is_leaf, int level, Env* env, const std::string& dbname,
           std::atomic<uint64_t>& flush_id, const Options* opt, const InternalKeyComparator& icmp,
           TableCache* table_cache, LSMTStatus stat, int leaf_limit, bool allow_single)
    : tree_(tree),
      icmp_(&icmp),
      flush_id_(flush_id),
      node_lsmt_(new LevelDBLSMT(env, dbname, flush_id, opt, icmp, table_cache, 
                                 level, stat, leaf_limit)),
      lsmt4compact_(nullptr),
      extra_page_(nullptr) {
}

Node::Node(BplusTree* tree, bool is_leaf, std::atomic<uint64_t>& flush_id,
           const InternalKeyComparator& icmp)
    : tree_(tree),
      icmp_(&icmp),
      flush_id_(flush_id),
      node_lsmt_(nullptr),
      lsmt4compact_(nullptr),
      extra_page_(nullptr) {
}

Node::~Node() {
    if (node_lsmt_ != nullptr)
        delete node_lsmt_;
    if (lsmt4compact_ != nullptr)
        delete lsmt4compact_;
    if (extra_page_ != nullptr)
        free(extra_page_);
}

void Node::NodePrint(int level) {
    if (level == 1) {
        std::cout << " Node#" << pg_id_ << "::";
        Page page = tree_->GetBufferManager()->Pin(pg_id_);
        if (((TreePageHeader) page)->is_leaf_) {
            std::string leaf_cato = "large-leaf";
            if (node_lsmt_->GetLSMTStatus() == LSMTStatus::kSmallLeaf) {
                leaf_cato = "small-leaf";
            }
            std::cout << leaf_cato << ":";
        }
        PagePrint(page);
        tree_->GetBufferManager()->Unpin(pg_id_);
        if (node_lsmt_ != nullptr) {
            node_lsmt_->Print();
            std::cout << "\n";
        }
    } else {
        Page page = tree_->GetBufferManager()->Pin(pg_id_);
        uint32_t item_num = ((TreePageHeader) page)->item_num_;
        for (int i = 0; i < item_num; i++) {
            ReadTbb a;
            if (tree_->GetNodeTable().find(a, PageReadChildAtOffset(page, i))) {
                a->second->NodePrint(level - 1);
            }
            a.release();
        }
        tree_->GetBufferManager()->Unpin(pg_id_);
    }
}

void Node::NodePrintStat(int level) {
    if (level == 1) {
        std::cout << " Node#" << pg_id_ << "::";
        Page page = tree_->GetBufferManager()->Pin(pg_id_);
        PagePrintStat(page);
        tree_->GetBufferManager()->Unpin(pg_id_);
        if (node_lsmt_ != nullptr) {
            node_lsmt_->PrintStat();
        }
    } else {
        Page page = tree_->GetBufferManager()->Pin(pg_id_);
        uint32_t item_num = ((TreePageHeader) page)->item_num_;
        for (int i = 0; i < item_num; i++) {
            ReadTbb a;
            if (tree_->GetNodeTable().find(a, PageReadChildAtOffset(page, i))) {
                a->second->NodePrintStat(level - 1);
            }
            a.release();
        }
        tree_->GetBufferManager()->Unpin(pg_id_);
    }
}

SlicePageMap Node::MaybeSplitOrFlush() {
    std::vector<SlicePageMap> new_pivots;
    std::vector<std::pair<Slice, uint32_t>> old_pivots;
    Page page = tree_->GetBufferManager()->Pin(pg_id_);
    bool leaf_page = ((TreePageHeader) page)->is_leaf_;
    
    if (leaf_page) {
        node_overflow_ = true;
    } else {
        CompactAndFlushToChildren(&new_pivots, &old_pivots);
        UpdatePivots(&new_pivots, &old_pivots);
    }
    
    SlicePageMap result;
    if (node_overflow_) {
        result = SplitNodeAndLSMT(&new_pivots, &old_pivots);
    }
    
    new_pivots.clear();
    old_pivots.clear();
    
    if (leaf_page) {
        tree_->GetBufferManager()->UnpinAndRelease(pg_id_);
    } else {
        tree_->GetBufferManager()->Unpin(pg_id_);
    }
    
    return result;
}

OpState Node::BufferCompactTopLevel(Page page) {
    if (lsmt4compact_ == nullptr) {
        lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
    }
    if (!((TreePageHeader) page)->is_leaf_ && tree_->GetBTreeState()->has_buffer_page) {
        uint32_t new_level = tree_->GetConfig().node_lsmt_level_limit > 1 ? 
                            tree_->GetConfig().node_lsmt_level_limit - 1 : 1;
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

OpState Node::BufferAddFiles(std::vector<std::shared_ptr<FileMetaData>>* files, Page p) {
    if (lsmt4compact_ == nullptr) {
        lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
    }
    OpState s = lsmt4compact_->AddFiles(0, files, p);
    return s;
}

void Node::GetObsoleteFilesAndInstallNewBuffer(std::set<uint64_t>& files) {
    if (lsmt4compact_ == nullptr) return;
    delete node_lsmt_;
    node_lsmt_ = lsmt4compact_;
    node_lsmt_->GetObsoleteFiles(files);
    lsmt4compact_ = nullptr;
    InstallNewPage();
    if (!tmp_min_.empty()) {
        Page self_page = tree_->GetBufferManager()->Pin(pg_id_);
        MayUpdateMinPivot(self_page, Slice(tmp_min_));
        tmp_min_.clear();
        tree_->GetBufferManager()->Unpin(pg_id_);
    }
}

void Node::InstallNewBuffer() {
    if (lsmt4compact_ == nullptr) return;
    delete node_lsmt_;
    node_lsmt_ = lsmt4compact_;
    node_lsmt_->RemoveObsoleteFiles();
    lsmt4compact_ = nullptr;
    InstallNewPage();
    if (!tmp_min_.empty()) {
        Page self_page = tree_->GetBufferManager()->Pin(pg_id_);
        MayUpdateMinPivot(self_page, Slice(tmp_min_));
        tmp_min_.clear();
        tree_->GetBufferManager()->Unpin(pg_id_);
    }
}

void Node::InstallNewPage() {
    if (extra_page_ != nullptr) {
        assert(tmp_min_.empty());
        Page self_page = tree_->GetBufferManager()->Pin(pg_id_);
        ((TreePageHeader) extra_page_)->page_id_ = pg_id_;
        ((TreePageHeader) extra_page_)->is_leaf_ = ((TreePageHeader) self_page)->is_leaf_;
        ((TreePageHeader) extra_page_)->is_dirty_ = true;
        ((TreePageHeader) extra_page_)->left_ = ((TreePageHeader) self_page)->left_;
        ((TreePageHeader) extra_page_)->right_ = ((TreePageHeader) self_page)->right_;
        memcpy(self_page, extra_page_, PAGE_SIZE);
        free(extra_page_);
        extra_page_ = nullptr;
        tree_->GetBufferManager()->Unpin(pg_id_);
    }
}

void Node::MayUpdateMinPivot(Page p, const Slice& k) {
    if (p != nullptr && ((TreePageHeader) p)->item_num_ > 0) {
        const Slice& minpivot = PageReadPivotAtOffset(p, 0);
        if (icmp_->user_comparator()->Compare(k, minpivot) < 0) {
            PageUpdateMinPivot(p, k);
        }
    }
}

void Node::SetBufferStatus(LSMTStatus status) {
    if (lsmt4compact_ != nullptr) {
        lsmt4compact_->SetLSMTStatus(status);
    } else if (node_lsmt_ != nullptr) {
        node_lsmt_->SetLSMTStatus(status);
    }
}

void Node::SetBufferLevelLimit(int level) {
    if (lsmt4compact_ != nullptr) {
        lsmt4compact_->SetLevelLimit(level);
    } else if (node_lsmt_ != nullptr) {
        node_lsmt_->SetLevelLimit(level);
    }
}

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

Status Node::BufferCompactTree(int* sn, std::vector<std::shared_ptr<FileMetaData>>* outfs) {
    if (lsmt4compact_ == nullptr) {
        lsmt4compact_ = new LevelDBLSMT(*node_lsmt_);
    }
    Status s = lsmt4compact_->CompactTree(sn, outfs);
    return s;
}

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

// Implementation of more complex methods will continue...
// For brevity, I'm showing the key structure and some methods

} // namespace WOT_NAMESPACE