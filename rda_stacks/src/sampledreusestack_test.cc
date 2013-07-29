/*
 * sampledreusestack_test.cc
 *
 *  Created on: Mar 4, 2010
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "sampledreusestack.h"

class SampledReuseStackTest : public testing::Test {
protected:
  static const int kDefaultGranularity = 64;
  static const int kDefaultThreads = 4;
  static const address_t kDefaultPC = 1;
  static const int kAccessCount = 20;
  static const address_t kDefaultAddress = kDefaultGranularity * kAccessCount * 10;
  acc_count_t GetLastDistance() { return sampler_->last_finalized_distance_;}
  const SetAndHoles *GetSetAndHoles(int thread, address_t address) {
    return sampler_->GetSetAndHoles(thread, address);
  }
  acc_count_t GetAddrPerSampleTotal() {return sampler_->addresses_per_sample_total_; }
  void SetUp() {
    sampler_ = new SampledReuseStack("sampledreusestack-test", kDefaultGranularity);
    sampler_->set_global_enable(true);
    for (int i = 0; i < kDefaultThreads; i++) {
      sampler_->Allocate(i);
    }
  }
  void TearDown() {
    delete sampler_;
  }
  SampledReuseStack *sampler_;
};

const int SampledReuseStackTest::kAccessCount;

//start sample, access T1 a bunch of other addrs, check result
TEST_F(SampledReuseStackTest, AccessOneThread) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance());
}

TEST_F(SampledReuseStackTest, AccessOneThreadMultiple) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, true);
  }
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance());
}

// create holes, don't fill (no effect on distance)
TEST_F(SampledReuseStackTest, AccessHolesNoFill) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) sampler_->SampleAccess(i * kDefaultGranularity, 1, kDefaultPC, true);
  }
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance());
}

// create holes, fill some of them
TEST_F(SampledReuseStackTest, AccessHolesFillHalf) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) sampler_->SampleAccess(i * kDefaultGranularity, 1, kDefaultPC, true);
  }
  ASSERT_TRUE(GetSetAndHoles(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount / 2, GetSetAndHoles(0, kDefaultAddress)->set->size());
  EXPECT_EQ(kAccessCount / 2, GetSetAndHoles(0, kDefaultAddress)->hole_count);
  // fill some holes (with addresses other than their originals)
  for (int i = kAccessCount; i < kAccessCount*2; i++) {
    if (i % 4 == 0) sampler_->SampleAccess(i * kDefaultGranularity + kAccessCount, 0,
                                           kDefaultPC, false);
  }
  ASSERT_TRUE(GetSetAndHoles(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount * 3 / 4, GetSetAndHoles(0, kDefaultAddress)->set->size());
  EXPECT_EQ(kAccessCount / 4, GetSetAndHoles(0, kDefaultAddress)->hole_count);
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance());
}

// create holes, fill all of them and more
TEST_F(SampledReuseStackTest, AccessHolesFill) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) sampler_->SampleAccess(i * kDefaultGranularity, 1, kDefaultPC, true);
  }
  // access kAccessCount new addresses, filling the holes and increasing size by 50%
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  ASSERT_TRUE(GetSetAndHoles(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount * 3 / 2, GetSetAndHoles(0, kDefaultAddress)->set->size());
  EXPECT_EQ(0, GetSetAndHoles(0, kDefaultAddress)->hole_count);
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount + kAccessCount / 2, GetLastDistance());
}

//nested distance, no holes
TEST_F(SampledReuseStackTest, NestedDistance) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  sampler_->NewSampledAddress(kDefaultAddress + kDefaultGranularity, 0, kDefaultPC);
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  EXPECT_FALSE(sampler_->SampleAccess(kDefaultAddress + kDefaultGranularity, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance());
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount * 2 + 1, GetLastDistance());
  EXPECT_EQ(kAccessCount * 3 + 3, GetAddrPerSampleTotal());
}

//overlapped distance, no holes
TEST_F(SampledReuseStackTest, OverlappedDistance) {
  sampler_->NewSampledAddress(kDefaultAddress, 0, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  sampler_->NewSampledAddress(kDefaultAddress + kDefaultGranularity, 0, kDefaultPC);
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    sampler_->SampleAccess(i * kDefaultGranularity, 0, kDefaultPC, false);
  }
  EXPECT_FALSE(sampler_->SampleAccess(kDefaultAddress , 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount * 2, GetLastDistance());
  sampler_->SampleAccess(0, 0, kDefaultPC, false);
  EXPECT_TRUE(sampler_->SampleAccess(kDefaultAddress + kDefaultGranularity, 0, kDefaultPC, false));
  EXPECT_EQ(kAccessCount + 2, GetLastDistance());
  EXPECT_EQ(kAccessCount * 3 + 4, GetAddrPerSampleTotal());
}

