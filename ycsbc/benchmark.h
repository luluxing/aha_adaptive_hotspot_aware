#include "db/db_factory.h"
#include "core/utils.h"
#include "core/timer.h"
#include "core/client.h"
#include "core/core_workload.h"
#include "util/helper.h"
#include "leveldb/port/port_stdcxx.h"
#include "leveldb/util/mutexlock.h"

#include <sys/time.h>

using namespace ycsbc;

namespace benchmark {

inline double get_now() { 
  struct timeval tv; 
  gettimeofday(&tv, 0); 
  return tv.tv_sec + tv.tv_usec / 1000000.0; 
} 

class Stats {
  utils::Timer<double> timer_;
  double seconds_;
  std::atomic<uint64_t> done_;
  std::atomic<uint64_t> interval_done_;
  uint64_t last_report_done_;
  uint64_t next_report_;
  uint64_t stats_interval_seconds_;
  uint64_t stats_interval_;
  uint64_t checkpoint_;
  double last_ts_;
  uint64_t last_done_;
  uint64_t last_interval_done_;
  WOT_NAMESPACE::port::Mutex mu_;

 public:
  Stats() = default;
  ~Stats() = default;

  void Start() {
    timer_.Start();
    next_report_ = 10;
    stats_interval_ = 2;
    stats_interval_seconds_ = 1; // report per second
    done_.store(0);
    interval_done_.store(0);
    last_report_done_ = 0;
    last_interval_done_ = 0;
    seconds_ = 0;
    last_ts_ = get_now();
  }

  double TimeSinceStart() {
    return timer_.DurationSinceStart();
  }

  void Reset() {
    timer_.Reset();
    done_.store(0);
    last_report_done_ = 0;
    seconds_ = 0;
  }

  void Checkpoint() {
    checkpoint_ = timer_.DurationSinceStart();
  }

  void ElapsedSinceCheckpoint() {
    fprintf(stdout, "Elapsed %.6f seconds\n", 
            timer_.DurationSinceStart() - checkpoint_);
  }

  void DoneOp(uint64_t num_ops, uint64_t op_interval) {
    WOT_NAMESPACE::MutexLock l(&mu_);
    uint64_t prev = interval_done_.fetch_add(num_ops);
    if (prev / op_interval == interval_done_.load() / op_interval - 1) {
      double cur_ts = get_now();
      double tput = (interval_done_.load() - last_interval_done_) / (cur_ts - last_ts_);
      last_ts_ = cur_ts;
      last_interval_done_ = interval_done_.load();
      fprintf(stdout, "$%.3f; ", tput);
      if (prev / (op_interval * 10) == interval_done_.load() / (op_interval * 10) - 1) {
        fprintf(stdout, "\n");
      }
    }
    
  }

  bool FinishedOps(uint64_t num_ops) {
    WOT_NAMESPACE::MutexLock l(&mu_);
    done_.fetch_add(num_ops);
    if (done_ >= next_report_) {
      double secs_since_last = timer_.End();
      double secs_since_start = timer_.DurationSinceStart();
      // Determine whether to print status where interval is either
      // each N operations or each N seconds.

      if (stats_interval_seconds_ &&
          secs_since_last < stats_interval_seconds_) {
        // Don't check again for this many operations.
        next_report_ += stats_interval_;

      } else {
        fprintf(stdout,
                "\n(%lu, %lu) ops and "
                "(%.1f,%.1f) ops/second in (%.6f,%.6f) seconds\n",
                done_.load() - last_report_done_, done_.load(),
                (done_.load() - last_report_done_) / secs_since_last,
                done_.load() / secs_since_start,
                secs_since_last, secs_since_start);
        timer_.DurationAndReset();
        next_report_ += stats_interval_;
        last_report_done_ = done_.load();
        fflush(stdout);
        return true;
      } 
    }
    return false;
  }

  void PrintAvgThroughput() {
    double secs_since_start = timer_.DurationSinceStart();
    fprintf(stdout, "Total done %lu ops in %.6f seconds, avg throughput %.1f\n",
                    done_.load(), secs_since_start, done_.load() / secs_since_start);
  }
};

class ThreadPool {
 public:
  explicit ThreadPool(size_t num_thr) 
  : stop(false),
    task_num_(0) {
    for (size_t i = 0; i < num_thr; ++i) {
      workers.emplace_back(
        [this] {
          while (true) {
              // std::function<void()> task;

              {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, 
                              [this] { return stop || task_num_.load() > 0; });

                if (stop && task_num_.load() == 0) {
                    return;
                }

                // task = std::move(tasks.front());
                // tasks.pop();
                // task_num_.fetch_sub(1);
              }

              task_();
          }
        }
      );
    }
  }

  void start() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      task_num_.store(1);
      // if (tasks.size() <= 100000 * workers.size()) {
      //   tasks.emplace([=]() mutable { std::forward<F>(f)(std::forward<Args>(args)...); });
      // }
    }
    condition.notify_all();
  }

  void clear() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      task_num_.store(0);
    }
    condition.notify_all();
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
      task_num_.store(0);
    }
    condition.notify_all();
    for (auto &worker : workers) {
      worker.join();
    }
  }

  void SetTask(std::function<void()> task) {
    task_ = task;
  }

private:
    std::vector<std::thread> workers;
    // std::queue<std::function<void()>> tasks;
    std::atomic<uint64_t> task_num_;
    std::function<void()> task_;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// class WorkloadState {
//  public:
//   virtual WorkloadState* ChangeState(int scan, int update) = 0;
//   virtual void StartTransaction(ThreadPool* wpool, ThreadPool* s1, ThreadPool* s2,
//                               DB* db, benchmark::Stats* stats, int adapt) = 0;


// };

// // We are transforming from mixed r/w to a mixed r/w
// class ToWriteState : public WorkloadState {
//  public:
//   static WorkloadState* Instance() {
//     static ToWriteState instance;
//     return &instance;
//   }

//   // We can have a relatively large num_ops here.
//   // We cannot use the two BG threads when there are write ops
//   void StartTransaction(ThreadPool* wpool, ThreadPool* s1, ThreadPool* s2, DB* db,
//                       benchmark::Stats* stats, int adapt) override;

//   WorkloadState* ChangeState(int scan, int update) override;
// };

// // We are transforming from mixed r/w to adapted read-optimized.
// // We need to monitor here when the adaptation process is done.
// class Write2ReadState : public WorkloadState {
//  public:
//   static WorkloadState* Instance() {
//     static Write2ReadState instance;
//     return &instance;
//   }

//   WorkloadState* ChangeState(int scan, int update) override;

//   // We need to have a relatively small num_ops here as we monitor the
//   // index adaptation process. When BG compaction thread is done, we can
//   // enable one standby thread. When BG flushing/adaptation is done, we
//   // can then enable the other standby thread.
//   void StartTransaction(ThreadPool* wpool, ThreadPool* s1, ThreadPool* s2, DB* db,
//                       benchmark::Stats* stats, int adapt) override;

//  private:
//   bool compaction_thread_idle = false;
//   bool flushing_thread_idle = false;
// };

} // namespace benchmark