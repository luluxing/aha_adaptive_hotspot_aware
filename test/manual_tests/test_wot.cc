#include <iostream>
#include <set>
// #include "rocksdb/db.h"
// #include "rocksdb/options.h"
// #include "rocksdb/sst_file_reader.h"
// #include "rocksdb/sst_file_writer.h"
// #include "rocksdb/status.h"
#include "wot_adapt.h"

using namespace WOT_NAMESPACE;

void test_insert() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  // Insert some key-value pairs
  for (int i = 0; i < std::stoi(options["test_size"]); i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    global.insert(key);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  std::cout << global.size() << std::endl;
}

void test_query() {
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  // Insert some key-value pairs
  for (int i = 0; i < std::stoi(options["test_size"]); i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    s = b.Insert(key, value);
    std::cout << key << "=>" << value << std::endl;
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  srand(1);
  for (int i = 0; i < 10; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value;
    s = b.Query(key, &value);
    if (s == OpState::kNotFound) {
      std::cout << "Key " << key << " not found\n";
      continue;
    }
    std::cout << "query " << key << "=>" << value << std::endl;
  }
}

void test_scan() {
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  // Insert some key-value pairs
  for (int i = 0; i < std::stoi(options["test_size"]); i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    s = b.Insert(key, value);
    std::cout << key << "=>" << value << std::endl;
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  srand(1);
  for (int i = 0; i < 20; i++) {
    int ki1 = rand() % 100;
    int r1 = rand() % 20;
    std::string k1 = std::to_string(ki1);
    std::string k2 = std::to_string(ki1 + r1);
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1, k2);
    iter->Seek();
    std::cout << "Range: [" << k1 << ", " << k2 << "]\n";
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value() << "\n";
    }
  }
}

void test_adapt() {
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num / 2; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  // b.IncreaseNodeSize();
  for (int i = 0; i < num / 2; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
}

void test_write_file_by_range() {
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
}

void test_leveled() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % 1000);
    std::string value = std::string(2, 'a' + (i % 26));
    global.insert(key);
    // std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  std::cout << global.size() << std::endl;
}

void test_adapt_to_read() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  BplusTree b(&options);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    global.insert(key);
    // std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  std::cout << "Total num keys: " << global.size() << std::endl;
  // b.Print();
  b.AdaptToRead();
  std::cout << "===============\n";
  srand(1);
  for (int i = 0; i < std::stoi(options["scan_size"]); i++) {
    int ki1 = rand() % 100;
    int r1 = rand() % 20;
    std::string k1 = std::to_string(ki1);
    std::string k2 = std::to_string(ki1 + r1);
    if (k1 > k2) {
      continue;
    }
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1, k2);
    iter->Seek();
    std::cout << "Range: [" << k1 << ", " << k2 << "]\n";
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value() << "\n";
    }
    b.Print();
    std::cout << "===============\n";
  }
}

int FLAGS_key_prefix = 0;

class KeyBuffer {
 public:
  KeyBuffer() {
    assert(FLAGS_key_prefix < sizeof(buffer_));
    memset(buffer_, 'a', FLAGS_key_prefix);
  }
  KeyBuffer& operator=(KeyBuffer& other) = delete;
  KeyBuffer(KeyBuffer& other) = delete;

  void Set(int k) {
    std::snprintf(buffer_ + FLAGS_key_prefix,
                  sizeof(buffer_) - FLAGS_key_prefix, "%016d", k);
  }

  Slice slice() const { return Slice(buffer_, FLAGS_key_prefix + 16); }

 private:
  char buffer_[1024];
};

void test_pure_lsmt() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  bool is_lsmt = options["is_lsmt"] == "1";
  BplusTree b(&options, is_lsmt);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  KeyBuffer key;
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    int k = rand() % 1000;
    key.Set(k);
    std::string v = std::string(100, 'a' + (i % 26));
    global.insert(key.slice().ToString());
    // std::cout << key.slice().ToString() << "=>" << std::endl;
    s = b.Insert(key.slice().ToString(), Slice(v).ToString());
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  srand(1);
  KeyBuffer k1, k2;
  for (int i = 0; i < 1; i++) {
    int ki1 = rand() % 1000;
    int r1 = rand() % 20;
    
    k1.Set(ki1);
    // std::string k1 = std::to_string(ki1);
    // std::string k2 = std::to_string(ki1 + r1);
    k2.Set(ki1 + r1);
    if (k1.slice().ToString() >= k2.slice().ToString()) continue;
    std::cout << "Range: [" << k1.slice().ToString() << ", " << k2.slice().ToString()<< "]\n";
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1.slice(),
                                                           k2.slice());
    iter->Seek();
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value().substr(0, 2) << "\n";
    }
    // b.Print();
    std::cout << "===============\n";
  }
  std::cout << global.size() << std::endl;
}

void test_pure_lsmt_to_wot() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  bool is_lsmt = options["is_lsmt"] == "1";
  BplusTree b(&options, is_lsmt);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    global.insert(key);
    // std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  std::cout << "++++++++++++++\n";
  b.PrintStat();
  b.AdaptToRead();
  std::cout << "===============\n";
  srand(1);
  for (int i = 0; i < 6; i++) {
    int ki1 = rand() % 100;
    int r1 = rand() % 20;
    std::string k1 = std::to_string(ki1);
    std::string k2 = std::to_string(ki1 + r1);
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1, k2);
    iter->Seek();
    std::cout << "Range: [" << k1 << ", " << k2 << "]\n";
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value() << "\n";
    }
    b.Print();
    // b.PrintStat();
    std::cout << "===============\n";
  }
  b.Print();
  // std::cout << "++++++++++++++\n";
  // b.PrintStat();
}

void test_pure_lsmt_to_wot_freq() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  bool is_lsmt = options["is_lsmt"] == "1";
  BplusTree b(&options, is_lsmt);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    global.insert(key);
    // std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  b.AdaptToRead();
  std::cout << "===============\n";
  for (int i = 0; i < 1; i++) {
    int ki1 = 83;
    int r1 = 6;
    std::string k1 = std::to_string(ki1);
    std::string k2 = std::to_string(ki1 + r1);
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1, k2);
    iter->Seek();
    std::cout << "Range: [" << k1 << ", " << k2 << "]\n";
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value() << "\n";
    }
    b.Print();
    // b.PrintStat();
    std::cout << "===============\n";
  }
  for (int i = 0; i < 32; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    global.insert(key);
    // std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  std::cout << "===============\n";
  srand(1);
  for (int i = 0; i < 3; i++) {
    int ki1 = rand() % 100;
    int r1 = rand() % 20;
    std::string k1 = std::to_string(ki1);
    std::string k2 = std::to_string(ki1 + r1);
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1, k2);
    iter->Seek();
    std::cout << "Range: [" << k1 << ", " << k2 << "]\n";
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value() << "\n";
    }
    b.Print();
    // b.PrintStat();
    std::cout << "===============\n";
  }
}

void test_pure_btree() {
  std::set<std::string> global;
  std::map<std::string, std::string> options;
  LoadOptions("../src/options.spec", &options);
  bool is_lsmt = options["is_lsmt"] == "1";
  bool is_btree = options["buffer_size"] == "0";
  BplusTree b(&options, is_lsmt, is_btree);
  srand(1);
  OpState s;
  int num = std::stoi(options["test_size"]);
  // Insert some key-value pairs
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % 100);
    std::string value = std::string(2, 'a' + (i % 26));
    global.insert(key);
    std::cout << key << "=>" << value << std::endl;
    s = b.Insert(key, value);
    if (s != OpState::kOk) {
      std::cerr << "Failed to insert KV pair\n";
      return;
    }
  }
  b.Print();
  srand(1);
  for (int i = 0; i < 1; i++) {
    int ki1 = 10;// rand() % 100;
    int r1 = 89;// rand() % 20;
    std::string k1 = std::to_string(ki1);
    std::string k2 = std::to_string(ki1 + r1);
    UnsortedTreeIterator* iter = b.NewUnsortedTreeIterator(k1, k2);
    iter->Seek();
    std::cout << "Range: [" << k1 << ", " << k2 << "]\n";
    for (; iter->Valid(); iter->Next()) {
      std::cout << "\t" << iter->key() << "->" << iter->value() << "\n";
    }
    // b.Print();
    std::cout << "===============\n";
  }
  std::cout << global.size() << std::endl;
}

int main(int argc, char * argv[]) {
  // test_insert();
  // test_query();
  // test_scan();
  // test_adapt();
  // test_write_file_by_range();
  // test_leveled();
  // test_adapt_to_read();
  test_pure_lsmt();
  // test_pure_lsmt_to_wot();
  // test_pure_lsmt_to_wot_freq();
  // test_pure_btree();

  return 0;
}