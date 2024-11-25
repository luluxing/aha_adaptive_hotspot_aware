
#ifndef YCSB_C_NORMAL_GENERATOR_H_
#define YCSB_C_NORMAL_GENERATOR_H_

#include "generator.h"

#include <atomic>
#include <mutex>
#include <random>

namespace ycsbc {

class NormalGenerator : public Generator<uint64_t> {
 public:
  // Both min and max are inclusive
  NormalGenerator(double mean, double std, uint64_t seed) 
  : dist_(mean, std),
    max_val_(std::numeric_limits<uint64_t>::max()),
    seed_(seed) {
    generator_.seed(seed); // add seed
    // Next();
  }
  
  uint64_t Next();
  uint64_t Last();
  uint64_t Maxdata() { return max_val_; }
  void ResetSeed() { generator_.seed(seed_ + 1); }
  
 private:
  std::mt19937_64 generator_;
  std::normal_distribution<double> dist_;
  uint64_t last_int_;
  std::mutex mutex_;
  uint64_t max_val_;
  uint64_t seed_;
};

inline uint64_t NormalGenerator::Next() {
  std::lock_guard<std::mutex> lock(mutex_);
  double number = dist_(generator_);
  return last_int_ = static_cast<uint64_t>(std::round(number));
}

inline uint64_t NormalGenerator::Last() {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_int_;
}

} // ycsbc

#endif // YCSB_C_UNIFORM_GENERATOR_H_
