#include <iostream>
#include <queue>
#include <random>
#include "file.h"

using namespace WOT_NAMESPACE;

void test_file_rw() {
  std::string fn = "test_file";

  {
    std::map<std::string, std::string> data;
    for (int i = 0; i < 10; i++) {
      data.insert({std::to_string(rand() % 100), std::to_string(rand() % 500)});
    }

    FileWrapper fw(fn, &data);
    fw.WriteFile();
    data.clear();
  }
  
  {
    std::map<std::string, std::string> new_data;
    FileWrapper fr(fn, &new_data);
    fr.ReadFile();
    for (const auto& kv : new_data) {
      std::cout << kv.first << "=>" << kv.second << "\n";
    }
    new_data.clear();
  }
}

void create_file(std::string fn) {
  std::map<std::string, std::string> data;
  for (int i = 0; i < 10; i++) {
    data.insert({std::to_string(rand() % 100), std::to_string(rand() % 500)});
  }
  FileWrapper fw(fn, &data);
  fw.WriteFile();
  data.clear();
}

void test_file_compaction_no_guard() {
  std::vector<std::string> fn = {"c1", "c2", "c3"};
  for (auto const& fn : fn) {
    create_file(fn);
  }

  {
    // test no guards
    Compaction c(&fn, nullptr, 15, "oc");
    c.Prepare();
    while (true) {
      std::string guard;
      std::vector<FileDescriptor*> out_fs;
      auto s = c.CompactFiles(&guard, &out_fs);
      if (s == FileState::kNotExist) {
        break;
      }
      std::cout << "Guard is " << guard << std::endl;
      for (auto const& fn : out_fs) {
        std::cout << "output file name is " << fn->file_path_ << "\t"
                  << "smallest: " << fn->smallest_ << ";"
                  << "largest: " << fn->largest_ << "\n";
      }
    }
    
  }
}

void test_file_compaction_0_guard() {
  std::vector<std::string> fn = {"c1", "c2", "c3"};
  for (auto const& fn : fn) {
    create_file(fn);
  }

  {
    // test with one guard
    std::queue<std::string> gs;
    gs.push("0");
    Compaction c(&fn, &gs, 15, "oc");
    c.Prepare();
    while (true) {
      std::string guard;
      std::vector<FileDescriptor*> out_fs;
      auto s = c.CompactFiles(&guard, &out_fs);
      if (s == FileState::kNotExist) {
        break;
      }
      std::cout << "Guard is " << guard << std::endl;
      for (auto const& fn : out_fs) {
        std::cout << "output file name is " << fn->file_path_ << "\t"
                  << "smallest: " << fn->smallest_ << ";"
                  << "largest: " << fn->largest_ << "\n";
      }
    }
    
  }
}

void test_file_compaction_1_guard() {
  std::vector<std::string> fn = {"c1", "c2", "c3"};
  for (auto const& fn : fn) {
    create_file(fn);
  }

  {
    // test with two guards
    std::queue<std::string> gs;
    gs.push("0");
    gs.push("30");
    Compaction c(&fn, &gs, 15, "oc");
    c.Prepare();
    while (true) {
      std::string guard;
      std::vector<FileDescriptor*> out_fs;
      auto s = c.CompactFiles(&guard, &out_fs);
      if (s == FileState::kNotExist) {
        break;
      }
      std::cout << "Guard is " << guard << std::endl;
      for (auto const& fn : out_fs) {
        std::cout << "output file name is " << fn->file_path_ << "\t"
                  << "smallest: " << fn->smallest_ << ";"
                  << "largest: " << fn->largest_ << "\n";
      }
    }
  }
}

int main() {
  srand(1);
  // test_file_rw();
  test_file_compaction_no_guard();
  test_file_compaction_0_guard();
  test_file_compaction_1_guard();

  return 0;
}