#ifndef YCSBC_C_HOTSPOT_GENERATOR_H_
#define YCSBC_C_HOTSPOT_GENERATOR_H_

#include "generator.h"

#include <atomic>
#include <mutex>
#include <random>

namespace ycsbc {

/**
 * Quoted from YCSB HotspotIntegerGenerator.java:
 * Generate integers resembling a hotspot distribution where x% of operations
 * access y% of data items. The parameters specify the bounds for the numbers,
 * the percentage of the of the interval which comprises the hot set and
 * the percentage of operations that access the hot set. Numbers of the hot set are
 * always smaller than any number in the cold set. Elements from the hot set and
 * the cold set are chose using a uniform distribution.
 *
 */

class HotspotGenerator : public Generator<uint64_t> {
 public:
  // Both min and max are inclusive
  HotspotGenerator(uint64_t min, uint64_t max, uint64_t seed,
                  double hot_data_percent=0.1) 
  : dist_(min, max),
    hot_op_dist_(0.0, 1.0),
    global_min_(min),
    global_max_(max),
    hot_data_percent_(hot_data_percent),
    last_int_(min),
    seed_(seed) {
      generator_.seed(seed);
      hot_interval_ = (max - min + 1) * hot_data_percent_;
      cold_interval_ = (max - min + 1) - hot_interval_;
  }
  
  uint64_t Next();
  uint64_t Last();
  uint64_t Maxdata() { return global_max_; }
  void ResetSeed() { generator_.seed(seed_ + 1); }
  void SetHotopPercent(double hot_op_percent) { hot_op_percent_ = hot_op_percent; }
  
 private:
  std::mt19937_64 generator_;
  std::uniform_int_distribution<uint64_t> dist_;
  std::uniform_real_distribution<double> hot_op_dist_;
  uint64_t global_min_;
  uint64_t global_max_;
  uint64_t hot_interval_;
  uint64_t cold_interval_;
  double hot_data_percent_;
  double hot_op_percent_;
  uint64_t last_int_;
  std::mutex mutex_;
  uint64_t seed_;
};

inline uint64_t HotspotGenerator::Next() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (hot_op_percent_ == 1.0) {
    // Operation is always within hotspot
    last_int_ = global_min_ + dist_(generator_) % hot_interval_;
  } else if (hot_op_percent_ == -1.0) {
    // Operation is uniformly distributed over the entire range
    last_int_ = global_min_ + dist_(generator_) % (global_max_ - global_min_ + 1);
  } else {
    double hot = hot_op_dist_(generator_);
    if (hot < hot_op_percent_) {
      // Choose a value from the hot set.
      last_int_ = global_min_ + dist_(generator_) % hot_interval_;
    } else {
      // Choose a value from the cold set.
      last_int_ = global_min_ + hot_interval_ + dist_(generator_) % cold_interval_;
    }
  }
  return last_int_;
}

inline uint64_t HotspotGenerator::Last() {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_int_;
}

} // namespace ycsbc

#endif // YCSBC_C_HOTSPOT_GENERATOR_H_