//
//  core_workload.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/9/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include "uniform_generator.h"
#include "normal_generator.h"
#include "zipfian_generator.h"
#include "zipf_generator_with_seed.h"
#include "scrambled_zipfian_generator.h"
#include "skewed_latest_generator.h"
#include "const_generator.h"
#include "hotspot_generator.h"
#include "core_workload.h"

#include <string>
#include <bits/stdc++.h>

using ycsbc::CoreWorkload;
using std::string;

const string CoreWorkload::TABLENAME_PROPERTY = "table";
const string CoreWorkload::TABLENAME_DEFAULT = "usertable";

const string CoreWorkload::FIELD_COUNT_PROPERTY = "fieldcount";
const string CoreWorkload::FIELD_COUNT_DEFAULT = "10";

// Lu
const string CoreWorkload::FIELD_COUNT_SEED_PROPERTY = "fieldcountseed";
const string CoreWorkload::FIELD_COUNT_SEED_DEFAULT = "9";

const string CoreWorkload::FIELD_LENGTH_DISTRIBUTION_PROPERTY =
    "field_len_dist";
const string CoreWorkload::FIELD_LENGTH_DISTRIBUTION_DEFAULT = "constant";

const string CoreWorkload::FIELD_LENGTH_PROPERTY = "fieldlength";
const string CoreWorkload::FIELD_LENGTH_DEFAULT = "100";

// Lu
const string CoreWorkload::FIELD_LENGTH_SEED_PROPERTY = "fieldlengthseed";
const string CoreWorkload::FIELD_LENGTH_SEED_DEFAULT = "10";

const string CoreWorkload::READ_ALL_FIELDS_PROPERTY = "readallfields";
const string CoreWorkload::READ_ALL_FIELDS_DEFAULT = "true";

const string CoreWorkload::WRITE_ALL_FIELDS_PROPERTY = "writeallfields";
const string CoreWorkload::WRITE_ALL_FIELDS_DEFAULT = "false";

const string CoreWorkload::REQUEST_DISTRIBUTION_PROPERTY =
    "requestdistribution";
const string CoreWorkload::REQUEST_DISTRIBUTION_DEFAULT = "uniform";

// allow seed
const string CoreWorkload::REQUEST_DISTR_SEED_PROPERTY = "requestseed";
const string CoreWorkload::REQUEST_DISTR_SEED_DEFAULT = "1";

const string CoreWorkload::ZERO_PADDING_PROPERTY = "zeropadding";
const string CoreWorkload::ZERO_PADDING_DEFAULT = "1";

// we can specify to use a fixed scan length
const string CoreWorkload::FIXED_SCAN_LENGTH_PROPERTY = "fixedcanlength";
const string CoreWorkload::FIXED_SCAN_LENGTH_DEFAULT = "0";

const string CoreWorkload::MAX_SCAN_LENGTH_PROPERTY = "maxscanlength";
const string CoreWorkload::MAX_SCAN_LENGTH_DEFAULT = "1000";

const string CoreWorkload::SCAN_LENGTH_DISTRIBUTION_PROPERTY =
    "scanlengthdistribution";
const string CoreWorkload::SCAN_LENGTH_DISTRIBUTION_DEFAULT = "uniform";

// allow seed
const string CoreWorkload::SCAN_LENGTH_SEED_PROPERTY = "scanlengthseed";
const string CoreWorkload::SCAN_LENGTH_SEED_DEFAULT = "2";

// allow sequentially ordered txn key
const string CoreWorkload::TXN_KEY_ORDER_PROPERTY = "txnkeyorder";
const string CoreWorkload::TXN_KEY_ORDER_DEFAULT = "hashed";

const string CoreWorkload::INSERT_ORDER_PROPERTY = "insertorder";
const string CoreWorkload::INSERT_ORDER_DEFAULT = "hashed";

const string CoreWorkload::INSERT_START_PROPERTY = "insertstart";
const string CoreWorkload::INSERT_START_DEFAULT = "0";

const string CoreWorkload::RECORD_COUNT_PROPERTY = "recordcount";
const string CoreWorkload::OPERATION_COUNT_PROPERTY = "operationcount";

const string CoreWorkload::OPS_ZIPF_SCALEOUT_FACTOR_PROPERTY = "zipf_scaleout_factor";
const string CoreWorkload::OPS_ZIPF_SCALEOUT_FACTOR_DEFAULT = "0.01";

const string CoreWorkload::INSERT_DISTRIBUTION_PROPERTY = "insertdistribution";
const string CoreWorkload::INSERT_DISTRIBUTION_DEFAULT = "uniform";

void CoreWorkload::Init(const utils::Properties &p) {
  table_name_ = p.GetProperty(TABLENAME_PROPERTY,TABLENAME_DEFAULT);
  
  field_count_ = std::stoi(p.GetProperty(FIELD_COUNT_PROPERTY,
                                         FIELD_COUNT_DEFAULT));
  // Lu
  uint64_t field_seed_ = std::stoi(p.GetProperty(FIELD_COUNT_SEED_PROPERTY,
                                                 FIELD_COUNT_SEED_DEFAULT));
  field_len_generator_ = GetFieldLenGenerator(p);
  
  record_count_ = std::stoi(p.GetProperty(RECORD_COUNT_PROPERTY));
  std::string request_dist = p.GetProperty(REQUEST_DISTRIBUTION_PROPERTY,
                                           REQUEST_DISTRIBUTION_DEFAULT);
  // Lu
  uint64_t request_seed = std::stoi(p.GetProperty(REQUEST_DISTR_SEED_PROPERTY,
                                                  REQUEST_DISTR_SEED_DEFAULT));

  zero_padding_ = std::stoi(p.GetProperty(ZERO_PADDING_PROPERTY, ZERO_PADDING_DEFAULT));
  // Lu
  fixed_scan_len_ = std::stoi(p.GetProperty(FIXED_SCAN_LENGTH_PROPERTY,
                                            FIXED_SCAN_LENGTH_DEFAULT));
  int max_scan_len = std::stoi(p.GetProperty(MAX_SCAN_LENGTH_PROPERTY,
                                             MAX_SCAN_LENGTH_DEFAULT));
  std::string scan_len_dist = p.GetProperty(SCAN_LENGTH_DISTRIBUTION_PROPERTY,
                                            SCAN_LENGTH_DISTRIBUTION_DEFAULT);
  // Lu
  uint64_t scan_seed = std::stoi(p.GetProperty(SCAN_LENGTH_SEED_PROPERTY,
                                               SCAN_LENGTH_SEED_DEFAULT));
  int insert_start = std::stoi(p.GetProperty(INSERT_START_PROPERTY,
                                             INSERT_START_DEFAULT));
  
  read_all_fields_ = utils::StrToBool(p.GetProperty(READ_ALL_FIELDS_PROPERTY,
                                                    READ_ALL_FIELDS_DEFAULT));
  write_all_fields_ = utils::StrToBool(p.GetProperty(WRITE_ALL_FIELDS_PROPERTY,
                                                     WRITE_ALL_FIELDS_DEFAULT));
  
  if (p.GetProperty(INSERT_ORDER_PROPERTY, INSERT_ORDER_DEFAULT) == "hashed") {
    ordered_inserts_ = false;
  } else {
    ordered_inserts_ = true;
  }

  if (p.GetProperty(TXN_KEY_ORDER_PROPERTY,TXN_KEY_ORDER_DEFAULT) == "hashed") {
    ordered_txn_keys_ = false;
  } else {
    ordered_txn_keys_ = true;
  }

  fixed_selectivity_ = std::stod(p.GetProperty("fixedselectivity", "0")) == 1 ? true : false;

  std::string insert_distr = p.GetProperty(INSERT_DISTRIBUTION_PROPERTY,
                                           INSERT_DISTRIBUTION_DEFAULT);
  int ins_scale_factor = std::stoi(p.GetProperty("insert_scale_factor",
                                                    "1000"));
  if (insert_distr == "uniform") {
    // key_generator_ = new CounterGenerator(insert_start);
    key_generator_ = new UniformGenerator(0, record_count_ * ins_scale_factor - 1, request_seed);
  } else if (insert_distr == "zipfian_seed") {
    key_generator_ = new ZipfianGeneratorSeed(0, record_count_ * ins_scale_factor, request_seed);
  }
  
  insert_key_sequence_.Set(record_count_);
  op_count_ = std::stoi(p.GetProperty(OPERATION_COUNT_PROPERTY));

  double ops_zipf_scaleout = std::stod(p.GetProperty(OPS_ZIPF_SCALEOUT_FACTOR_PROPERTY, OPS_ZIPF_SCALEOUT_FACTOR_DEFAULT));

  // scan_hotspot_ratio_ = std::stod(p.GetProperty("scan_hot", "1.0"));
  // update_hotspot_ratio_ = std::stod(p.GetProperty("update_hot", "1.0"));
  // double hotspot = std::stod(p.GetProperty("hotspot", "0.1"));

  if (request_dist == "uniform") {
    key_chooser_ = new UniformGenerator(0, record_count_ * ins_scale_factor - 1, request_seed);
  } /*else if (request_dist == "zipfian") {
    // If the number of keys changes, we don't want to change popular keys.
    // So we construct the scrambled zipfian generator with a keyspace
    // that is larger than what exists at the beginning of the test.
    // If the generator picks a key that is not inserted yet, we just ignore it
    // and pick another key.
    int op_count = std::stoi(p.GetProperty(OPERATION_COUNT_PROPERTY));
    int new_keys = (int)(op_count * insert_proportion * 2); // a fudge factor
    key_chooser_ = new ScrambledZipfianGenerator(record_count_ + new_keys);
    
  }*/ else if (request_dist == "normal") {
    // Mean value is by default record_count_ * ins_scale_factor / 2
    // Standard deviation is calculated by the given z score. Default: 1.645
    double normal_mean = std::stod(p.GetProperty("normal_mean", "0"));
    if (normal_mean <= 0) normal_mean = record_count_ * ins_scale_factor / 2;
    double normal_confidence_level = std::stod(p.GetProperty("normal_confidence_level", "0.9"));
    double std;
  
    if (normal_confidence_level == 0.9) {
      std = (normal_mean) / 1.645;
    } else if (normal_confidence_level == 0.95) {
      std = (normal_mean) / 1.96;
    } else if (normal_confidence_level == 0.99) {
      std = (normal_mean) / 2.576;
    } else {
      std = (normal_mean) / 1.645;
    }
    key_chooser_ = new NormalGenerator(normal_mean, std, request_seed);
  } /*else if (request_dist == "latest") {
    key_chooser_ = new SkewedLatestGenerator(insert_key_sequence_);
    
  }*/ else if (request_dist == "zipfian_seed") {
    // int op_count = std::stoi(p.GetProperty(OPERATION_COUNT_PROPERTY));
    // int new_keys = (int)(op_count * insert_proportion * 2);
    uint64_t zipf_start = std::stoi(p.GetProperty("zipf_start", "0"));
    uint64_t zipf_end = record_count_ * ins_scale_factor;// std::stoi(p.GetProperty("zipf_end", "1000"));
    key_chooser_ = new ZipfianGeneratorSeed(zipf_start, zipf_end, request_seed);
  } else if (request_dist.substr(0, 8) == "hotspot-") {
    int hotspot_num = std::stoi(request_dist.substr(8));
    double hot_data_percent = std::stod(p.GetProperty("hotdatapercent", "0.1"));
    // double hot_op_percent = std::stod(p.GetProperty("hotoperationpercent", "0.2"));
    key_chooser_ = new HotspotGenerator(0, record_count_ * ins_scale_factor - 1, request_seed,
                                        hot_data_percent, hotspot_num);
    fprintf(stdout, "%d hotspots:\n", hotspot_num);
    // SetHotspotStart(BuildKeyName(0));
    // We need to consider ranges that are not covered by the hotspot
    uint64_t scan_len = fixed_scan_len_ == 0 ? max_scan_len : fixed_scan_len_;
    auto gen = dynamic_cast<HotspotGenerator*>(key_chooser_);
    assert(gen != nullptr);
    for (int i = 0; i < hotspot_num; i++) {
      uint64_t hs = gen->GetHotspotStart(i);
      uint64_t he = gen->GetHotspotEnd(i);
      fprintf(stdout, "\thotspot #%d: %lu -> %lu\n", i, hs, he);
      AppendHotspot(BuildKeyName(hs), BuildKeyName(he + scan_len));
    }
    // SetHotspotEnd(BuildKeyName(
    //     (uint64_t)record_count_ * ins_scale_factor * hot_data_percent + scan_len));
  } else {
    throw utils::Exception("Unknown request distribution: " + request_dist);
  }
  
  field_chooser_ = new UniformGenerator(0, field_count_ - 1, field_seed_);
  
  // TODO: zipf distribution also needs a seed
  if (fixed_scan_len_ == 0) {
    if (scan_len_dist == "uniform") {
      scan_len_chooser_ = new UniformGenerator(1, max_scan_len, scan_seed);
    } else if (scan_len_dist == "zipfian") {
      scan_len_chooser_ = new ZipfianGenerator(1, max_scan_len);
    } else {
      throw utils::Exception("Distribution not allowed for scan length: " +
          scan_len_dist);
    }
  } else {
    scan_len_chooser_ = new ConstGenerator(fixed_scan_len_);
  }
    
}

ycsbc::Generator<uint64_t> *CoreWorkload::GetFieldLenGenerator(
    const utils::Properties &p) {
  string field_len_dist = p.GetProperty(FIELD_LENGTH_DISTRIBUTION_PROPERTY,
                                        FIELD_LENGTH_DISTRIBUTION_DEFAULT);
  int field_len = std::stoi(p.GetProperty(FIELD_LENGTH_PROPERTY,
                                          FIELD_LENGTH_DEFAULT));
  uint64_t field_seed = std::stoi(p.GetProperty(FIELD_LENGTH_SEED_PROPERTY,
                                                FIELD_LENGTH_SEED_DEFAULT));
  if(field_len_dist == "constant") {
    return new ConstGenerator(field_len);
  } else if(field_len_dist == "uniform") {
    return new UniformGenerator(1, field_len, field_seed);
  } else if(field_len_dist == "zipfian") {
    return new ZipfianGenerator(1, field_len);
  } else {
    throw utils::Exception("Unknown field length distribution: " +
        field_len_dist);
  }
}

void CoreWorkload::BuildValues(std::vector<ycsbc::DB::KVPair> &values) {
  for (int i = 0; i < field_count_; ++i) {
    ycsbc::DB::KVPair pair;
    pair.first.append("field").append(std::to_string(i));
    pair.second.append(field_len_generator_->Next(), rand_dev_->RandomChar()/*utils::RandomPrintChar()*/);
    values.push_back(pair);
  }
}

std::string CoreWorkload::BuildValue() {
  std::string value;
  value.append(field_len_generator_->Next(), rand_dev_->RandomChar()/*utils::RandomPrintChar()*/);
  return value;
}

void CoreWorkload::BuildUpdate(std::vector<ycsbc::DB::KVPair> &update) {
  ycsbc::DB::KVPair pair;
  pair.first.append(NextFieldName());
  pair.second.append(field_len_generator_->Next(), rand_dev_->RandomChar()/*utils::RandomPrintChar()*/);
  update.push_back(pair);
}

