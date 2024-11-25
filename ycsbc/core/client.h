//
//  client.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/10/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_CLIENT_H_
#define YCSB_C_CLIENT_H_

#include <chrono>
#include <iostream>
#include <string>
#include "db.h"
#include "core_workload.h"
#include "utils.h"
#include "workload_monitor.h"

namespace ycsbc {

class Client {
 public:
  Client(DB &db/*,CoreWorkload &wl, std::map<std::string, std::string>* options,
         bool use*/)
  : db_(db) {}
    // workload_(wl),
    // monitor_(new WorkloadMonitor(std::stoi((*options)["sample_freq"]),
    //                              std::stod((*options)["sample_prob"]),
    //                              std::stoi((*options)["sample_size"]),
    //                              std::stod((*options)["scan_threshold"]),
    //                              std::stod((*options)["write_threshold"]))),
    // adapt_(adapt),
    // use_monitor_(use) { }
  
  virtual int DoInsert(CoreWorkload* workload);
  virtual int DoTransaction(CoreWorkload* workload);
  virtual void PrintAvgLatencySoFar();
  
  virtual ~Client() { /*delete monitor_;*/ }

  virtual int CorrectnessTransaction(CoreWorkload* wl);
  virtual int CorrectnessInsertion(CoreWorkload* wl, FILE* file);
  virtual int CorrectnessUpdate(CoreWorkload* wl, FILE* file);

 protected:

  std::pair<double, int> read_latency_{0, 0};
  std::pair<double, int> insert_latency_{0, 0};
  std::pair<double, int> update_latency_{0, 0};
  std::pair<double, int> scan_latency_{0, 0};
  
  virtual int TransactionRead(CoreWorkload* workload);
  virtual int TransactionReadModifyWrite(CoreWorkload* workload);
  virtual int TransactionScan(CoreWorkload* workload);
  virtual int TransactionUpdate(CoreWorkload* workload);
  virtual int TransactionInsert(CoreWorkload* workload);

  void PrintLatency(std::pair<double, int>* lat, std::string name);
  
  DB &db_;
  std::mutex mu_;
  // CoreWorkload* workload_;
  // WorkloadMonitor* monitor_;
  // bool adapt_;
  // bool use_monitor_;
};

inline void Client::PrintLatency(std::pair<double, int>* lat, std::string name) {
  if (lat->second != 0) {
    // std::cout << "Total " << name << lat->first << "; ";
    // std::cout << "Avg " << name << lat->first / lat->second << "; ";
    fprintf(stdout, "%s Avg=%f/%d=%f; ", name.c_str(), lat->first, lat->second,
                                         lat->first / lat->second);
  }
  lat->first = 0;
  lat->second = 0;
}

inline void Client::PrintAvgLatencySoFar() {
  PrintLatency(&read_latency_, "Read ");
  PrintLatency(&update_latency_, "Update ");
  PrintLatency(&insert_latency_, "Insert ");
  PrintLatency(&scan_latency_, "Scan ");
  std::cout << "\n";
}

inline int Client::DoInsert(CoreWorkload* workload) {
  std::vector<DB::KVPair> pairs;
  if (db_.WriteBatchSize() == 0) {
    std::string key = workload->NextSequenceKey();
    // workload->BuildValues(pairs);
    DB::KVPair pair;
    pair.first = key;
    pair.second = workload->BuildValue();
    pairs.push_back(pair);
    return db_.Insert(workload->NextTable(), key, pairs);
  } else {
    for (int i = 0; i < db_.WriteBatchSize(); ++i) {
      std::string key = workload->NextSequenceKey();
      DB::KVPair pair;
      pair.first = key;
      pair.second = workload->BuildValue();
      pairs.push_back(pair);
    }
    return db_.BatchInsert(workload->NextTable(), pairs);
  }
}

inline int Client::CorrectnessInsertion(CoreWorkload* workload, FILE* file) {
  std::vector<DB::KVPair> pairs;
  if (db_.WriteBatchSize() == 0) {
    std::string key = workload->NextSequenceKey();
    // workload->BuildValues(pairs);
    DB::KVPair pair;
    pair.first = key;
    pair.second = workload->BuildValue();
    pairs.push_back(pair);
    if (strcmp(key.substr(key.size()-10).c_str(), "0045597137") == 0) {
      fprintf(stdout, "FInd!!!\n");
    }
    fprintf(file, "print: %s => %c\n", key.substr(key.size()-10).c_str(), pair.second.c_str()[0]);
    return db_.Insert(workload->NextTable(), key, pairs);
  } else {
    for (int i = 0; i < db_.WriteBatchSize(); ++i) {
      std::string key = workload->NextSequenceKey();
      DB::KVPair pair;
      pair.first = key;
      pair.second = workload->BuildValue();
      pairs.push_back(pair);
      fprintf(file, "print: %s => %c\n", key.substr(key.size()-10).c_str(), pair.second.c_str()[0]);
    }
    return db_.BatchInsert(workload->NextTable(), pairs);
  }
}

inline int Client::CorrectnessUpdate(CoreWorkload* workload, FILE* file) {
  std::vector<DB::KVPair> pairs;
  if (db_.WriteBatchSize() == 0) {
    std::string key = workload->NextSequenceKey();
    // workload->BuildValues(pairs);
    DB::KVPair pair;
    pair.first = key;
    pair.second = workload->BuildValue();
    pairs.push_back(pair);
    if (strcmp(key.substr(key.size()-10).c_str(), "0000000321") == 0) {
      fprintf(stdout, "FInd!!!\n");
    }
    fprintf(file, "print: %s => %c\n", key.substr(key.size()-10).c_str(), pair.second.c_str()[0]);
    return db_.Update(workload->NextTable(), key, pairs);
  } else {
    for (int i = 0; i < db_.WriteBatchSize(); ++i) {
      std::string key = workload->NextSequenceKey();
      DB::KVPair pair;
      pair.first = key;
      pair.second = workload->BuildValue();
      pairs.push_back(pair);
      if (strcmp(key.substr(key.size()-10).c_str(), "0000000321") == 0) {
        fprintf(stdout, "FInd!!!\n");
      }
      fprintf(file, "print: %s => %c\n", key.substr(key.size()-10).c_str(), pair.second.c_str()[0]);
    }
    return db_.BatchUpdate(workload->NextTable(), pairs);
  }
}

inline int Client::DoTransaction(CoreWorkload* workload) {
  int status = -1;
  Operation op = workload->NextOperation();
  // if (use_monitor_ && adapt_ && monitor_->AddSampleAndMakeDecision(op)) {
  //   if (monitor_->ScanHeavy()) {
  //     db_.AddTree(true);
  //     std::cout << "adapt to read\n";
  //   } else if (monitor_->WriteHeavy()) {
  //     db_.AddTree(false);
  //     std::cout << "no-adapt to read\n";
  //   }
  //   monitor_->FinishDecision();
  // }
  auto start = std::chrono::steady_clock::now();
  auto end = start;
  std::chrono::duration<double> elapsed_seconds = end-start;
  // std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
  switch (op) {
    case READ:
      status = TransactionRead(workload);
      end = std::chrono::steady_clock::now();
      elapsed_seconds = end-start;
      {
        std::unique_lock<std::mutex> lock(mu_);
        read_latency_.first += elapsed_seconds.count();
        read_latency_.second++;
      }
      break;
    case UPDATE:
      status = TransactionUpdate(workload);
      // end = std::chrono::steady_clock::now();
      // elapsed_seconds = end-start;
      // {
      //   std::unique_lock<std::mutex> lock(mu_);
      //   update_latency_.first += elapsed_seconds.count();
      //   update_latency_.second++;
      // }
      break;
    case INSERT:
      status = TransactionInsert(workload);
      // end = std::chrono::steady_clock::now();
      // elapsed_seconds = end-start;
      // {
      //   std::unique_lock<std::mutex> lock(mu_);
      //   insert_latency_.first += elapsed_seconds.count();
      //   insert_latency_.second++;
      // }
      break;
    case SCAN:
      status = TransactionScan(workload);
      // end = std::chrono::steady_clock::now();
      // elapsed_seconds = end-start;
      // {
      //   std::unique_lock<std::mutex> lock(mu_);
      //   if (status > 0) {
      //     scan_latency_.first += elapsed_seconds.count();
      //     scan_latency_.second += status;
      //   }
      // }
      break;
    case READMODIFYWRITE:
      status = TransactionReadModifyWrite(workload);
      break;
    default:
      throw utils::Exception("Operation request is not recognized!");
  }
  // assert(status >= 0);
  return status;
}

inline int Client::TransactionRead(CoreWorkload* workload) {
  const std::string &table = workload->NextTable();
  const std::string &key = workload->NextTransactionKey();
  std::vector<DB::KVPair> result;
  if (!workload->read_all_fields()) {
    std::vector<std::string> fields;
    fields.push_back("field" + workload->NextFieldName());
    return db_.Read(table, key, &fields, result);
  } else {
    return db_.Read(table, key, NULL, result);
  }
}

inline int Client::TransactionReadModifyWrite(CoreWorkload* workload) {
  const std::string &table = workload->NextTable();
  const std::string &key = workload->NextTransactionKey();
  std::vector<DB::KVPair> result;

  if (!workload->read_all_fields()) {
    std::vector<std::string> fields;
    fields.push_back("field" + workload->NextFieldName());
    db_.Read(table, key, &fields, result);
  } else {
    db_.Read(table, key, NULL, result);
  }

  std::vector<DB::KVPair> values;
  if (workload->write_all_fields()) {
    workload->BuildValues(values);
  } else {
    workload->BuildUpdate(values);
  }
  return db_.Update(table, key, values);
}

inline int Client::TransactionScan(CoreWorkload* workload) {
  const std::string &table = workload->NextTable();
  // const std::string &key = workload->NextTransactionKey();
  // int len = workload_.NextScanLength();
  // const std::string &upper_key = workload->NextTransactionKeyUpper();
  auto str_pair = workload->NextTransactionKeys();
  const std::string& lk = str_pair.first;
  const std::string& uk = str_pair.second;

  std::vector<std::vector<DB::KVPair>> result;
  int select = workload->GetRangeScanSelectivity();
  if (!workload->read_all_fields()) {
    std::vector<std::string> fields;
    fields.push_back("field" + workload->NextFieldName());
    return db_.Scan(table, lk, uk, &fields, result, select);
  } else {
    return db_.Scan(table, lk, uk, NULL, result, select);
  }
}

inline int Client::TransactionUpdate(CoreWorkload* workload) {
  std::vector<DB::KVPair> values;
  if (db_.WriteBatchSize() > 0) {
    for (int i = 0; i < db_.WriteBatchSize(); ++i) {
      std::string key = workload->NextTransactionKey();
      DB::KVPair pair;
      pair.first = key;
      pair.second = workload->BuildValue();
      values.push_back(pair);
    }
    return db_.BatchUpdate(workload->NextTable(), values);
  } else {
    const std::string &table = workload->NextTable();
    const std::string &key = workload->NextTransactionKey();
    // if (workload->write_all_fields()) {
      // workload->BuildValues(values);
      DB::KVPair pair;
      pair.first = key;
      pair.second = workload->BuildValue();
      values.push_back(pair);
    // } else {
    //   workload->BuildUpdate(values);
    // }
    return db_.Update(table, key, values);
  }
}

inline int Client::TransactionInsert(CoreWorkload* workload) {
  const std::string &table = workload->NextTable();
  std::vector<DB::KVPair> values;
  if (db_.WriteBatchSize() > 0) {
    for (int i = 0; i < db_.WriteBatchSize(); ++i) {
      std::string key = workload->NextSequenceKey();
      DB::KVPair pair;
      pair.first = key;
      pair.second = workload->BuildValue();
      values.push_back(pair);
    }
    return db_.BatchInsert(workload->NextTable(), values);
  } else {
    const std::string &key = workload->NextSequenceKey();
    workload->BuildValues(values);
    return db_.Insert(table, key, values);
  }
} 

inline int Client::CorrectnessTransaction(CoreWorkload* wl) {
  return TransactionScan(wl);
}

} // ycsbc

#endif // YCSB_C_CLIENT_H_
