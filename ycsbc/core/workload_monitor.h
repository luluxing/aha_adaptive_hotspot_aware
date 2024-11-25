#ifndef YCSB_C_WORKLOAD_MONITOR_H_
#define YCSB_C_WORKLOAD_MONITOR_H_

#include <random>
#include "core_workload.h"

namespace ycsbc {

// We can sample at a fixed frequency, which can be tuned later.
// We can also sample based on a probability, e.g., 0.3 for 30% queries.
// We make decisions every second, or when we have seen "enough" queries.
// We define a scan heavy workload by observing more than threshold of range queries.
// Same for write heavy with more than threshold of inserts and updates
class WorkloadMonitor {
 public:
  WorkloadMonitor(uint32_t sample_freq=1, double sample_prob=1, uint64_t sample_size=100,
                  double scan_threshold=0.4,
                  double write_threshold=0.6, uint16_t seed=1)
  : sample_freq_(sample_freq),
    sample_prob_(sample_prob),
    sample_size_(sample_size),
    scan_threshold_(scan_threshold),
    // insert_threshold_(insert_threshold),
    write_threshold_(write_threshold) {
    generator_.seed(seed);
  }

  void SetSampleFrequency(uint16_t freq) { sample_freq_ = freq; }
  uint16_t SampleFrequency() { return sample_freq_; }

  bool AddSampleAndMakeDecision(Operation op) {
    if (!AddSample(op)) {
      return false;
    }
    switch (op) {
      case SCAN:
        scan_num_++;
        break;
      case UPDATE:
        update_num_++;
        break;
      case INSERT:
        insert_num_++;
        break;
      default:
        break;
    }
    return MakeDecision();
  }

  bool AddSample(Operation op) {
    if (sample_freq_ == 1 && sample_prob_ == 1) {
      // Both parameters suggest to record every operation we saw
      cur_sample_size_++;
      return true;
    } else {
      if (sample_freq_ == 1) {
        // This suggests we have set a sample probability
        double n = dist_(generator_);
        if (n >= sample_prob_) {
          cur_sample_size_++;
          return true;
        }
      } else {
        // This suggests we have set a sample frequency
        visited_since_last_sample_++;
        if (visited_since_last_sample_ >= sample_freq_) {
          visited_since_last_sample_ = 0;
          cur_sample_size_++;
          return true;
        }
      }
    }
    return false;
  }

  bool MakeDecision() {
    if (cur_sample_size_ < sample_size_) {
      return false;
    }
    if (scan_num_ + insert_num_ + update_num_ > sample_size_) {
      std::cout << "Monitor records incorrect data\n";
      return false;
    }
    return true;
  }

  bool ScanHeavy() {
    bool scan_heavy = 1.0 * scan_num_ / sample_size_ >= scan_threshold_;
    if (scan_heavy && prev_op_ != SCAN) {
      prev_op_ = SCAN;
      return true;
    }
    return false;
  }

  bool WriteHeavy() {
    bool write_heavy = 1.0 * (insert_num_ + update_num_) / sample_size_ > write_threshold_;
    if (write_heavy && prev_op_ == SCAN) {
      prev_op_ = INSERT;
      return true;
    }
    return false;
  }

  void FinishDecision() {
    cur_sample_size_ = 0;
    scan_num_ = 0;
    insert_num_ = 0;
    update_num_ = 0;
  }

 private:
  std::mt19937_64 generator_;
  std::uniform_real_distribution<double> dist_{0, 1.0};

  // Tunable parameters
  uint32_t sample_freq_;
  double sample_prob_;
  uint64_t sample_size_;
  // If the ratio is greater or equal to the threshold, we adapt to this operation
  double scan_threshold_;
  // double insert_threshold_;
  double write_threshold_;

  // Counters
  uint32_t visited_since_last_sample_ = 0;
  uint64_t cur_sample_size_ = 0;
  uint64_t scan_num_ = 0;
  uint64_t insert_num_ = 0;
  uint64_t update_num_ = 0;

  Operation prev_op_ = INSERT;
};

} // namespace ycsbc

#endif