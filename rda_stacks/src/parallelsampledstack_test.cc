/*
 * parallelsampledstack_test.cc
 *
 *  Created on: Mar 4, 2010
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "parallelsampledstack.h"
#include "sampledreusestack.h"

class ParallelSampledStackTest : public testing::Test {
protected:
  static const int kDefaultGranularity = 64;
  static const int kDefaultThreads = 4;
  static const address_t kDefaultPC = 1;
  static const int kAccessCount = 20;
  // make sure our address does not get accessed accidentally as part of accesscount
  static const address_t kDefaultAddress = kDefaultGranularity * kAccessCount * 10;
  acc_count_t GetLastDistance(int thread) {
    return threads_[0]->global_rw_->last_finalized_distance;
  }
  const DistanceSet *GetDS(int thread, address_t address) {
    return threads_[thread]->GetDS(address);
  }
  acc_count_t GetActiveSamples(int thread) {
    return threads_[thread]->global_rw_->active_sample_count;
  }
  void SetUp() {
    ASSERT_TRUE(ParallelSampledStack::Initialize("parallelsampledstack-test", kDefaultGranularity,
                                                 kDefaultThreads, ParallelSampledStack::kPrivateStacks));
    ParallelSampledStack::SetGlobalEnable(true);
    for (int i = 0; i < kDefaultThreads; i++) {
      threads_[i] = ParallelSampledStack::GetThreadStack(i);
      threads_[i]->SetThreadEnable(true);
    }
  }
  void TearDown() {
    ParallelSampledStack::CleanUp();
  }
  ParallelSampledStack *threads_[kDefaultThreads];
};

const int ParallelSampledStackTest::kAccessCount;

//start sample, access T1 a bunch of other addrs, check result
TEST_F(ParallelSampledStackTest, AccessOneThread) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance(0));
}

TEST_F(ParallelSampledStackTest, AccessOneThreadMultiple) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, true);
  }
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance(0));
}

//multithread tests:

// create holes, fill (distance reduced) //also try nomerge

// create holes, don't fill (no effect on distance)
TEST_F(ParallelSampledStackTest, AccessHolesNoFill) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) threads_[1]->Access(i * kDefaultGranularity, kDefaultPC, true);
  }
  // threads_[0]->global_rw_->merge_needed = false;
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance(0));
}

// create holes, fill some of them
TEST_F(ParallelSampledStackTest, AccessHolesFillHalfNoMerge) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) threads_[1]->Access(i * kDefaultGranularity, kDefaultPC, true);
  }
  // hasnt been any merging yet
  ASSERT_TRUE(GetDS(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount, GetDS(0, kDefaultAddress)->set.size());
  EXPECT_EQ(0, GetDS(0, kDefaultAddress)->holes);
  // fill some holes (with addresses other than their originals)
  for (int i = kAccessCount; i < kAccessCount*2; i++) {
    if (i % 4 == 0) threads_[0]->Access(i * kDefaultGranularity + kAccessCount, kDefaultPC, false);
  }
  ASSERT_TRUE(GetDS(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount * 5 / 4, GetDS(0, kDefaultAddress)->set.size());
  EXPECT_EQ(0, GetDS(0, kDefaultAddress)->holes);
  // with the final merge, kAccessCount / 2 are invalidated but holes added == invals
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount * 5 / 4, GetLastDistance(0));
}

// create holes, fill all of them and more
TEST_F(ParallelSampledStackTest, AccessHolesFillNoMerge) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) threads_[1]->Access(i * kDefaultGranularity, kDefaultPC, true);
  }
  // access kAccessCount new addresses, filling the holes and increasing size by 50%
  // BUT no merging yet
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  // hasn't been any merging yet
  ASSERT_TRUE(GetDS(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount * 2, GetDS(0, kDefaultAddress)->set.size());
  EXPECT_EQ(0, GetDS(0, kDefaultAddress)->holes);
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount * 2, GetLastDistance(0));
}


// create holes, fill some of them
TEST_F(ParallelSampledStackTest, AccessHolesFillHalfMerge) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) threads_[1]->Access(i * kDefaultGranularity, kDefaultPC, true);
  }
  threads_[0]->MergeAllSamples();
  ASSERT_TRUE(GetDS(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount / 2, GetDS(0, kDefaultAddress)->set.size());
  EXPECT_EQ(kAccessCount / 2, GetDS(0, kDefaultAddress)->holes);
  // fill some holes (with addresses other than their originals)
  for (int i = kAccessCount; i < kAccessCount*2; i++) {
    if (i % 4 == 0) threads_[0]->Access(i * kDefaultGranularity + kAccessCount, kDefaultPC, false);
  }
  threads_[0]->MergeAllSamples();
  ASSERT_TRUE(GetDS(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount * 3 / 4, GetDS(0, kDefaultAddress)->set.size());
  EXPECT_EQ(kAccessCount / 4, GetDS(0, kDefaultAddress)->holes);
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount , GetLastDistance(0));
}

// create holes, fill all of them and more
TEST_F(ParallelSampledStackTest, AccessHolesFillMerge) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  // lay down base RD accesses
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  // invalidate half of them, leaving holes
  for (int i = 0; i < kAccessCount; i++) {
    if (i % 2 == 0) threads_[1]->Access(i * kDefaultGranularity, kDefaultPC, true);
  }
  threads_[0]->MergeAllSamples();
  // access kAccessCount new addresses, filling the holes and increasing size by 50%
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }

  ASSERT_TRUE(GetDS(0, kDefaultAddress) != NULL);
  EXPECT_EQ(kAccessCount * 3 / 2, GetDS(0, kDefaultAddress)->set.size());
  EXPECT_EQ(0, GetDS(0, kDefaultAddress)->holes);
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount + kAccessCount / 2, GetLastDistance(0));
}

//nested distance, no holes
TEST_F(ParallelSampledStackTest, NestedDistance) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress + kDefaultGranularity, kDefaultPC);
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  EXPECT_FALSE(threads_[0]->Access(kDefaultAddress + kDefaultGranularity, kDefaultPC, false));
  EXPECT_EQ(kAccessCount, GetLastDistance(0));
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount * 2 + 1, GetLastDistance(0));
  EXPECT_EQ(0/*kAccessCount * 3 + 3*/, GetActiveSamples(0));
}

//overlapped distance, no holes
TEST_F(ParallelSampledStackTest, OverlappedDistance) {
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress, kDefaultPC);
  for (int i = 0; i < kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  ParallelSampledStack::ActivateSampledAddress();
  threads_[0]->NewSampledAddress(kDefaultAddress + kDefaultGranularity, kDefaultPC);
  for (int i = kAccessCount; i < 2 * kAccessCount; i++) {
    threads_[0]->Access(i * kDefaultGranularity, kDefaultPC, false);
  }
  EXPECT_FALSE(threads_[0]->Access(kDefaultAddress, kDefaultPC, false));
  EXPECT_EQ(kAccessCount * 2, GetLastDistance(0));
  threads_[0]->Access(0, kDefaultPC, false);
  EXPECT_TRUE(threads_[0]->Access(kDefaultAddress + kDefaultGranularity, kDefaultPC, false));
  EXPECT_EQ(kAccessCount + 2, GetLastDistance(0));
  EXPECT_EQ(0/*kAccessCount * 3 + 4*/, GetActiveSamples(0));
}

const std::string kTraceFile("../rddata/applu-10k_sampled-fulltrace");

TEST_F(ParallelSampledStackTest, CompareSingleStack) {
  SampledReuseStack *sampler = new SampledReuseStack("sampledreusestack-test", kDefaultGranularity);
  sampler->set_global_enable(true);
  for (int i = 0; i < kDefaultThreads; i++) {
    sampler->Allocate(i);
  }
  try {
    InputTrace trace(kTraceFile);
    const TraceElement *element = trace.GetNext();
    int count = 100000;
    while (element && count) {
      while (element->thread != 0) element = trace.GetNext();
      count--;
      switch (element->type) {
        case TraceElement::kNewAddress:
          sampler->NewSampledAddress(element->address, element->thread, kDefaultPC);
          ParallelSampledStack::ActivateSampledAddress();
          threads_[element->thread]->NewSampledAddress(element->address, kDefaultPC);
          break;
        case TraceElement::kAccess:
          sampler->SampleAccess(element->address, element->thread, kDefaultPC, element->is_write);
          threads_[element->thread]->Access(element->address, kDefaultPC, element->is_write);
          EXPECT_EQ(sampler->GetLastDistance(), GetLastDistance(0)) << count;
          ASSERT_EQ(sampler->GetGlobalTrackedAddressCount(), GetActiveSamples(0)) << count;
          break;
        case TraceElement::kMerge:
          // ignore merges for 1 thread
          break;
        case TraceElement::kEnable:
          sampler->set_thread_enable(element->thread, element->enable);
          threads_[element->thread]->SetThreadEnable(element->enable);
          break;
        default:
          FAIL() << "Bad trace element type";
      }
      element = trace.GetNext();
    }
  } catch (std::ifstream::failure ex) {
    FAIL() << "Could not open input file";
  }
  sampler->DumpStats("");
  ParallelSampledStack::DumpStatsPython("");
  delete sampler;
}

TEST_F(ParallelSampledStackTest, CompareMultiStack) {
  SampledReuseStack *sampler = new SampledReuseStack("sampledreusestack-test", kDefaultGranularity);
  sampler->set_global_enable(true);
  for (int i = 0; i < kDefaultThreads; i++) {
    sampler->Allocate(i);
  }
  try {
    InputTrace trace(kTraceFile);
    const TraceElement *element = trace.GetNext();
    int count = 0;
    while (element && count < 10000000) {
      count++;
      switch (element->type) {
        case TraceElement::kNewAddress:
          sampler->NewSampledAddress(element->address, element->thread, kDefaultPC);
          ParallelSampledStack::ActivateSampledAddress();
          threads_[element->thread]->NewSampledAddress(element->address, kDefaultPC);
          break;
        case TraceElement::kAccess:
          sampler->SampleAccess(element->address, element->thread, kDefaultPC, element->is_write);
          threads_[element->thread]->Access(element->address, kDefaultPC, element->is_write);
          //ASSERT_EQ(sampler->GetLastDistance(), GetLastDistance(0)) << count;
          ASSERT_EQ(sampler->GetGlobalTrackedAddressCount(), GetActiveSamples(0)) << count;
          break;
        case TraceElement::kMerge:
          //
          threads_[element->thread]->MergeAllSamples();
          break;
        case TraceElement::kEnable:
          sampler->set_thread_enable(element->thread, element->enable);
          threads_[element->thread]->SetThreadEnable(element->enable);
          break;
        default:
          FAIL() << "Bad trace element type";
      }
      //threads_[0]->MergeAllSamples();
      //if (count % 1000 == 0) threads_[0]->MergeAllSamples();
      element = trace.GetNext();
    }
  } catch (std::ifstream::failure ex) {
    FAIL() << "Could not open input file";
  }
  sampler->DumpStats("");
  ParallelSampledStack::DumpStatsPython("");
  delete sampler;
}
