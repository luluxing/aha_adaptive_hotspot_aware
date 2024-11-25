//
//  timer.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_TIMER_H_
#define YCSB_C_TIMER_H_

#include <chrono>

namespace utils {

template <typename T>
class Timer {
 public:
  void Start() {
    cur_time_ = Clock::now();
    start_time_ = cur_time_;
  }

  T End() {
    Duration span;
    Clock::time_point t = Clock::now();
    span = std::chrono::duration_cast<Duration>(t - cur_time_);
    return span.count();
  }

  T DurationAndReset() {
    Duration span;
    Clock::time_point t = Clock::now();
    span = std::chrono::duration_cast<Duration>(t - cur_time_);
    cur_time_ = t;
    return span.count();
  }

  T DurationSinceStart() {
    Duration span;
    Clock::time_point t = Clock::now();
    span = std::chrono::duration_cast<Duration>(t - start_time_);
    return span.count();
  }

  void Reset() {
    cur_time_ = Clock::now();
    start_time_ = cur_time_;
  }

 private:
  typedef std::chrono::high_resolution_clock Clock;
  typedef std::chrono::seconds Duration;

  Clock::time_point cur_time_;
  Clock::time_point start_time_;
};

} // utils

#endif // YCSB_C_TIMER_H_

