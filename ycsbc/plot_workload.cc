#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <future>
#include <thread>
#include "core/utils.h"
#include "core/timer.h"
#include "core/client.h"
#include "core/core_workload.h"
#include "db/db_factory.h"

using namespace std;


bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}

void ParseCommandLine(int argc, const char *argv[],
                      utils::Properties &props) {
  int argindex = 1;
  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-workload") == 0) {
      argindex++;
      // filename.assign(argv[argindex]);
      ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const string &message) {
        std::cout << message << endl;
        exit(0);
      }
      input.close();
      argindex++;
    } else {
      std::cout << "Unknown option '" << argv[argindex] << "'" << std::endl;
      exit(0);
    }
  }
}


int main(const int argc, const char *argv[]) {
  utils::Properties props;
  ParseCommandLine(argc, argv, props);

  ycsbc::CoreWorkload wl;
  wl.Init(props);

  int constr_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
  for (int i = 0; i < constr_ops; i++) {
    // std::cout << wl.NextSequenceKey() << "\n";
    wl.NextSequenceKey();
  }
  // std::cout << "===============\n";
  int txn_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
  for (int i = 0; i < txn_ops; i++) {
    std::cout << wl.NextTransactionKey() << "\n";
  }
  wl.NextHotspot();
  for (int i = 0; i < txn_ops; i++) {
    std::cout << wl.NextTransactionKey() << "\n";
  }
  wl.NextHotspot();
  for (int i = 0; i < txn_ops; i++) {
    std::cout << wl.NextTransactionKey() << "\n";
  }
}
