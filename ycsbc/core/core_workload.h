//
//  core_workload.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/9/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_CORE_WORKLOAD_H_
#define YCSB_C_CORE_WORKLOAD_H_

#include <vector>
#include <string>
#include <iostream>
#include "db.h"
#include "properties.h"
#include "generator.h"
#include "discrete_generator.h"
#include "counter_generator.h"
#include "utils.h"
#include "uniform_generator.h"

namespace ycsbc {

class CoreWorkload {
 public:
  /// 
  /// The name of the database table to run queries against.
  ///
  static const std::string TABLENAME_PROPERTY;
  static const std::string TABLENAME_DEFAULT;
  
  /// 
  /// The name of the property for the number of fields in a record.
  ///
  static const std::string FIELD_COUNT_PROPERTY;
  static const std::string FIELD_COUNT_DEFAULT;
  
  /// 
  /// The name of the property for the field length distribution.
  /// Options are "uniform", "zipfian" (favoring short records), and "constant".
  ///
  static const std::string FIELD_LENGTH_DISTRIBUTION_PROPERTY;
  static const std::string FIELD_LENGTH_DISTRIBUTION_DEFAULT;
  
  /// 
  /// The name of the property for the length of a field in bytes.
  ///
  static const std::string FIELD_LENGTH_PROPERTY;
  static const std::string FIELD_LENGTH_DEFAULT;
  
  /// 
  /// The name of the property for deciding whether to read one field (false)
  /// or all fields (true) of a record.
  ///
  static const std::string READ_ALL_FIELDS_PROPERTY;
  static const std::string READ_ALL_FIELDS_DEFAULT;

  /// 
  /// The name of the property for deciding whether to write one field (false)
  /// or all fields (true) of a record.
  ///
  static const std::string WRITE_ALL_FIELDS_PROPERTY;
  static const std::string WRITE_ALL_FIELDS_DEFAULT;
  
  /// 
  /// The name of the property for the proportion of read transactions.
  ///
  static const std::string READ_PROPORTION_PROPERTY;
  static const std::string READ_PROPORTION_DEFAULT;
  
  /// 
  /// The name of the property for the proportion of update transactions.
  ///
  static const std::string UPDATE_PROPORTION_PROPERTY;
  static const std::string UPDATE_PROPORTION_DEFAULT;
  
  /// 
  /// The name of the property for the proportion of insert transactions.
  ///
  static const std::string INSERT_PROPORTION_PROPERTY;
  static const std::string INSERT_PROPORTION_DEFAULT;
  
  /// 
  /// The name of the property for the proportion of scan transactions.
  ///
  static const std::string SCAN_PROPORTION_PROPERTY;
  static const std::string SCAN_PROPORTION_DEFAULT;
  
  ///
  /// The name of the property for the proportion of
  /// read-modify-write transactions.
  ///
  static const std::string READMODIFYWRITE_PROPORTION_PROPERTY;
  static const std::string READMODIFYWRITE_PROPORTION_DEFAULT;
  
  /// 
  /// The name of the property for the the distribution of request keys.
  /// Options are "uniform", "zipfian" and "latest".
  ///
  static const std::string REQUEST_DISTRIBUTION_PROPERTY;
  static const std::string REQUEST_DISTRIBUTION_DEFAULT;
  
  ///
  /// The name of the property for adding zero padding to record numbers in order to match 
  /// string sort order. Controls the number of 0s to left pad with.
  ///
  static const std::string ZERO_PADDING_PROPERTY;
  static const std::string ZERO_PADDING_DEFAULT;

  /// 
  /// The name of the property for the max scan length (number of records).
  ///
  static const std::string MAX_SCAN_LENGTH_PROPERTY;
  static const std::string MAX_SCAN_LENGTH_DEFAULT;
  
  /// 
  /// The name of the property for the scan length distribution.
  /// Options are "uniform" and "zipfian" (favoring short scans).
  ///
  static const std::string SCAN_LENGTH_DISTRIBUTION_PROPERTY;
  static const std::string SCAN_LENGTH_DISTRIBUTION_DEFAULT;

  /// 
  /// The name of the property for the order to insert records.
  /// Options are "ordered" or "hashed".
  ///
  static const std::string INSERT_ORDER_PROPERTY;
  static const std::string INSERT_ORDER_DEFAULT;

  static const std::string INSERT_START_PROPERTY;
  static const std::string INSERT_START_DEFAULT;
  
  static const std::string RECORD_COUNT_PROPERTY;
  static const std::string OPERATION_COUNT_PROPERTY;

  // Lu
  static const std::string FIELD_COUNT_SEED_PROPERTY;
  static const std::string FIELD_COUNT_SEED_DEFAULT;
  static const std::string FIELD_LENGTH_SEED_PROPERTY;
  static const std::string FIELD_LENGTH_SEED_DEFAULT;
  static const std::string REQUEST_DISTR_SEED_PROPERTY;
  static const std::string REQUEST_DISTR_SEED_DEFAULT;
  static const std::string FIXED_SCAN_LENGTH_PROPERTY;
  static const std::string SCAN_LENGTH_SEED_PROPERTY;
  static const std::string SCAN_LENGTH_SEED_DEFAULT;
  static const std::string FIXED_SCAN_LENGTH_DEFAULT;
  static const std::string TXN_KEY_ORDER_PROPERTY;
  static const std::string TXN_KEY_ORDER_DEFAULT;
  static const std::string OPS_ZIPF_SCALEOUT_FACTOR_PROPERTY;
  static const std::string OPS_ZIPF_SCALEOUT_FACTOR_DEFAULT;
  static const std::string INSERT_DISTRIBUTION_PROPERTY;
  static const std::string INSERT_DISTRIBUTION_DEFAULT;


  ///
  /// Initialize the scenario.
  /// Called once, in the main client thread, before any operations are started.
  ///
  virtual void Init(const utils::Properties &p);
  
  virtual void BuildValues(std::vector<ycsbc::DB::KVPair> &values);
  virtual void BuildUpdate(std::vector<ycsbc::DB::KVPair> &update);
  virtual std::string BuildValue();
  
  virtual std::string NextTable() { return table_name_; }
  virtual std::string NextSequenceKey(); /// Used for loading data
  virtual std::string NextTransactionKey(); /// Used for transactions
  virtual std::pair<std::string, std::string> NextTransactionKeys();
  virtual std::string PrependStringToKey(uint64_t key_num);
  virtual Operation NextOperation() { return op_chooser_.Next(); }
  virtual std::string NextFieldName();
  // allow fixed scan length
  virtual size_t NextScanLength() { return scan_len_chooser_->Next(); }
  virtual uint64_t GetBoundedHashKey(uint64_t key);
  
  bool read_all_fields() const { return read_all_fields_; }
  bool write_all_fields() const { return write_all_fields_; }

  CoreWorkload() :
      field_count_(0), read_all_fields_(false), write_all_fields_(false),
      field_len_generator_(NULL), key_generator_(NULL), key_chooser_(NULL),
      field_chooser_(NULL), scan_len_chooser_(NULL), insert_key_sequence_(3),
      ordered_inserts_(true), record_count_(0),
      rand_dev_(new utils::RandomDevice(200)) {
  }
  
  virtual ~CoreWorkload() {
    if (field_len_generator_) delete field_len_generator_;
    if (key_generator_) delete key_generator_;
    if (key_chooser_) delete key_chooser_;
    if (field_chooser_) delete field_chooser_;
    if (scan_len_chooser_) delete scan_len_chooser_;
  }

  DiscreteGenerator<Operation> op_chooser_;
  Generator<uint64_t> *key_chooser_;
  uint64_t hotspot_num_ = 1;

  // only when the workload is zipfian
  virtual void NextHotspot();
  void SetHotspotStart(const std::string &start) { hot_queried_start_ = start; }
  void SetHotspotEnd(const std::string &end) { hot_queried_end_ = end; }

  std::string GetLowestKey() {
    return BuildKeyName(0);
  }

  std::string GetHighestKey() {
    return BuildKeyName(key_chooser_->Maxdata());
  }
  
  void ResetSeed() {
    // key_generator_->ResetSeed();
    op_chooser_.ResetSeed();
    key_chooser_->ResetSeed();
    // scan_len_chooser_->ResetSeed();
  }

  int GetRangeScanSelectivity() {
    if (fixed_selectivity_) {
      return fixed_scan_len_;
    }
    return -1;
  }

  utils::RandomDevice* rand_dev_;
  std::string hot_queried_start_;
  std::string hot_queried_end_;
  
 protected:
  static Generator<uint64_t> *GetFieldLenGenerator(const utils::Properties &p);
  std::string BuildKeyName(uint64_t key_num);

  std::string table_name_;
  int field_count_;
  bool read_all_fields_;
  bool write_all_fields_;
  Generator<uint64_t> *field_len_generator_;
  Generator<uint64_t> *key_generator_;
  
  
  Generator<uint64_t> *field_chooser_;
  Generator<uint64_t> *scan_len_chooser_;
  CounterGenerator insert_key_sequence_;
  bool ordered_inserts_;
  size_t record_count_;
  size_t op_count_;
  int zero_padding_;
  std::atomic<uint64_t> prev_key_;
  // Lu:
  uint64_t fixed_scan_len_;
  bool ordered_txn_keys_;
  bool fixed_selectivity_;
  // uint64_t cur_hotspot_ = 0;
  // uint64_t zipf_right_limit_;
};

inline void CoreWorkload::NextHotspot() {
  // uint64_t gap = zipf_right_limit_ / hotspot_num_;
  // cur_hotspot_ += gap;
  // if (cur_hotspot_ >= zipf_right_limit_) {
  //   cur_hotspot_ = 0;
  // }
  // std::cout << "Changing hotspot\n";
}

inline std::string CoreWorkload::NextSequenceKey() {
  uint64_t key_num = key_generator_->Next();
  return BuildKeyName(key_num);
}

inline std::string CoreWorkload::NextTransactionKey() {
  uint64_t key_num;
  key_num = (key_chooser_->Next()) % (key_generator_->Maxdata() + 1);
  prev_key_.store(key_num);
  return BuildKeyName(key_num);
}

inline uint64_t CoreWorkload::GetBoundedHashKey(uint64_t key) {
  uint64_t hashed = utils::Hash(key);
  return hashed % (record_count_ + op_count_);
}

inline std::string CoreWorkload::BuildKeyName(uint64_t key_num) {
  if (!ordered_inserts_) {
    key_num = GetBoundedHashKey(key_num);// utils::Hash(key_num);
  }
  return PrependStringToKey(key_num);
}

inline std::string CoreWorkload::NextFieldName() {
  return std::string("field").append(std::to_string(field_chooser_->Next()));
}

inline std::string CoreWorkload::PrependStringToKey(uint64_t key_num) {
  std::string key_num_str = std::to_string(key_num);
  int zeros = zero_padding_ - key_num_str.length();
  zeros = std::max(0, zeros);
  return std::string(zeros, '0').append(key_num_str);
}

// Scans only
inline std::pair<std::string, std::string> CoreWorkload::NextTransactionKeys() {
  uint64_t key_num1, key_num2;
  key_num1 = (key_chooser_->Next()) % (key_generator_->Maxdata() + 1);
  if (fixed_selectivity_) {
    return std::make_pair(BuildKeyName(key_num1), "");
  }
  uint64_t next_key = key_num1;
  int len = NextScanLength();
  if (!ordered_inserts_) {
    next_key = GetBoundedHashKey(key_num1);
  }
  key_num2 = next_key + len;
  return std::make_pair(BuildKeyName(key_num1), PrependStringToKey(key_num2));
}
  
} // ycsbc

#endif // YCSB_C_CORE_WORKLOAD_H_
