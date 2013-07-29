/*
 * rda-sync.h
 *
 *  Created on: Feb 9, 2010
 *      Author: dschuff
 */

#ifndef RDASYNC_H_
#define RDASYNC_H_

#include <stdexcept>
#include <stdio.h>
#include "threadqueue.h"
//#include "ticket_lock.h"
//#include "futex_lock.h"

//typedef FutexLock RdaLock;
//#define RdaInitLock(l) FutexInitLock(l)
//#define RdaGetLock(l) FutexGetLock(l)
//#define RdaReleaseLock(l) FutexReleaseLock(l)
//#define RdaTryLock(l) FutexTryLock(l)

//typedef raw_spinlock_t RdaLock;
//inline void RdaInitLock(RdaLock *lock) {
//  __ticket_spin_init(lock);
//}
//
//inline void RdaGetLock(RdaLock *lock) {
//  __ticket_spin_lock(lock);
//}
//
//inline void RdaReleaseLock(RdaLock *lock) {
//  __ticket_spin_unlock(lock);
//}
//
//inline bool RdaTryLock(RdaLock *lock) {
//  return __ticket_spin_trylock(lock);
//}

typedef volatile int RdaLock;

inline void RdaInitLock(RdaLock *lock) {
  *lock = 0;
}

inline void RdaGetLock(RdaLock *lock) {
  while (__sync_lock_test_and_set(lock, 1)) {
    while (*lock) {
      asm volatile("pause"  ::: "memory");
	  }
	}
}

inline bool RdaTryLock(RdaLock *lock) {
  return __sync_lock_test_and_set(lock, 1) == 0;
}

inline void RdaReleaseLock(RdaLock *lock) {
  __sync_lock_release(lock);
}

inline void AtomicIncrement(volatile int *v) {
  __sync_add_and_fetch(v, 1);
}

inline void AtomicDecrement(volatile int *v) {
  __sync_sub_and_fetch(v, 1);
}

class LockHolder {
public:
  explicit LockHolder(RdaLock *lock) {
    lock_ = lock;
    RdaGetLock(lock);
  }
  ~LockHolder() {
    RdaReleaseLock(lock_);
  }
private:
  RdaLock *lock_;
};

//template<int stage_count, int max_threads=MAX_THREADS> class MultistageBarrier {
#define max_threads 16
#define stage_count 3
class ParallelSampledStack;
class MultistageBarrier {
public:
  MultistageBarrier() : thread_queue_(NULL), threads_(0), next_threads_(0), stage_(0),
  generation_(1), total_threads_(0) {}
  void Init(CircularThreadQueue<ParallelSampledStack *> *tq) {
    thread_queue_ = tq;
    RdaInitLock(&lock_);
    for (int i = 0; i < max_threads; i++) {
      awake_[i] = true;
    }
    for (int i = 0; i < stage_count; i++) {
      count_[i] = 0;
    }
  }
  int AddThread() ;
  int WaitStart(int thread) ;
  void WaitStage(int stage, int thread) ;
  int Wake(int thread) ;
  void Sleep(int thread) ;
  void WaitConsistency(int stage);
  // returns the next-adjacent thread that is awake (if all threads but this one are asleep,
  // returns threadid)
  int GetAdjacentSleepers(int threadid) ;
  ~MultistageBarrier() {
    //make sure all the threads get released
    LockHolder l(&lock_);
    for (int i = 0; i < stage_count; i++) {
      if (count_[i] != 0){
        printf("error: Tried to delete barrier with threads still waiting");
      }
      stage_++;
    }
    generation_++;
  }
//private:
  RdaLock(lock_);
  CircularThreadQueue<ParallelSampledStack *> *thread_queue_;
  int threads_;
  int next_threads_;
  volatile int count_[stage_count];
  volatile bool awake_[max_threads];
  volatile int stage_;
  volatile int generation_;
  int total_threads_;
  int stages[max_threads];
  int generations[max_threads];
  int threads[max_threads];
  void WaitForEnd(int generation);
};

class FakeBarrier { // for testing. all methods just return
public:
  FakeBarrier() : total_threads_(0) {}
  void Init(void *tq) {}
  int AddThread() { return total_threads_++;}
  int WaitStart(int thread) {return 1;}
  void WaitStage(int stage, int thread) {}
  int Wake(int thread) {return 1;}
  void Sleep(int thread) {}
  void WaitConsistency(int stage) {}
  // returns the next-adjacent thread that is awake (if all threads but this one are asleep,
  // returns threadid)
  int GetAdjacentSleepers(int threadid) {return threadid;}
private:
  int total_threads_;
};

#endif /* RDASYNC_H_ */
