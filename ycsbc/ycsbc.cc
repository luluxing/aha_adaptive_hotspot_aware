//
//  ycsbc.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <iostream>
#include <queue>
#include <regex>
#include <string>
#include <vector>
#include <future>
#include <thread>
#include "benchmark.h"
#include "core/hotspot_generator.h"

using namespace std;
using namespace ycsbc;
using namespace benchmark;

struct StageParam {
  double scan;
  double insert;
  double update;
  double read;
  bool need_adapt;
  bool adapt_by_time;
  bool need_tree_insert;
  int op_or_sec_num;
  double hot_op_frac;
  bool change_hotspot;
};

void UsageMessage(const char *command);
bool StrStartWith(const char *str, const char *pre);
bool ParseCommandLine(int argc, const char *argv[],
                      std::map<std::string, std::string>* options,
                      utils::Properties &props,
                      utils::Properties &props2);

int report_throughput = 0;
std::vector<std::string> ops_vec;
int op_index;
std::atomic<uint64_t> done_ops(0);
bool print_page = false;

auto wl = new CoreWorkload();

int DelegateClient(ycsbc::DB *db, const int num_ops,
                  benchmark::Stats* stats);
int DelegateClientLoad(ycsbc::DB *db, const int num_ops,
                       benchmark::Stats* stats);



int DelegateClientLoad(ycsbc::DB *db, const int num_ops,
                       benchmark::Stats* stats) {
  
  ycsbc::Client client(*db);
  int oks = 0;
  bool adapt_start = false;
  int success = 0;

  while (true) {
    success = client.DoInsert(wl);
    oks += success;
    if (report_throughput) {
      if (success > 0) {
        // client.PrintAvgLatencySoFar();
        stats->FinishedOps(success);
        stats->DoneOp(success, 50000);
      }
    }
    
    if (oks >= num_ops) break;
  }

  fprintf(stdout, "Thread done\n");
  return oks;
}

int DelegateClient(ycsbc::DB *db, const int num_ops,
                  benchmark::Stats* stats) {
  // db->Init();
  ycsbc::Client client(*db);
  int oks = 0;
  bool adapt_start = false;
  int success = 0;

  success = client.DoTransaction(wl);
  oks += success;
  done_ops.fetch_add(success);
  if (report_throughput) {
    if (success > 0) {
      if (stats->FinishedOps(success)) db->Print(); 
      stats->DoneOp(success, 5000);
      // client.PrintAvgLatencySoFar();
    }
  }
  return oks;
}

void ParseStage(StageParam& param) {
  // Assume the workload in this format "i-0.2-r-0.0-s-0.7-u-0.1"
  float s, r, i, u;
  i = std::stof(ops_vec[op_index].substr(2, 3));
  r = std::stof(ops_vec[op_index].substr(8, 3));
  s = std::stof(ops_vec[op_index].substr(14, 3));
  u = std::stof(ops_vec[op_index].substr(20, 3));
  param.scan = s;
  param.insert = i;
  param.update = u;
  param.read = r;
  wl->op_chooser_.UpdateWorkload(s, r, u, i);
  fprintf(stdout, "New workload: scan %f; insert %f; update %f; read %f\n",
                s, i, u, r);
  
  // Example: -sequence 1 i-0.0-r-0.0-s-1.0-u-0.0 1 60 0 1 0
  // 1: use time instead of number of operations
  // 60: run after 60 seconds
  // 0: use tree insertion
  // 1: adapt to the read-optimized structure
  // [0, 1]: percentage of hotspot over cold spot operations
  // 0: no change in hotspot
  op_index++;
  param.adapt_by_time = std::stoi(ops_vec[op_index]) == 1 ? true : false;
  op_index++;
  param.op_or_sec_num = std::stoi(ops_vec[op_index]);
  op_index++;
  param.need_tree_insert = std::stoi(ops_vec[op_index]) == 1 ? true : false;
  op_index++;
  param.need_adapt = std::stoi(ops_vec[op_index]) == 1 ? true : false;
  op_index++;
  param.hot_op_frac = std::stof(ops_vec[op_index]);
  HotspotGenerator* gen = dynamic_cast<HotspotGenerator*>(wl->key_chooser_);
  if (gen != nullptr) {
    gen->SetHotopPercent(param.hot_op_frac);
  }
  op_index++;
  param.change_hotspot = std::stoi(ops_vec[op_index]) == 1 ? true : false;
}

void MonitorTime(ycsbc::DB *db, int num_threads, benchmark::Stats* stats) {
  // wl->hotspot_num_ = hotspots_num;
  ThreadPool* working_pool = new ThreadPool(num_threads - 2);
  ThreadPool* standby_pool = new ThreadPool(1);
  ThreadPool* standby_pool2 = new ThreadPool(1);

  working_pool->SetTask([=]() { DelegateClient(db, 1, stats); });
  standby_pool->SetTask([=]() { DelegateClient(db, 1, stats); });
  standby_pool2->SetTask([=]() { DelegateClient(db, 1, stats); });

  while (true) {
    if (op_index >= ops_vec.size()) {
      break;
    }
    done_ops.store(0);
    StageParam param;
    ParseStage(param);
    if (param.need_tree_insert) {
      db->EnableTreeInsertion();
    } else {
      db->EnableBufferInsertion();
    }
    if (param.change_hotspot) {
      wl->ChangeAndResetHotspot();
      db->UpdateHotspot(wl->hot_queried_start_, wl->hot_queried_end_);
    }
    // target_ops.store(param.op_or_sec_num);
    if (param.need_adapt == 1) {
      db->AddTree(true);
      fprintf(stdout, "Adapt to read\n");
      // db->Print();
    } else {
      db->AddTree(false);
      fprintf(stdout, "Noadapt to read\n");
      // db->Print();
    }
    op_index++;         

    done_ops.store(0);
    
    double elapsed_time = 0.0, cur_time = stats->TimeSinceStart();
    int count = 0, next_check = 100;
    bool bg_compaction_added = false, bg_flush_added = false;
    working_pool->start();
    if (param.need_tree_insert) {
      // Aha only
      standby_pool->start();
      standby_pool2->start();
      bg_compaction_added = true;
      bg_flush_added = true;
    }
      
    while (true) {  
      if (!param.need_tree_insert) {
        if (!bg_compaction_added) {
          if (db->BGCompactionIdle()) {
            standby_pool->start();
            bg_compaction_added = true;
          }
        } else {
          if (!db->BGCompactionIdle()) {
            standby_pool->clear();
            bg_compaction_added = false;
          }
        }
        if (!bg_flush_added) {
          if (db->BGFlushIdle()) {
            standby_pool2->start();
            bg_flush_added = true;
          }
        } else {
          if (!db->BGFlushIdle()) {
            standby_pool2->clear();
            bg_flush_added = false;
          }
        }
      }

      if (param.adapt_by_time) {
        elapsed_time = stats->TimeSinceStart() - cur_time;      
        if (elapsed_time >= param.op_or_sec_num) {
          break;
        }
      } else {
        if (done_ops.load() >= param.op_or_sec_num) {
          break;
        }
      }
    }

    working_pool->clear();
    standby_pool->clear();
    standby_pool2->clear();
            
    if (op_index >= ops_vec.size()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      break;
    }

    // Original seed in those distributions are incremented by 1
    wl->ResetSeed();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  delete working_pool;
  delete standby_pool;
  delete standby_pool2;
  if (print_page)
    db->PrintAllPages();
}

int main(const int argc, const char *argv[]) {
  utils::Properties props;
  utils::Properties props_2;
  std::map<std::string, std::string> options;
  ParseCommandLine(argc, argv, &options, props, props_2);

  ycsbc::DB *db = ycsbc::DBFactory::CreateDB(&options, props);
  if (!db) {
    cout << "Unknown database name " << options["dbname"] << endl;
    exit(0);
  }

  wl->Init(props);
  db->Init();
  db->SetHotspotStart(wl->hot_queried_start_);
  db->SetHotspotEnd(wl->hot_queried_end_);

  report_throughput = std::stoi(options["report_throughput"]);
  const int num_threads = std::stoi(options["threads"]);
  if (num_threads < 3) {
    fprintf(stderr, "We need at least 3 threads\n");
    std::abort();
  }
  const int loading_threads = std::stoi(options["loading_threads"]);

  // Loads data
  {
    std::cout << "###### Loading ########\n";
    // vector<future<int>> actual_ops;
    int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
    std::vector<std::thread> thread_vec;
    auto stats = new benchmark::Stats();
    stats->Start();
    // int load_threads = num_threads;
    for (int i = 0; i < loading_threads; ++i) {
      std::thread t(DelegateClientLoad, db, total_ops / loading_threads,
                   stats);
      thread_vec.push_back(std::move(t));
    }
    for (int i = 0; i < thread_vec.size(); i++) {
      thread_vec[i].join();
    }
    delete stats;
  }
  db->Print();

  // Make sure insertion is completely done before proceeding
  {
    // Measure the elaspsed time
    auto start = std::chrono::high_resolution_clock::now();
    if (options["db_name"] == "wot") {
      while (!db->BGFlushIdle() || !db->BGCompactionIdle()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "BG idle in " << elapsed.count() << " seconds\n";
  }

  // Perform operation
  {
    std::cout << "###### Operations ########\n";
    // vector<future<int>> actual_ops;
    int total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
    // std::vector<std::thread> thread_vec;
    utils::Timer<double> timer;
    timer.Start();
    auto stats = new benchmark::Stats();
    stats->Start();

    MonitorTime(db, num_threads, stats);

    stats->PrintAvgThroughput();
    // if (!report_throughput) {
      fprintf(stdout, "Final-throughput is %f\n", total_ops / timer.End());
    // }
    delete stats;
  }
  // db->Print();
  delete wl;
  delete db;
}

// TODO: instead of pass parameters via cmd, use an option file
bool ParseCommandLine(int argc, const char *argv[],
                      std::map<std::string, std::string>* options,
                      utils::Properties &props, utils::Properties &props2) {
  int argindex = 1;
  bool has_p2 = false;
  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-options") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      WOT_NAMESPACE::LoadOptions(argv[argindex], options);
      argindex++;
    } else if (strcmp(argv[argindex], "-workload") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      // filename.assign(argv[argindex]);
      ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const string &message) {
        cout << message << endl;
        exit(0);
      }
      input.close();
      argindex++;
    } else if (strcmp(argv[argindex], "-sequence") == 0) {
      argindex++;
      int op_num = std::stoi(argv[argindex]);
      argindex++;
      for (int i = 0; i < op_num * 7; i++) {
        ops_vec.push_back(argv[argindex]);
        argindex++;
      }
    } else if (strcmp(argv[argindex], "-printpage") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      print_page = std::stoi(argv[argindex]) == 1 ? true : false;
      argindex++;
    } else {
      cout << "Unknown option '" << argv[argindex] << "'" << endl;
      exit(0);
    }
  }

  if (argindex == 1 || argindex != argc) {
    UsageMessage(argv[0]);
    exit(0);
  }

  return has_p2;
}

void UsageMessage(const char *command) {
  cout << "Usage: " << command << " [options]" << endl;
  cout << "Options:" << endl;
  cout << "\t-options file: load options file\n";
  cout << "\t-workload propertyfile: load properties from the given file." <<
                    "Multiple files can be specified, and will be"  <<
                    "processed in the order specified\n";
  cout << "\t-workload2 2nd propertyfile: start the workload after the 1st one (default: "")\n";
}

inline bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}

