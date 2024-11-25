#include "wot_page_split_policy.h"
#include "gtest/gtest.h"

using namespace WOT_NAMESPACE;

namespace {

class PageSplitTest : public testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override {}

};

TEST_F(PageSplitTest, EvenSplitTest) {
  auto policy = new EvenSplitPolicy(28, 128);
  policy->Init(1000);
  for (int i = 0; i < 13; i++) {
    bool ret = policy->ShouldAllocPage(48);
    ASSERT_TRUE(ret);
  }
  ASSERT_TRUE(policy->ShouldAllocPage(47));
}

TEST_F(PageSplitTest, SoundRemedySplitTest) {
  auto policy = new SoundRemedySplitPolicy(28, 128);
  policy->Init(100);
  for (int i = 0; i < 41; i++) {
    bool ret = policy->ShouldAllocPage(i);
    ASSERT_FALSE(ret);
  }
}

}  // namespace