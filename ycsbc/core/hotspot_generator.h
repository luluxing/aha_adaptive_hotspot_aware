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

/*
  Multiple hotspots are supported. The hotspots are disjoint and are uniformly, e.g.,
  if there are 2 hotspots with 2% hot data, the first hotspot is from 0 to 0.1, 
  the second hotspot is from mean to mean + 0.1. The cold data is from 0.1 to mean 
  and from mean + 0.1 to 1.
*/
class HotspotGenerator : public Generator<uint64_t> {
 public:
  // Both min and max are inclusive
  HotspotGenerator(uint64_t min, uint64_t max, uint64_t seed,
                  double hot_data_percent=0.1, int hotspot_num=1) 
  : dist_(min, max),
    hot_op_dist_(0.0, 1.0),
    global_min_(min),
    global_max_(max),
    hot_data_percent_(hot_data_percent),
    hotspot_num_(hotspot_num),
    last_int_(min),
    seed_(seed) {
      generator_.seed(seed);
      hot_len_ = (max - min + 1) * hot_data_percent_;
      cold_len_ = (max - min + 1) - hot_len_;
      hot_interval_ = hot_len_ / hotspot_num;
      cold_interval_ = cold_len_ / hotspot_num;
      hotspot_starts_.resize(hotspot_num_);
      for (int i = 0; i < hotspot_num_; i++) {
        hotspot_starts_[i] = min + i * (hot_interval_ + cold_interval_);
      }
  }
  
  uint64_t Next();
  uint64_t Last();
  uint64_t Maxdata() { return global_max_; }
  void ResetSeed() { generator_.seed(seed_ + 1); }
  void SetHotopPercent(double hot_op_percent) { hot_op_percent_ = hot_op_percent; }
  void ResetHotspotNum(int hotspot_num) {
    hotspot_num_ = hotspot_num;
    hot_interval_ = hot_len_ / hotspot_num;
    cold_interval_ = cold_len_ / hotspot_num;
    hotspot_starts_.clear();
    hotspot_starts_.resize(hotspot_num_);
    for (int i = 0; i < hotspot_num_; i++) {
      hotspot_starts_[i] = global_min_ + i * hot_interval_;
    }
  }
  bool IsHot(uint64_t value);
  bool IsCold(uint64_t value);

  uint64_t GetHotspotStart(int i) { return hotspot_starts_[i]; }
  uint64_t GetHotspotEnd(int i) { return hotspot_starts_[i] + hot_interval_ - 1; }
  
 private:
  uint64_t GetHot();
  uint64_t GetCold();

  std::mt19937_64 generator_;
  std::uniform_int_distribution<uint64_t> dist_;
  std::uniform_real_distribution<double> hot_op_dist_;
  uint64_t global_min_;
  uint64_t global_max_;
  uint64_t hot_interval_;
  uint64_t cold_interval_;
  uint64_t hot_len_;
  uint64_t cold_len_;
  double hot_data_percent_;
  double hot_op_percent_;
  int hotspot_num_;
  std::vector<double> hotspot_starts_;
  uint64_t last_int_;
  std::mutex mutex_;
  uint64_t seed_;
};

inline uint64_t HotspotGenerator::GetHot() {
  // Already locked
  uint64_t num = dist_(generator_) % hot_len_;
  uint64_t quotient = num / hot_interval_;
  uint64_t residual = num % hot_interval_;
  return hotspot_starts_[quotient] + residual;
}

inline uint64_t HotspotGenerator::GetCold() {
  // Already locked
  uint64_t num = dist_(generator_) % cold_len_;
  uint64_t quotient = num / cold_interval_;
  uint64_t residual = num % cold_interval_;
  return hotspot_starts_[quotient] + hot_interval_ + residual;
}

inline uint64_t HotspotGenerator::Next() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (hot_op_percent_ >= 1.0) {
    // Operation is always within hotspot
    last_int_ = GetHot();
  } else if (hot_op_percent_ <= -1.0) {
    // Operation is uniformly distributed over the entire range
    last_int_ = global_min_ + dist_(generator_) % (global_max_ - global_min_ + 1);
  } else if (hot_op_percent_ >= 0.0 && hot_op_percent_ < 1.0) {
    double hot = hot_op_dist_(generator_);
    if (hot < hot_op_percent_) {
      // Choose a value from the hot set.
      last_int_ = GetHot();
    } else {
      // Choose a value from the cold set.
      last_int_ = GetCold();
    }
  }
  return last_int_;
}

inline uint64_t HotspotGenerator::Last() {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_int_;
}

inline bool HotspotGenerator::IsHot(uint64_t value) {
  for (int i = 0; i < hotspot_num_; i++) {
    if (value >= hotspot_starts_[i] && value < hotspot_starts_[i] + hot_interval_) {
      return true;
    }
  }
  return false;
}

inline bool HotspotGenerator::IsCold(uint64_t value) {
  return !IsHot(value);
}

} // namespace ycsbc

#endif // YCSBC_C_HOTSPOT_GENERATOR_H_