/*
 * strideprefetcher_test.cc
 *
 *  Created on: Nov 9, 2010
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "strideprefetcher.h"

const acc_count_t kLongDistance = 1024 * 1024 * 10;//something long enough to always be a miss

//test that streams with no repeated PCs never result in prefetches
TEST(StridePrefetcher, UniquePCs) {
  StridePrefetcher pref;
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, pref.Access(i, i, kLongDistance, false));
  }
}

//test that short distances never result in prefetches
TEST(StridePrefetcher, ShortDistances) {
  StridePrefetcher pref;
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, pref.Access(i, 1, 10, false));
  }
}

//test that alternating strides
TEST(StridePrefetcher, AlternatingStrides) {
  StridePrefetcher pref;
  EXPECT_EQ(kAddressMax, pref.Access(1, 1, kLongDistance, false));
  EXPECT_EQ(kAddressMax, pref.Access(0, 1, kLongDistance, false));
  EXPECT_EQ(kAddressMax, pref.Access(1, 1, kLongDistance, false));
  for (int i = 0; i < 100; i++) {
    SCOPED_TRACE(i);
    EXPECT_EQ(!(i % 2), pref.Access(i % 2, 1, kLongDistance, false));
  }
}

//test that zero strides never result in prefetches
TEST(StridePrefetcher, ZeroStride) {
  StridePrefetcher pref;
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kAddressMax, pref.Access(1, 1, kLongDistance, false));
  }
}

//test that streams with various strides and only one PC result in prefetches
TEST(StridePrefetcher, Strides) {
  for (int stride = -2; stride < 3; stride++) {
    StridePrefetcher pref;
    SCOPED_TRACE(stride);
    if (stride == 0) continue;
    for (int i = 0; i < 50; i++) {
      EXPECT_EQ(i > 1 ? i * stride + stride : kAddressMax, pref.Access(i * stride, 1, kLongDistance, false));
    }
  }
}
