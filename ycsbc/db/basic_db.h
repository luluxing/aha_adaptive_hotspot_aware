//
//  basic_db.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_BASIC_DB_H_
#define YCSB_C_BASIC_DB_H_

#include "../core/db.h"

#include <iostream>
#include <string>
#include <mutex>
#include "../core/properties.h"

using std::cout;
using std::endl;

namespace ycsbc {

// used to record the workload data
class BasicDB : public DB {
 public:
  BasicDB(std::map<std::string, std::string>* options)
  : print_key((*options)["print_key"] == "1") {}

  void Init() {
    std::lock_guard<std::mutex> lock(mutex_);
    cout << "A new thread begins working." << endl;
  }

  int Read(const std::string &table, const std::string &key,
           const std::vector<std::string> *fields,
           std::vector<KVPair> &result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      cout << "READ " << key;
      if (fields) {
        cout << " [ ";
        for (auto f : *fields) {
          cout << f << ' ';
        }
        cout << ']' << endl;
      } else {
        cout  << " < all fields >" << endl;
      }
    }
    return 0;
  }

  int Scan(const std::string &table, const std::string &key,
           int len, const std::vector<std::string> *fields,
           std::vector<std::vector<KVPair>> &result, int select) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      cout << "SCAN " << key << " " << len;
      if (fields) {
        cout << " [ ";
        for (auto f : *fields) {
          cout << f << ' ';
        }
        cout << ']' << endl;
      } else {
        cout  << " < all fields >" << endl;
      }
    }
    return 0;
  }

  int Update(const std::string &table, const std::string &key,
             std::vector<KVPair> &values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      fprintf(stdout, "UPDATE %s\n", key.c_str());
      fflush(stdout);
      // cout << "UPDATE " << key << " [ ";
      // for (auto v : values) {
      //   cout << v.first << '=' << v.second << ' ';
      // }
      // cout << ']' << endl;
    }
    return 0;
  }

  int BatchUpdate(const std::string &table, std::vector<KVPair> &values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      fprintf(stdout, "BATCH UPDATE\n");
      fflush(stdout);
      cout << ']' << endl;
    }
    return 0;
  }

  int Insert(const std::string &table, const std::string &key,
             std::vector<KVPair> &values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      fprintf(stdout, "INSERT %s\n", key.c_str());
      fflush(stdout);
      // cout << "INSERT " << key << " [ ";
      // for (auto v : values) {
      //   cout << v.first << '=' << v.second << ' ';
      // }
      cout << ']' << endl;
    }
    return 0;
  }

  int BatchInsert(const std::string &table, std::vector<KVPair> &values, bool has_hot=true) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      fprintf(stdout, "BATCH INSERT\n");
      fflush(stdout);
      cout << ']' << endl;
    }
    return 0;
  }

  int Delete(const std::string &table, const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    cout << "DELETE " << key << endl;
    return 0; 
  }

  void Print() { }
  void PrintAllData(std::string, std::string, std::string) { }
  void PrintAllPages() { }
  void RemoveTree(bool concurrent=false) { }
  void AddTree(bool add) { }
  int Scan(const std::string &table, const std::string &key,
           const std::string &k2, const std::vector<std::string> *fields,
           std::vector<std::vector<KVPair>> &result, int select) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (print_key) {
      fprintf(stdout, "SCAN %s\n", key.c_str());
      fflush(stdout);
      // cout << "SCAN " << key << " " << k2 << std::endl;;
      // if (fields) {
      //   cout << " [ ";
      //   for (auto f : *fields) {
      //     cout << f << ' ';
      //   }
      //   cout << ']' << endl;
      // } else {
      //   cout  << " < all fields >" << endl;
      // }
    }
    return 0;
  }
  void EnableTreeInsertion() {}
  void EnableBufferInsertion() {}
  // void SetHotspotStart(const std::string &start) { hot_queried_start_ = start; }
  // void SetHotspotEnd(const std::string &end) { hot_queried_end_ = end; }
  void SetHotspots(const std::vector<std::pair<std::string,std::string>> &hotspot) {}

  bool BGCompactionIdle() { return true; }
  bool BGFlushIdle() { return true; }
  bool IsBtree() { return false; }
  bool IsLSMT() { return false; }
  int WriteBatchSize() { return 0; }

 private:
  std::mutex mutex_;
  bool print_key;
  // std::string hot_queried_start_;
  // std::string hot_queried_end_;
};

} // ycsbc

#endif // YCSB_C_BASIC_DB_H_

