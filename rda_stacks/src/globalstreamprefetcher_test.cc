/*
 * globalstreamprefetcher_test.cc
 *
 *  Created on: Nov 15, 2010
 *      Author: Derek
 */

#include <gtest/gtest.h>
#include "globalstreamprefetcher.h"

const acc_count_t kLongDistance = 1024 * 1024 * 10;//something long enough to always be a miss

// test that streams with nothing within 4 cache lines never results in prefetch
TEST(GlobalStreamPrefetcher, NoProximity) {
  GlobalStreamPrefetcher pref;
  for (int i = 0; i < 100; i++) {
      EXPECT_EQ(kAddressMax, pref.Access(i * 5, i, 10, false));
    }
}

//test that short distances never result in prefetches
TEST(GlobalStreamPrefetcher, ShortDistances) {
  GlobalStreamPrefetcher pref;
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, pref.Access(i, 1, 10, false));
  }
}

//test that zero strides never result in prefetches
TEST(GlobalStreamPrefetcher, ZeroStride) {
  GlobalStreamPrefetcher pref;
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, pref.Access(1, i, kLongDistance, false));
  }
}

//test that streams with various strides and result in prefetches
TEST(GlobalStreamPrefetcher, Strides) {
  for (int stride = -4; stride <= 4; stride++) {
    GlobalStreamPrefetcher pref;
    SCOPED_TRACE(stride);
    if (stride == 0) continue;
    for (int i = 0; i < 50; i++) {
      EXPECT_EQ(i > 1 ? i * stride + stride + 1000: kAddressMax, pref.Access(i * stride + 1000, i, kLongDistance, false));
    }
  }
}

//test that alternating strides work
TEST(GlobalStreamPrefetcher, AlternatingStrides) {
  GlobalStreamPrefetcher pref;
  EXPECT_EQ(kAddressMax, pref.Access(11, 1, kLongDistance, false));
  EXPECT_EQ(kAddressMax, pref.Access(10, 1, kLongDistance, false));
  EXPECT_EQ(kAddressMax, pref.Access(11, 1, kLongDistance, false));
  for (int i = 0; i < 100; i++) {
    SCOPED_TRACE(i);
    EXPECT_EQ(10 + !(i % 2), pref.Access(10 + (i % 2), i, kLongDistance, false));
  }
}

//test that streams with various strides and result in prefetches
TEST(GlobalStreamPrefetcher, InterleavedStrides) {
  for (int stride = -4; stride <= 4; stride++) {
    GlobalStreamPrefetcher pref;
    SCOPED_TRACE(stride);
    if (stride == 0) continue;
    for (int i = 0; i < 50; i++) {
      EXPECT_EQ(i > 1 ? i * stride + stride + 1000: kAddressMax, pref.Access(i * stride + 1000, i, kLongDistance, false));
      EXPECT_EQ(i > 1 ? i * stride + stride + 2000: kAddressMax, pref.Access(i * stride + 2000, i, kLongDistance, false));
      EXPECT_EQ(i > 1 ? i * stride + stride + 3000: kAddressMax, pref.Access(i * stride + 3000, i, kLongDistance, false));
    }
  }
}

//test that table overflows properly
TEST(GlobalStreamPrefetcher, TableOverflow) {
  for (int stride = -4; stride <= 4; stride++) {
    GlobalStreamPrefetcher pref;
    SCOPED_TRACE(stride);
    if (stride == 0) continue;
    for (int i = 0; i < 50; i++) {
      int j;
      for (j = 0; j < GlobalStreamPrefetcher::kStreamTableSize ; j++) {
        EXPECT_EQ(kAddressMax, pref.Access(i * stride + (j+1)*1000, i, kLongDistance, false));
      }
      EXPECT_EQ(kAddressMax, pref.Access(i * stride + (j+1)*1000, i, kLongDistance, false));
    }
  }
}
