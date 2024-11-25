#include <iostream>
#include "core/workload_monitor.h"
#include "core/core_workload.h"

using namespace ycsbc;

void test_pure_scan() {
  WorkloadMonitor monitor(3, 1, 10);
  Operation op = SCAN;
  for (int i = 0 ; i < 80; i++) {
    if (monitor.AddSampleAndMakeDecision(op)) {
      if (monitor.ScanHeavy()) {
        std::cout << i << "; We are scan\n";
      } else {
        std::cout << i << "; We are write\n";
      }
      monitor.FinishDecision();
    }
  }
}

void test_pure_insert() {
  WorkloadMonitor monitor(1, 0.5, 10);
  Operation op = INSERT;
  for (int i = 0 ; i < 80; i++) {
    if (monitor.AddSampleAndMakeDecision(op)) {
      if (monitor.ScanHeavy()) {
        std::cout << i << "; We are scan\n";
      } else if (monitor.WriteHeavy()) {
        std::cout << i << "; We are write\n";
      }
      monitor.FinishDecision();
    }
  }
}

void test_interleaved_ops() {
  WorkloadMonitor monitor(3, 1, 10, 0.9, 0.1);
  for (int i = 0 ; i < 80; i++) {
    Operation op;
    if (i % 2 == 0) {
      op = SCAN;
    } else {
      op = UPDATE;
    }
    if (monitor.AddSampleAndMakeDecision(op)) {
      std::cout << i << std::endl;
      if (monitor.ScanHeavy()) {
        std::cout << i << "; We are scan\n";
      } else if (monitor.WriteHeavy()) {
        std::cout << i << "; We are write\n";
      }
      monitor.FinishDecision();
    }
  }
}

int main(const int argc, const char *argv[]) {
  // test_pure_scan();
  // test_pure_insert();
  test_interleaved_ops();
  return 0;
}