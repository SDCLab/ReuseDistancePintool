/*
 * test_parallel.cc
 *
 *  Created on: Mar 9, 2010
 *      Author: dschuff
 */

#include <pthread.h>
#include <gtest/gtest.h>
#include <boost/format.hpp>
#include "parallelsampledstack.h"

class ParallelSampledStackTest : public testing::Test {
public:
  void ParallelWorker(int thread);
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
                                                 kDefaultThreads));
    ParallelSampledStack::SetGlobalEnable(true);
    for (int i = 0; i < kDefaultThreads; i++) {
      threads_[i] = ParallelSampledStack::GetThreadStack(i);
    }
  }
  void TearDown() {
    ParallelSampledStack::CleanUp();
  }

  ParallelSampledStack *threads_[kDefaultThreads];
};

static const int kThreadCount = 4;
static const int kValueIncrementCount = 100000;
static const std::string kFilenameBase = "../rddata/applu-10k_sampled-fulltrace-";

void CreateThreads(pthread_t *threads, int count, void * (*function)(void *), void *arg) {
  for (long i = 0; i < count; i++) {
     if (pthread_create(&threads[i], NULL, function, reinterpret_cast<void *>(i)) != 0) {
       FAIL() << "Couldn't create threads";
     }
  }
}

void CleanUp(pthread_t *threads, int count) {
  for (int i = 0; i < count; i++) {
    void *ret = NULL;
    EXPECT_EQ(0, pthread_join(threads[i], &ret));
    long ret_int = reinterpret_cast<long>(ret);
    EXPECT_EQ(0, ret_int);
  }
}

void ParallelSampledStackTest::ParallelWorker(int thread) {
  std::string trace_file = kFilenameBase + str(boost::format("%d") % thread);
  try {
    InputTrace trace(trace_file);
    const TraceElement *element = trace.GetNext();
    int count = 0;
    printf("thread %d\n", thread);
    while (element && count < 1000000000) {
      count++;
      if (element->thread != thread)
        printf("mismatch %d %d\n", thread, element->thread);
      switch (element->type) {
        case TraceElement::kNewAddress:
          ParallelSampledStack::ActivateSampledAddress();
          threads_[element->thread]->NewSampledAddress(element->address, kDefaultPC);
          break;
        case TraceElement::kAccess:
          threads_[element->thread]->Access(element->address, kDefaultPC, element->is_write);
          break;
        case TraceElement::kMerge:
          threads_[element->thread]->MergeAllSamples();
          break;
        case TraceElement::kEnable:
          threads_[element->thread]->SetThreadEnable(element->enable);
          break;
        default:
          FAIL() << "Bad trace element type";
      }
      if (count % 1000 == 0) threads_[thread]->MergeAllSamples();
      element = trace.GetNext();
    }
    threads_[thread]->Sleep();
  } catch (std::ifstream::failure ex) {
    FAIL() << "Could not open input file";
  }
}

ParallelSampledStackTest *psst;

void *PW(void *arg) {
  long thread = reinterpret_cast<long>(arg);
  psst->ParallelWorker(thread);
  return reinterpret_cast<void *>(0);
}

TEST_F(ParallelSampledStackTest, ParallelTest) {
  pthread_t threads[kThreadCount];
  psst = this;
  CreateThreads(threads, kThreadCount, PW, NULL);
  CleanUp(threads, kThreadCount);
  ParallelSampledStack::DumpStatsPython("");
}
