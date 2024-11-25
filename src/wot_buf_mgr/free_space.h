#ifndef FREE_SPACE_WOT_NAMESPACE_H_
#define FREE_SPACE_WOT_NAMESPACE_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include "tree_page.h"

namespace WOT_NAMESPACE {

class TreeBufferManager;

class TreeFile {
 public:
  TreeFile(const std::string& filename)
  : page_num_(0) {
    tree_file_ = std::fopen(filename.c_str(), "w+" );
    if (!tree_file_) {
      std::perror("Error: file not open properly");
      std::abort();
    }
  }

  ~TreeFile() { std::fclose(tree_file_); }

  // Page AllocateNewPage();
  uint32_t GetNewPageid();
  uint32_t GetPageid() { return page_num_; }

  void Delete(uint32_t pg_id);

  void WritePage(uint32_t pg_id, Page page);

  void GetPage(uint32_t pg_id, void* buf);

 private:
  std::FILE* tree_file_;

  uint32_t page_num_;
};


} // namespace WOT_NAMESPACE

#endif // FREE_SPACE_WOT_NAMESPACE_H_