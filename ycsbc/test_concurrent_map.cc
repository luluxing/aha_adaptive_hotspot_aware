#include <oneapi/tbb/concurrent_hash_map.h>
#include <atomic>
#include <thread>


class MyObj {
 public:
  MyObj()
  : value(0) {}

  void Increment() {
    value.fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t GetValue() const {
    return value.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t> value;
};


typedef oneapi::tbb::concurrent_hash_map<uint32_t, MyObj*> TbbTable;
typedef TbbTable::const_accessor ReadTbb;
typedef TbbTable::accessor WriteTbb;

TbbTable tbb_table;

void InsertToTable(uint32_t key, uint32_t value) {
  WriteTbb write_tbb;
  tbb_table.insert(write_tbb, key);
  auto obj = new MyObj();
  write_tbb->second = obj;
}

void ReadKey(uint32_t key) {
  for (int i = 0; i < 10000; ++i) {
    ReadTbb read_tbb;
    tbb_table.find(read_tbb, key);
    auto obj = read_tbb->second;
    read_tbb.release();
    obj->Increment();
  }
}

int main(const int argc, const char *argv[]) {
  // Multiple threads attempt to insert into the table.
  // Each thread will increment the value of the key by 1.
  // The final value of the key should be equal to the number of threads.
  int key = 11;
  InsertToTable(key, 0);
  const int num_threads = std::stoi(argv[1]);
  std::thread threads[num_threads];
  for (int i = 0; i < num_threads; ++i) {
    threads[i] = std::thread(ReadKey, key);
  }
  for (int i = 0; i < num_threads; ++i) {
    threads[i].join();
  }

  ReadTbb read_tbb;
  tbb_table.find(read_tbb, key);
  MyObj* obj = read_tbb->second;
  read_tbb.release();
  fprintf(stdout, "Final value: %d\n", obj->GetValue());

}