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
#include "db/level_db.h"
#include "db/btree_db.h"
#include "db/wotdb.h"
#include "db/buffer_tree_db.h"


using namespace benchmark;

bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}

ycsbc::LeveldbYcsb* level_db = nullptr;
ycsbc::BtreeDB* btree_db = nullptr;
ycsbc::WotDB* wot_db  = nullptr;
ycsbc::WotDB* lsmt_db = nullptr;
#ifdef INCLUDE_BUFFERTREE
ycsbc::BufferTreeDB* buftree_db = nullptr;
#endif

std::vector<std::string> ops_vec;
std::vector<bool> adapt_start_vec;

auto wl = new CoreWorkload();

void ParseCommandLine(int argc, const char *argv[],
                      std::map<std::string, std::string>* options,
                      utils::Properties &props, utils::Properties &props2) {
  int argindex = 1;
  fprintf(stdout, "argc: %d\n", argc);
  if (argc <= 1) {
    fprintf(stderr, "Usage: %s [options]\n", argv[0]);
    std::exit(0);
  }
  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-leveldb") == 0) {
      argindex++;
      std::map<std::string, std::string> options;
      WOT_NAMESPACE::LoadOptions(argv[argindex], &options);
      level_db = new ycsbc::LeveldbYcsb(&options);
      argindex++;
    } else if (strcmp(argv[argindex], "-btree") == 0) {
      argindex++;
      WOT_NAMESPACE::LoadOptions(argv[argindex], options);
      btree_db = new ycsbc::BtreeDB(options);
      argindex++;
    } else if (strcmp(argv[argindex], "-aha") == 0) {
      argindex++;
      WOT_NAMESPACE::LoadOptions(argv[argindex], options);
      wot_db = new ycsbc::WotDB(options);
      argindex++;
    } else if (strcmp(argv[argindex], "-lsmt") == 0) {
      argindex++;
      WOT_NAMESPACE::LoadOptions(argv[argindex], options);
      lsmt_db = new ycsbc::WotDB(options);
      argindex++;
#ifdef INCLUDE_BUFFERTREE
    } else if (strcmp(argv[argindex], "-buftree") == 0) {
      argindex++;
      WOT_NAMESPACE::LoadOptions(argv[argindex], options);
      buftree_db = new ycsbc::BufferTreeDB(options);
      argindex++;
#endif
    } else if (strcmp(argv[argindex], "-workload") == 0) {
      argindex++;
      std::ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const std::string &message) {
        std::cout << message << std::endl;
        std::exit(0);
      }
      input.close();
      argindex++;
    } else {
      std::cout << "Unknown option '" << argv[argindex] << "'" << std::endl;
      std::exit(0);
    }
  }
}

void SetIndexHotspot(std::vector<ycsbc::DB*> dbs) {
  for (auto const& db : dbs) {
    if (db != nullptr) {
      db->Init();
      db->SetHotspotStart(wl->hot_queried_start_);
      db->SetHotspotEnd(wl->hot_queried_end_);
    }
  }
}

// Write output to file
int DelegateClientLoad(ycsbc::DB *db, const int num_ops, std::string filename) {
  ycsbc::Client client(*db);
  int oks = 0;
  bool adapt_start = false;
  int success = 0;
  FILE* file = fopen(filename.c_str(), "w");
  while (true) {
    success = client.CorrectnessInsertion(wl, file);
    oks += success;
    if (oks >= num_ops) break;
  }
  fclose(file);
  fprintf(stdout, "Index done\n");
  return oks;
}

// Write output to file
int DelegateClientUpdate(ycsbc::DB *db, const int num_ops, std::string filename) {
  ycsbc::Client client(*db);
  int oks = 0;
  bool adapt_start = false;
  int success = 0;
  FILE* file = fopen(filename.c_str(), "w");
  while (true) {
    success = client.CorrectnessUpdate(wl, file);
    oks += success;
    if (oks >= num_ops) break;
  }
  fclose(file);
  fprintf(stdout, "Index done\n");
  return oks;
}


void LoadIndex(utils::Properties props,
              std::vector<ycsbc::DB*> dbs,
              std::vector<std::string> db_name) {
  // Only one thread is allowed per index
  std::cout << "###### Loading ########\n";
  std::vector<std::thread> thread_vec;
  for (int i = 0; i < dbs.size(); i++) {
    auto db = dbs[i];
    auto dn = db_name[i];
    if (db == nullptr) continue;

    // vector<future<int>> actual_ops;
    int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
    // int load_threads = num_threads;
    std::thread t(DelegateClientLoad, db, total_ops / 1, dn + "_load.txt");
    thread_vec.push_back(std::move(t));
  }
  // Join the threads
  for (auto& t : thread_vec) {
    t.join();
  }
  std::cout << "Done Loading\n";
}

int DelegateClient(ycsbc::DB *db,int wait_time) {
  // db->Init();
  ycsbc::Client client(*db);
  int oks = 0;
  // Current time in milliseconds
  auto start = std::chrono::steady_clock::now();
  while (true) {
    oks = client.CorrectnessTransaction(wl);
    // if elapsed time exceeds wait time
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;
    if (elapsed_seconds.count() > wait_time) {
      break;
    }
  }
  return oks;
}

void ReadIndex(utils::Properties props,
              int num_threads,  // Number of threads  
              std::vector<ycsbc::DB*> dbs,
              std::vector<std::string> db_name) {
  std::vector<std::thread> thread_vec;
  std::string lk = wl->GetLowestKey();
  std::string uk = wl->GetHighestKey();
  std::cout << "###### Reading ########\n";
  for (int i = 0; i < dbs.size(); i++) {
    auto db = dbs[i];
    auto dn = db_name[i];
    if (db == nullptr) continue;

    if (strcmp(dn.c_str(), "aha") != 0 && strcmp(dn.c_str(), "buftree") != 0) {
      // leveldb, lsmt, btree
      db->PrintAllData(lk, uk, dn + "_read.txt");
      return;
    }
    // Other index requires adaptation
    // We need to check at least three times
    // 1. before adapt
    db->PrintAllData(lk, uk, dn + "_read_before.txt");
    db->AddTree(true);
    int total_ops = 1000000;
    // 2. during adapt 
    int wait_time = 2;
    for (int i = 0; i < num_threads; i++) {
      std::thread t(DelegateClient, db, wait_time);
      thread_vec.push_back(std::move(t));
    }
    // Join the threads
    for (auto& t : thread_vec) {
      t.join();
    }
    thread_vec.clear();
    db->PrintAllData(lk, uk, dn + "_read_mid.txt");
    // 3. after adapt
    wait_time = 60;
    for (int i = 0; i < num_threads; i++) {
      std::thread t(DelegateClient, db, wait_time);
      thread_vec.push_back(std::move(t));
    }
    // Join the threads
    for (auto& t : thread_vec) {
      t.join();
    }
    db->PrintAllData(lk, uk, dn + "_read_after.txt");
  }
}

void ReadWriteReadIndex(utils::Properties props,
              int num_threads,  // Number of threads  
              std::vector<ycsbc::DB*> dbs,
              std::vector<std::string> db_name) {
  std::vector<std::thread> thread_vec;
  std::string lk = wl->GetLowestKey();
  std::string uk = wl->GetHighestKey();
  std::cout << "###### Reading and Writing ########\n";
  for (int i = 0; i < dbs.size(); i++) {
    auto db = dbs[i];
    auto dn = db_name[i];
    if (db == nullptr) continue;

    if (strcmp(dn.c_str(), "aha") != 0) {
      // leveldb, lsmt, btree, bufftree
      return;
    }
    db->AddTree(true);
    int wait_time = 60;
    for (int i = 0; i < num_threads; i++) {
      std::thread t(DelegateClient, db, wait_time);
      thread_vec.push_back(std::move(t));
    }
    // Join the threads
    for (auto& t : thread_vec) {
      t.join();
    }
    thread_vec.clear();
    db->PrintAllData(lk, uk, dn + "_read_after.txt");
    
    // Tree insertion
    // db->EnableTreeInsertion();
    // Buffer insertion
    int total_ops = 10000000;
    std::thread t(DelegateClientUpdate, db, total_ops / 1, dn + "_load2.txt");
    thread_vec.push_back(std::move(t));
    for (auto& t : thread_vec) {
      t.join();
    }
    thread_vec.clear();

    // 2nd adaptation
    wait_time = 120;
    for (int i = 0; i < num_threads; i++) {
      std::thread t(DelegateClient, db, wait_time);
      thread_vec.push_back(std::move(t));
    }
    // Join the threads
    for (auto& t : thread_vec) {
      t.join();
    }
    db->PrintAllData(lk, uk, dn + "_read_after_write.txt");
  }
}

int main(const int argc, const char *argv[]) {
  utils::Properties props;
  utils::Properties props_2;
  std::map<std::string, std::string> options;
  ParseCommandLine(argc, argv, &options, props, props_2);

  wl->Init(props);

  std::vector<ycsbc::DB*> dbs;
  std::vector<std::string> db_name;
  dbs.push_back(level_db);
  db_name.push_back("leveldb");
  dbs.push_back(btree_db);
  db_name.push_back("btree");
  dbs.push_back(wot_db);
  db_name.push_back("aha");
  dbs.push_back(lsmt_db);
  db_name.push_back("lsmt");
#ifdef INCLUDE_BUFFERTREE
  dbs.push_back(buftree_db);
  db_name.push_back("buftree");
#endif


  SetIndexHotspot(dbs);

  LoadIndex(props, dbs, db_name);

  const int num_threads = 10;
  // ReadIndex(props, num_threads, dbs, db_name);

  ReadWriteReadIndex(props, num_threads, dbs, db_name);
}