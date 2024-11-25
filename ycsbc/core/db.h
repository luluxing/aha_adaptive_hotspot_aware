//
//  db.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/10/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_DB_H_
#define YCSB_C_DB_H_

#include <vector>
#include <string>

namespace ycsbc {

class DB {
 public:
  typedef std::pair<std::string, std::string> KVPair;
  static const int kOK = 0;
  static const int kErrorNoData = 1;
  static const int kErrorConflict = 2;
  ///
  /// Initializes any state for accessing this DB.
  /// Called once per DB client (thread); there is a single DB instance globally.
  ///
  virtual void Init() { }
  ///
  /// Clears any state for accessing this DB.
  /// Called once per DB client (thread); there is a single DB instance globally.
  ///
  virtual void Close() { }
  ///
  /// Reads a record from the database.
  /// Field/value pairs from the result are stored in a vector.
  ///
  /// @param table The name of the table.
  /// @param key The key of the record to read.
  /// @param fields The list of fields to read, or NULL for all of them.
  /// @param result A vector of field/value pairs for the result.
  /// @return Zero on success, or a non-zero error code on error/record-miss.
  ///
  virtual int Read(const std::string &table, const std::string &key,
                   const std::vector<std::string> *fields,
                   std::vector<KVPair> &result) = 0;
  ///
  /// Performs a range scan for a set of records in the database.
  /// Field/value pairs from the result are stored in a vector.
  ///
  /// @param table The name of the table.
  /// @param key The key of the first record to read.
  /// @param record_count The number of records to read.
  /// @param fields The list of fields to read, or NULL for all of them.
  /// @param result A vector of vector, where each vector contains field/value
  ///        pairs for one record
  ///
  virtual int Scan(const std::string &table, const std::string &key,
                   int record_count, const std::vector<std::string> *fields,
                   std::vector<std::vector<KVPair>> &result, int select=-1) = 0;

  virtual int Scan(const std::string &table, const std::string &key,
                   const std::string &key2, const std::vector<std::string> *fields,
                   std::vector<std::vector<KVPair>> &result, int select=-1) = 0;
  ///
  /// Updates a record in the database.
  /// Field/value pairs in the specified vector are written to the record,
  /// overwriting any existing values with the same field names.
  ///
  /// @param table The name of the table.
  /// @param key The key of the record to write.
  /// @param values A vector of field/value pairs to update in the record.
  /// @return Zero on success, a non-zero error code on error.
  ///
  virtual int Update(const std::string &table, const std::string &key,
                     std::vector<KVPair> &values) = 0;
  virtual int BatchUpdate(const std::string &table,
                     std::vector<KVPair> &values) = 0;
  
  ///
  /// Inserts a record into the database.
  /// Field/value pairs in the specified vector are written into the record.
  ///
  /// @param table The name of the table.
  /// @param key The key of the record to insert.
  /// @param values A vector of field/value pairs to insert in the record.
  /// @return Zero on success, a non-zero error code on error.
  ///
  virtual int Insert(const std::string &table, const std::string &key,
                     std::vector<KVPair> &values) = 0;
  virtual int BatchInsert(const std::string &table,
                     std::vector<KVPair> &values, bool has_hot=true) = 0;
  ///
  /// Deletes a record from the database.
  ///
  /// @param table The name of the table.
  /// @param key The key of the record to delete.
  /// @return Zero on success, a non-zero error code on error.
  ///
  virtual int Delete(const std::string &table, const std::string &key) = 0;

  virtual void Print() = 0;
  virtual void PrintAllPages() = 0;
  virtual void EnableTreeInsertion() = 0;
  virtual void EnableBufferInsertion() = 0;

  virtual void RemoveTree(bool concurrent) = 0;
  virtual void AddTree(bool add) = 0;
  // virtual void SetHotspotStart(const std::string &start) = 0;
  // virtual void SetHotspotEnd(const std::string &end) = 0;
  virtual void SetHotspots(const std::vector<std::pair<std::string,std::string>> &hotspot) = 0;

  virtual bool BGCompactionIdle() = 0;
  virtual bool BGFlushIdle() = 0;
  virtual bool IsBtree() = 0;
  virtual bool IsLSMT() = 0;

  virtual int WriteBatchSize() = 0;

  virtual void PrintAllData(std::string, std::string, std::string) = 0;
  
  virtual ~DB() { }
};

} // ycsbc

#endif // YCSB_C_DB_H_
