//
//  basic_db.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include "db_factory.h"

#include <string>
#include "basic_db.h"
#include "btree_db.h"
#include "wotdb.h"
#include "level_db.h"
#include "buffer_tree_db.h"

using namespace std;

ycsbc::DB* ycsbc::DBFactory::CreateDB(
        std::map<std::string, std::string>* options, utils::Properties &props) {
  if ((*options)["dbname"] == "basic") {
    return new ycsbc::BasicDB(options);
  } /** else if (props["dbname"] == "lock_stl") {
    return new LockStlDB;
  } else if (props["dbname"] == "redis") {
    int port = stoi(props["port"]);
    int slaves = stoi(props["slaves"]);
    return new RedisDB(props["host"].c_str(), port, slaves);
  } else if (props["dbname"] == "tbb_rand") {
    return new TbbRandDB;
  } else if (props["dbname"] == "tbb_scan") {
    return new TbbScanDB;
  } else if (props["dbname"] == "be_tree") {
    return new Betree(props["dir"], 
                      stod(props.GetProperty("epsilon", "0.9")), 
                      // stoi(props.GetProperty("node", "64")), 
                      stoi(props.GetProperty("pivot", "64")), 
                      stoi(props.GetProperty("buffer", "64")), 
                      stoi(props.GetProperty("cache", "4")),
                      stoi(props.GetProperty("flushall", "0")));
  } */ else if ((*options)["dbname"] == "wot") {
    return new ycsbc::WotDB(options);
  } else if ((*options)["dbname"] == "btree") {
    return new ycsbc::BtreeDB(options);
  } else if ((*options)["dbname"] == "leveldb") {
    return new ycsbc::LeveldbYcsb(options);
#ifdef INCLUDE_BUFFERTREE
  } else if ((*options)["dbname"] == "buffertree") {
    return new ycsbc::BufferTreeDB(options);
#endif
  } else return NULL;
}

