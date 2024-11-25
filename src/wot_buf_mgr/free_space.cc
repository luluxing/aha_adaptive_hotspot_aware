#include <iostream>
#include "free_space.h"

namespace WOT_NAMESPACE {

// Page TreeFile::AllocateNewPage() {
//   void* buffer = malloc(PAGE_SIZE);
//   memset(buffer, 0, PAGE_SIZE);
//   ((TreePageHeader) buffer)->page_id_ = page_num_;
//   page_num_++;
//   return (Page) buffer;
// }

uint32_t TreeFile::GetNewPageid() {
  return page_num_++;
}

void TreeFile::Delete(uint32_t pg_id) {
  long offset = (long) pg_id * PAGE_SIZE;
  std::fseek(tree_file_, offset, SEEK_SET);
  void* buffer = malloc(PAGE_SIZE);
  memset(buffer, 0, PAGE_SIZE);
  size_t w = std::fwrite(buffer, sizeof(char), PAGE_SIZE, tree_file_);
  if (w == 0) {
    std::perror("Error: fwrite 0 bytes\n");
    std::abort();
  }
  free(buffer);
}

void TreeFile::WritePage(uint32_t pg_id, Page page) {
  if (!((TreePageHeader) page)->is_dirty_) {
    return;
  }
  ((TreePageHeader) page)->is_dirty_ = false;
  long offset = (long) pg_id * PAGE_SIZE;
  std::fseek(tree_file_, offset, SEEK_SET);
  size_t w = std::fwrite(page, sizeof(char), PAGE_SIZE, tree_file_);
  if (w == 0) {
    std::perror("Error: fwrite 0 bytes\n");
    std::abort();
  }
  // free(page);
  // page.reset();
}

void TreeFile::GetPage(uint32_t pg_id, void* buf) {
  if (pg_id >= page_num_) {
    fprintf(stderr, "Error: file cannot find this page\n");
    std::abort();
    // return nullptr;
  }
  long offset = (long) pg_id * PAGE_SIZE;
  if (std::fseek(tree_file_, offset, SEEK_SET) != 0) {
    std::perror("Error: fseek failed\n");
    std::abort();
  }
  // void* buffer = malloc(PAGE_SIZE);
  // memset(buffer, 0, PAGE_SIZE);
  // std::shared_ptr<char> buf((char*) malloc(PAGE_SIZE), free);
  // memset(buf.get(), 0, PAGE_SIZE);
  if (std::fread(buf, sizeof(char), PAGE_SIZE, tree_file_) != PAGE_SIZE) {
    if (ferror(tree_file_)) {
      fprintf(stderr, "read error\n");
    }
    if (feof(tree_file_)) {
      fprintf(stderr, "reaching end %d, %ld\n", pg_id, page_num_);
    }
    std::perror("Error: fread not one page\n");
    std::abort();
  }
  // return buf;
}

} // namespace WOT_NAMESPACE
