#include "../ycsbc/core/hotspot_generator.h"
#include <gtest/gtest.h>

using namespace ycsbc;


uint64_t min_val = 0;
uint64_t max_val = 400000;
double hotdata_percent = 0.4;
int hotnum = 4;

class MultiHotspotTest : public testing::Test {
 protected:
  void SetUp() override {
    gen_ = new HotspotGenerator(/*min*/min_val, /*max*/max_val, /*seed*/1,
                                /*hotdata%*/hotdata_percent, /*hotnum*/hotnum);
    gen_->SetHotopPercent(1.0);
  }

  void TearDown() override {
    delete gen_;
  }

  HotspotGenerator* gen_;
};

TEST_F(MultiHotspotTest, TestHotRange) {
  uint64_t hotlen = (max_val - min_val + 1) * hotdata_percent;
  uint64_t hotinterval = hotlen / hotnum;
  uint64_t coldlen = (max_val - min_val + 1) - hotlen;
  uint64_t coldinterval = coldlen / hotnum;
  for (int h = 0; h < hotnum; h++) {
    uint64_t hot_start = min_val + h * (hotinterval + coldinterval);
    uint64_t hot_end = hot_start + hotinterval - 1;
    fprintf(stdout, "hot_start: %lu, hot_end: %lu\n", hot_start, hot_end);
    for (uint64_t x = hot_start + 1; x < hot_end; x++) {
      EXPECT_TRUE(gen_->IsHot(x));
    }
  }
}

TEST_F(MultiHotspotTest, TestColdRange) {
  uint64_t hotlen = (max_val - min_val + 1) * hotdata_percent;
  uint64_t hotinterval = hotlen / hotnum;
  uint64_t coldlen = (max_val - min_val + 1) - hotlen;
  uint64_t coldinterval = coldlen / hotnum;
  for (int h = 0; h < hotnum; h++) {
    uint64_t cold_start = min_val + h * (hotinterval + coldinterval) + hotinterval - 1;
    uint64_t cold_end = cold_start + coldinterval - 1;
    fprintf(stdout, "cold_start: %lu, cold_end: %lu\n", cold_start, cold_end);
    for (uint64_t x = cold_start + 1; x < cold_end; x++) {
      EXPECT_TRUE(gen_->IsCold(x));
    }
  }
}

TEST_F(MultiHotspotTest, TestGetHot) {
  int test_num = 10000;
  int cnt = 0;
  for (int i = 0; i < test_num; i++) {
    uint64_t n = gen_->Next();
    EXPECT_TRUE(gen_->IsHot(n));
    if (n > (min_val + max_val) / 2) cnt++;
  }
  fprintf(stdout, "%d\n", cnt);
}