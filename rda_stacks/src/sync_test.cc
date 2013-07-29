/*
 * sync_test.cc
 *
 *  Created on: Feb 24, 2010
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include <pthread.h>
#include "rda-sync.h"

static const int kThreadCount = 4;
static const int kValueIncrementCount = 100000;

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

RdaLock lock;
int value = 0;

void *MutexWorker(void *arg) {
  //int *value = static_cast<int *>(arg);
  for (int i = 0; i < kValueIncrementCount; i++) {
    RdaGetLock(&lock);
    value += 1;
    RdaReleaseLock(&lock);
  }
  return reinterpret_cast<void *>(0);
}

TEST(SyncTest, BasicMutexWorks) {
  pthread_t threads[kThreadCount];
  value = 0;
  RdaInitLock(&lock);
  CreateThreads(threads, kThreadCount, MutexWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(kValueIncrementCount * kThreadCount, value);
}

TEST(SyncTest, TryLockWorksSequential) {
  RdaInitLock(&lock);
  EXPECT_TRUE(RdaTryLock(&lock));
  RdaReleaseLock(&lock);
  RdaGetLock(&lock);
  EXPECT_FALSE(RdaTryLock(&lock));
  RdaReleaseLock(&lock);
}

void *LockHolderWorker(void *arg) {
  //int *value = static_cast<int *>(arg);
  for (int i = 0; i < kValueIncrementCount; i++) {
    LockHolder lh(&lock);
    value += 1;
  }
  return reinterpret_cast<void *>(0);
}

TEST(SyncTest, LockHolderWorks) {
  pthread_t threads[kThreadCount];
  value = 0;
  RdaInitLock(&lock);
  CreateThreads(threads, kThreadCount, LockHolderWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(kValueIncrementCount * kThreadCount, value);
}

void *AtomicIncWorker(void *arg) {
  //int *value = static_cast<int *>(arg);
  for (int i = 0; i < kValueIncrementCount; i++) {
    AtomicIncrement(&value);
  }
  return reinterpret_cast<void *>(0);
}

void *AtomicDecWorker(void *arg) {
  //int *value = static_cast<int *>(arg);
  for (int i = 0; i < kValueIncrementCount; i++) {
    AtomicDecrement(&value);
  }
  return reinterpret_cast<void *>(0);
}

TEST(SyncTest, AtomicIncrementWorks) {
  pthread_t threads[kThreadCount];
  value = 0;
  CreateThreads(threads, kThreadCount, AtomicIncWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(kValueIncrementCount * kThreadCount, value);
}

TEST(SyncTest, AtomicDecrementWorks) {
  pthread_t threads[kThreadCount];
  value = kValueIncrementCount * kThreadCount;
  CreateThreads(threads, kThreadCount, AtomicDecWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(0, value);
}

MultistageBarrier *barrier;
const int kBarrierLoops = 5;

void *BasicBarrierWorker(void *arg) {
  long threadid = reinterpret_cast<long>(arg);
  for (int i = 0; i < kBarrierLoops; i++) {
    if (threadid == (1 + i) % kThreadCount) value++;
    barrier->WaitStart(threadid);
    if (threadid == (2 + i) % kThreadCount) value++;
    barrier->WaitStage(1, threadid);
    if (threadid == (3 + i) % kThreadCount) value++;
    barrier->WaitStage(2, threadid);
    //barrier->WaitStage(3, threadid);
    //if (threadid == (4 + i) % kThreadCount) value++;
  }
  return reinterpret_cast<void *>(0);
}

TEST(SyncTest, BasicBarrierTest) {
  barrier = new MultistageBarrier();
  pthread_t threads[kThreadCount];
  barrier->Init(NULL);
  value = 0;
  for (int i = 0; i < kThreadCount; i++) {
    ASSERT_EQ(i, barrier->AddThread());
  }
  CreateThreads(threads, kThreadCount, BasicBarrierWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(3 * kBarrierLoops, value);
  delete barrier;
}

void *SleepingBarrierWorker(void *arg) {
  long threadid = reinterpret_cast<long>(arg);
  if (threadid == kThreadCount - 1) {
    for (int i = 0; i < kBarrierLoops; ) {
      if (i % 2 == 0) {
        barrier->Sleep(threadid);
        i += barrier->Wake(threadid);
      } else {
        barrier->WaitStart(threadid);
        barrier->WaitStage(1, threadid);
        barrier->WaitStage(2, threadid);
        //barrier->WaitStage(3, threadid);
        i++;
      }
    }
  } else {
    for (int i = 0; i < kBarrierLoops; i++) {
      if (threadid == (1 + i) % (kThreadCount-1)) value++;
      barrier->WaitStart(threadid);
      if (threadid == (2 + i) % (kThreadCount-1)) value++;
      barrier->WaitStage(1, threadid);
      if (threadid == (3 + i) % (kThreadCount-1)) value++;
      barrier->WaitStage(2, threadid);
      //barrier->WaitStage(3, threadid);
    }
  }
  return reinterpret_cast<void *>(0);
}

TEST(SyncTest, SleepingBarrierTest) {
  barrier = new MultistageBarrier();
  pthread_t threads[kThreadCount];
  barrier->Init(NULL);
  value = 0;
  for (int i = 0; i < kThreadCount; i++) {
    ASSERT_EQ(i, barrier->AddThread());
  }
  CreateThreads(threads, kThreadCount, SleepingBarrierWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(3 * kBarrierLoops, value);
  delete barrier;
}

void *SleepingBarrier2Worker(void *arg) {
  long threadid = reinterpret_cast<long>(arg);
  if (threadid == kThreadCount - 2) {
    for (int i = 0; i < kBarrierLoops; ) {
      if ((i + threadid % 2) % 2 == 0) {
        barrier->Sleep(threadid);
        i += barrier->Wake(threadid);
      } else {
        barrier->WaitStart(threadid);
        barrier->WaitStage(1, threadid);
        barrier->WaitStage(2, threadid);
        //barrier->WaitStage(3, threadid);
        i++;
      }
    }
  } else {
    for (int i = 0; i < kBarrierLoops; i++) {
      if (threadid == (1 + i) % (kThreadCount-2)) value++;
      barrier->WaitStart(threadid);
      if (threadid == (2 + i) % (kThreadCount-2)) value++;
      barrier->WaitStage(1, threadid);
      if (threadid == (3 + i) % (kThreadCount-2)) value++;
      barrier->WaitStage(2, threadid);
      //barrier->WaitStage(3, threadid);
    }
  }
  return reinterpret_cast<void *>(0);
}

TEST(SyncTest, SleepingBarrierTest2Sleepers) {
  barrier = new MultistageBarrier();
  pthread_t threads[kThreadCount];
  barrier->Init(NULL);
  value = 0;
  for (int i = 0; i < kThreadCount; i++) {
    ASSERT_EQ(i, barrier->AddThread());
  }
  value = 0;
  CreateThreads(threads, kThreadCount, SleepingBarrierWorker, NULL);
  CleanUp(threads, kThreadCount);
  EXPECT_EQ(3 * kBarrierLoops, value);
  delete barrier;
}




