/*
 * prefetcher_test.cc
 *
 *  Created on: Oct 7, 2010
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "prefetcher.h"
#include "trace.h"

class DCUPrefetcherTest : public testing::Test {
protected:
  DCUPrefetcher pref_;
};

// test that sequential accesses never result in prefetches
TEST_F(DCUPrefetcherTest, SequentialAccesses) {
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, pref_.Access(i, 0, 0, false));
  }
}

// test that 2 accesses to the same block result in a prefetch
TEST_F(DCUPrefetcherTest, SinglePrefetch) {
  for (int i = 0; i < DCUPrefetcher::kRequestProximity; i++) {
    EXPECT_EQ(kAddressMax, pref_.Access(i, 0, 0, false));
  }
  EXPECT_EQ(7, pref_.Access(6, 0, 0, false));
}

// test that 2 accesses to the same block but too far apart, do not result in a prefetch
TEST_F(DCUPrefetcherTest, RepeatTooFarApart) {
  for (int i = 0; i < DCUPrefetcher::kRequestProximity + 1; i++) {
    EXPECT_EQ(kAddressMax, pref_.Access(i, 0, 0, false));
  }
  EXPECT_EQ(kAddressMax, pref_.Access(0, 0, 0, false));
}

/* test that 2 pairs of accesses to different blocks result in prefetches  */
TEST_F(DCUPrefetcherTest, PrefetchDistance) {
  for (int i = 0; i < DCUPrefetcher::kRequestProximity - 2; i++) {
    EXPECT_EQ(kAddressMax, pref_.Access(i, 0, 0, false));
  }
  EXPECT_EQ(DCUPrefetcher::kRequestProximity - 3,
            pref_.Access(DCUPrefetcher::kRequestProximity - 4, 0, 0, false));
  for (int i = 0; i < DCUPrefetcher::kRequestProximity - 4; i++) {
    EXPECT_EQ(kAddressMax, pref_.Access(i + 2 * DCUPrefetcher::kRequestProximity, 0, 0, false));
  }
  EXPECT_EQ(DCUPrefetcher::kRequestProximity - 4, pref_.Access(DCUPrefetcher::kRequestProximity - 5, 0, 0, false));
}

TEST_F(DCUPrefetcherTest, TraceTest) {
  InputTrace trace("../rddata/applu-10k_sampled-fulltrace");
  const TraceElement *element = trace.GetNext();
  int count = 10000;
  while (element && count) {
    while (element->thread != 0) element = trace.GetNext();
    count--;
    switch (element->type) {
    case TraceElement::kNewAddress:
      break;
    case TraceElement::kAccess:
      pref_.Access(element->address, 0, 0, false);
      break;
    case TraceElement::kMerge:
      // ignore merges for 1 thread
      break;
    case TraceElement::kEnable:
      break;
    default:
      FAIL() << "Bad trace element type";
    }
    element = trace.GetNext();
  }

}
