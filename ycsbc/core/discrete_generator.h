//
//  discrete_generator.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/6/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_DISCRETE_GENERATOR_H_
#define YCSB_C_DISCRETE_GENERATOR_H_

#include "generator.h"

#include <atomic>
#include <cassert>
#include <mutex>
#include <vector>
#include "utils.h"

namespace ycsbc {

enum Operation {
  INSERT,
  READ,
  UPDATE,
  SCAN,
  READMODIFYWRITE
};

template <typename Value>
class DiscreteGenerator : public Generator<Value> {
 public:
  DiscreteGenerator() 
  : sum_(0),
    seed_(10), // default seed
    rd_(new utils::RandomDevice(10)) { }
  
  void AddValue(Value value, double weight);

  Value Next();
  Value Last() { return last_; }
  
  // allow a new distribution
  void Clear();
  Value Maxdata() { return Last(); }
  void UpdateWorkload(double s, double r, double u, double i);
  void ResetSeed() { rd_ = new utils::RandomDevice(seed_ + 1); }

 private:
  std::vector<std::pair<Value, double>> values_;
  int seed_;
  double sum_;
  std::atomic<Value> last_;
  std::mutex mutex_;
  utils::RandomDevice* rd_;
};

template <typename Value>
inline void DiscreteGenerator<Value>::UpdateWorkload(double s, double r, double u, double i) {
  // mutex_.lock();
  std::unique_lock<std::mutex> lock(mutex_);
  Clear();
  AddValue(ycsbc::Operation::SCAN, s);
  AddValue(ycsbc::Operation::READ, r);
  AddValue(ycsbc::Operation::UPDATE, u);
  AddValue(ycsbc::Operation::INSERT, i);
  // mutex_.unlock();
}

template <typename Value>
inline void DiscreteGenerator<Value>::Clear() {
  sum_ = 0;
  values_.clear();
}

template <typename Value>
inline void DiscreteGenerator<Value>::AddValue(Value value, double weight) {
  if (values_.empty()) {
    last_ = value;
  }
  values_.push_back(std::make_pair(value, weight));
  sum_ += weight;
}

template <typename Value>
inline Value DiscreteGenerator<Value>::Next() {
  // mutex_.lock();
  std::unique_lock<std::mutex> lock(mutex_);
  double chooser = rd_->RandomDouble(); //utils::RandomDouble();
  // mutex_.unlock();
  
  for (auto p = values_.cbegin(); p != values_.cend(); ++p) {
    if (chooser < p->second / sum_) {
      return last_ = p->first;
    }
    chooser -= p->second / sum_;
  }
  
  assert(false);
  return last_;
}

} // ycsbc

#endif // YCSB_C_DISCRETE_GENERATOR_H_
