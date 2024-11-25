// Compare the CPU usage of the following five cases:
// 1. A single thread
// 2. Two threads, one is sleeping, one is in the for loop incrementing
// 3. Two threads, both are in the for loop incrementing
// 4. Two threads, one is waiting on a condition variable, one is in the for loop incrementing
// 5. Two threads, one is spin waiting in a loop, one is in the for loop incrementing

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

uint64_t count = 80000000;

void LongSleepThread() {
  std::this_thread::sleep_for(std::chrono::seconds(10));
}

void ShortSleepThread() {
  std::this_thread::sleep_for(std::chrono::seconds(4));
}

void IncrementThread(std::mutex *mutex, std::condition_variable *cond_var,
                      std::atomic<uint64_t> *value) {
  for (int i = 0; i < count; ++i) {
    value->fetch_add(1, std::memory_order_relaxed);
    if (value->load(std::memory_order_relaxed) == count / 2) {
      fprintf(stdout, "%d\n", value->load(std::memory_order_relaxed));
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::unique_lock<std::mutex> lock(*mutex);
      cond_var->notify_one();
    }
  }
}

void WaitThread(std::mutex *mutex, std::condition_variable *cond_var,
                std::atomic<uint64_t> *value) {
  std::unique_lock<std::mutex> lock(*mutex);
  while (value->load(std::memory_order_relaxed) < count / 2) {
    cond_var->wait(lock);
  }
}

void SpinThread(std::atomic<bool> *spin) {
  while (spin->load(std::memory_order_relaxed)) {
    
  }
}

// void Test1(int num_threads) {
//   std::this_thread::sleep_for(std::chrono::seconds(1));
//   fprintf(stdout, "Test1: single thread\n");
//   auto start = std::chrono::high_resolution_clock::now();
//   std::thread threads[num_threads];
//   std::atomic<uint64_t> value(0);
//   threads[0] = std::thread(IncrementThread, &value);
//   threads[0].join();
//   fprintf(stdout, "%ld\n", value.load(std::memory_order_relaxed));
//   auto end = std::chrono::high_resolution_clock::now();
//   std::chrono::duration<double> elapsed = end - start;
//   fprintf(stdout, "Time: %f\n", elapsed.count());
// }

// void Test2(int num_threads) {
//   fprintf(stdout, "Test2: one thread sleeping long, one thread incrementing\n");
//   std::this_thread::sleep_for(std::chrono::seconds(1));
//   std::thread threads[num_threads];
//   std::atomic<uint64_t> value(0);
//   threads[0] = std::thread(LongSleepThread);
//   for (int i = 1; i < num_threads; ++i) {
//     threads[i] = std::thread(IncrementThread, &value);
//   }
//   for (int i = 0; i < num_threads; ++i) {
//     threads[i].join();
//   }
// }

// void Test22(int num_threads) {
//   fprintf(stdout, "Test2: one thread sleeping short, one thread incrementing\n");
//   std::this_thread::sleep_for(std::chrono::seconds(1));
//   std::thread threads[num_threads];
//   std::atomic<uint64_t> value(0);
//   threads[0] = std::thread(ShortSleepThread);
//   for (int i = 1; i < num_threads; ++i) {
//     threads[i] = std::thread(IncrementThread, &value);
//   }
//   for (int i = 0; i < num_threads; ++i) {
//     threads[i].join();
//   }
// }

// void Test3(int num_threads) {
//   fprintf(stdout, "Test3: two threads incrementing\n");
//   std::this_thread::sleep_for(std::chrono::seconds(1));
//   std::thread threads[num_threads];
//   std::atomic<uint64_t> value(0);
//   for (int i = 0; i < num_threads; ++i) {
//     threads[i] = std::thread(IncrementThread, &value);
//   }
//   for (int i = 0; i < num_threads; ++i) {
//     threads[i].join();
//   }
// }

void Test4(int num_threads) {
  fprintf(stdout, "Test4: one thread waiting until half count, one thread incrementing\n");
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::thread threads[num_threads];
  std::atomic<uint64_t> value(0);
  std::mutex mutex;
  std::condition_variable cond_var;
  threads[0] = std::thread(WaitThread, &mutex, &cond_var, &value);
  for (int i = 1; i < num_threads; ++i) {
    threads[i] = std::thread(IncrementThread, &mutex, &cond_var, &value);
  }
 
  for (int i = 0; i < num_threads; ++i) {
    threads[i].join();
  }
}

// void Test5(int num_threads) {
//   fprintf(stdout, "Test5: one thread spin waiting for 1s, one thread incrementing\n");
//   std::this_thread::sleep_for(std::chrono::seconds(1));
//   std::thread threads[num_threads];
//   std::atomic<uint64_t> value(0);
//   std::atomic<bool> spin(true);
//   threads[0] = std::thread(SpinThread, &spin);
//   for (int i = 1; i < num_threads; ++i) {
//     threads[i] = std::thread(IncrementThread, &value);
//   }
//   std::this_thread::sleep_for(std::chrono::seconds(1));
//   spin.store(false, std::memory_order_relaxed);
//   for (int i = 0; i < num_threads; ++i) {
//     threads[i].join();
//   }
// }

int main(const int argc, const char *argv[]) {
  int num_threads = std::stoi(argv[1]);
  int test_case = std::stoi(argv[2]);
  switch (test_case) {
    case 1:
      // Test1(num_threads);
      break;
    case 2:
      // Test2(num_threads);
      break;
    case 22:
      // Test22(num_threads);
      break;
    case 3:
      // Test3(num_threads);
      break;
    case 4:
      Test4(num_threads);
      break;
    case 5:
      // Test5(num_threads);
      break;
    default:
      std::cout << "Invalid number of threads" << std::endl;
      break;
  }
  return 0;
}