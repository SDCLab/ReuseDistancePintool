/*
 * prefetcharbiter_test.cc
 *
 *  Created on: Nov 11, 2010
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "prefetcher.h"

class MockPrefetcher : public PrefetcherInterface {
public:
  MockPrefetcher(int idx) : index_(idx) {}
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write) {
    return index_;
  }
  /* Returns string containing stats */
  virtual std::string GetStatsString() { return std::string(); }
  virtual ~MockPrefetcher() {}
protected:
  int index_;
};

class ParrotMockPrefetcher : public MockPrefetcher {
public:
  ParrotMockPrefetcher() : MockPrefetcher(0) {}
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write) {
    return addr;
  }
};

class DCUArbTest : public testing::Test {
protected:
  DCUArbTest() {
    arb_.AddPrefetcher(new DCUPrefetcher());
  }
  PrefetchArbiter arb_;

};

//test that a request is generated with a single prefetcher
TEST(PrefetchArbiter, SinglePrefetch) {
  PrefetchArbiter arb;
  arb.AddPrefetcher(new MockPrefetcher(1));
  EXPECT_EQ(1, arb.Access(0, 0, 0, false));
}

//test that a duplicate only generates one request
TEST(PrefetchArbiter, DuplicatePrefetch) {
  PrefetchArbiter arb;
  arb.AddPrefetcher(new MockPrefetcher(1));
  EXPECT_EQ(1, arb.Access(0, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(0, 0, 0, false));
}

//test that first-inserted prefetcher overrides second
TEST(PrefetchArbiter, PrefetchOverride) {
  PrefetchArbiter arb;
  arb.AddPrefetcher(new MockPrefetcher(1));
  arb.AddPrefetcher(new MockPrefetcher(2));
  EXPECT_EQ(1, arb.Access(0, 0, 0, false));
}

// test that repeated references generate repeated prefetches the right distance apart
TEST(PrefetchArbiter, RepeatedPrefetch) {
  PrefetchArbiter arb;
  arb.AddPrefetcher(new ParrotMockPrefetcher());
  EXPECT_EQ(1, arb.Access(1, 0, 0, false));
  for (int i = 0; i < PrefetchArbiter::kPrefetchRepeatFrequency - 1; i++) {
    SCOPED_TRACE(i);
    if (i % 2 == 0) {
      EXPECT_EQ(i, arb.Access(i, 0, 0, false));
    } else {
      EXPECT_EQ(kAddressMax, arb.Access(1, 0, 0, false));
    }
  }
  EXPECT_EQ(1, arb.Access(1, 0, 0, false));
  // and the second one, just for good measure
  EXPECT_EQ(0, arb.Access(0, 0, 0, false));
}

// test that the prefetch is properly delayed
TEST(PrefetchArbiter, DelayedPrefetch) {
  PrefetchArbiter arb(4);
  arb.AddPrefetcher(new ParrotMockPrefetcher());
  EXPECT_EQ(kAddressMax, arb.Access(1, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(2, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(3, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(4, 0, 0, false));
  EXPECT_EQ(1, arb.Access(5, 0, 0, false));
}

// test that prefetch in queue gets canceled when a fetch hits
TEST(PrefetchArbiter, CanceledPrefetch) {
  PrefetchArbiter arb(4);
  arb.AddPrefetcher(new ParrotMockPrefetcher());
  EXPECT_EQ(kAddressMax, arb.Access(1, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(2, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(1, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(4, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb.Access(5, 0, 0, false));
}

// repeat DCU tests for arbiter
// test that sequential accesses never result in prefetches
TEST_F(DCUArbTest, SequentialAccesses) {
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, arb_.Access(i, 0, 0, false));
  }
}

// test that 2 accesses to the same block result in a prefetch
TEST_F(DCUArbTest, SinglePrefetch) {
  for (int i = 0; i < DCUPrefetcher::kRequestProximity; i++) {
    EXPECT_EQ(kAddressMax, arb_.Access(i, 0, 0, false));
  }
  EXPECT_EQ(7, arb_.Access(6, 0, 0, false));
}

// filtering functionality now resides in PrefetchArbiter
// test that 3 accesses to the same block result in only one prefetch
TEST_F(DCUArbTest, SinglePrefetch3Refs) {
  for (int i = 0; i < DCUPrefetcher::kRequestProximity - 2; i++) {
    EXPECT_EQ(kAddressMax, arb_.Access(i, 0, 0, false));
  }
  EXPECT_EQ(DCUPrefetcher::kRequestProximity - 3,
            arb_.Access(DCUPrefetcher::kRequestProximity - 4, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb_.Access(DCUPrefetcher::kRequestProximity + 4, 0, 0, false));
  EXPECT_EQ(kAddressMax, arb_.Access(DCUPrefetcher::kRequestProximity - 4, 0, 0, false));
}
