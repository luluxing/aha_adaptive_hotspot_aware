// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef BUILDER_FROM_LEVELDB_WOT_NAMESPACE_H_
#define BUILDER_FROM_LEVELDB_WOT_NAMESPACE_H_

#include <atomic>
#include <string>

#include "leveldb/dbformat.h"
#include "leveldb/include/status.h"

namespace WOT_NAMESPACE {

struct Options;
struct FileMetaData;
class Env;
class Iterator;
class TableCache;
class VersionEdit;

// copied from version_edit.h
struct FileMetaData {
  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) {}
  // copy constructor
  FileMetaData(const FileMetaData& other) 
  : refs(other.refs), 
    allowed_seeks(other.allowed_seeks.load()),
    file_size(other.file_size),
    number(other.number),
    file_entry(other.file_entry),
    smallest(other.smallest),
    largest(other.largest) {}

  int refs;
  std::atomic<int> allowed_seeks;  // Seeks allowed until compaction
  uint64_t number;
  uint64_t file_entry;    // file size in kv pairs
  uint64_t file_size;    // File size in bytes
  InternalKey smallest;  // Smallest internal key served by table
  InternalKey largest;   // Largest internal key served by table
};

// Build a Table file from the contents of *iter.  The generated file
// will be named according to meta->number.  On success, the rest of
// *meta will be filled with metadata about the generated table.
// If no data is present in *iter, meta->file_size will be set to
// zero, and no Table file will be produced.
Status BuildTable(const std::string& dbname, Env* env, 
                  const Options& opt,TableCache* table_cache,
                  Iterator* iter, FileMetaData* meta);

}  // namespace WOT_NAMESPACE

#endif