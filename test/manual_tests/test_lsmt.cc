#include <iostream>
#include <random>
#include "lsmt.h"

using namespace WOT_NAMESPACE;

FileDescriptor create_file(std::string fn) {
  std::map<std::string, std::string> data;
  int fs = 2;
  for (int i = 0; i < fs; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string val = std::to_string(rand() % 500);
    data[key] = val;
    std::cout << key << "=>" << val << " | ";
  }
  std::cout << "\n";
  FileWrapper fw(fn, &data);
  fw.WriteFile();
  return FileDescriptor(fn, data.begin()->first, data.rbegin()->first, fs);
}

void test_construct() {
  std::vector<std::string> fn = {"c1", "c2", "c3"};
  std::vector<FileDescriptor> fd_vec;
  for (auto const& fn : fn) {
    FileDescriptor fd = create_file(fn);
    fd_vec.push_back(fd);
  }

  VanillaTree vt(4, 4, 10, 40, "vanilla_tree");
  for (const auto& fd : fd_vec) {
    vt.InsertFile(fd);
  }
  vt.Print();
}

void test_level_flush(int num) {
  std::vector<FileDescriptor> fd_vec;
  for (int i = 0; i < num; i++) {
    std::string fn = "c" + std::to_string(i);
    FileDescriptor fd = create_file(fn);
    fd_vec.push_back(fd);
  }

  VanillaTree vt(3, 4, 10, 30, "vanilla_tree");
  for (const auto& fd : fd_vec) {
    vt.InsertFile(fd);
  }
  vt.Print();
}

void test_query(int num) {
  std::vector<FileDescriptor> fd_vec;
  for (int i = 0; i < num; i++) {
    std::string fn = "c" + std::to_string(i);
    FileDescriptor fd = create_file(fn);
    fd_vec.push_back(fd);
  }

  VanillaTree vt(3, 4, 8, 24, "vanilla_tree");
  for (const auto& fd : fd_vec) {
    vt.InsertFile(fd);
  }
  vt.Print();
  srand(1);
  for (int i = 0; i < 10; i++) {
    std::string val = "";
    std::string key = std::to_string(rand() % 100);
    std::cout << key; 
    if (vt.Query(key, &val) == OpState::kOk) {
      std::cout << "=>" << val;
    }
    std::cout << "\n";
  }
}

void test_scan(int num) {
  std::vector<FileDescriptor> fd_vec;
  for (int i = 0; i < num; i++) {
    std::string fn = "c" + std::to_string(i);
    FileDescriptor fd = create_file(fn);
    fd_vec.push_back(fd);
  }

  VanillaTree vt(3, 4, 8, 24, "vanilla_tree");
  for (const auto& fd : fd_vec) {
    vt.InsertFile(fd);
  }
  vt.Print();
  srand(1);
  for (int i = 0; i < 1; i++) {
    std::string low = "7";
    std::string up = "8";
    UnsortedIterator* un_iter = vt.NewUnsortedIterator(low, up);
    un_iter->Seek();
    for (; un_iter->Valid(); un_iter->Next()) {
      std::cout << un_iter->key() << "=>" << un_iter->value() << std::endl;
    }
  }
}

void test_guarded_compaction(int num) {
  std::vector<FileDescriptor> fd_vec;
  for (int i = 0; i < num; i++) {
    std::string fn = "c" + std::to_string(i);
    FileDescriptor fd = create_file(fn);
    fd_vec.push_back(fd);
  }

  VanillaTree vt(2, 3, 2, 30, "vanilla_tree");
  vt.AddGuard("0");
  vt.AddGuard("40");
  vt.AddGuard("100");
  for (const auto& fd : fd_vec) {
    vt.InsertFile(fd);
  }
  
  vt.Print();
}

void test_leveled_compaction(int num) {
  std::vector<FileDescriptor> fd_vec;
  for (int i = 0; i < num; i++) {
    std::string fn = "c" + std::to_string(i);
    FileDescriptor fd = create_file(fn);
    fd_vec.push_back(fd);
  }

  VanillaTree vt(2, 3, 2, 6, "vanilla_tree");
  for (const auto& fd : fd_vec) {
    vt.InsertFile(fd);
  }
  
  vt.Print();
}

int main() {
  srand(1);
  // test_construct();
  // Test level-0 flush
  // test_level_flush(11);
  // Test level-1 flush
  // test_level_flush(12);
  // test_query(12);
  // test_scan(2);
  // test_guarded_compaction(6);
  test_leveled_compaction(6);
  return 0;
}