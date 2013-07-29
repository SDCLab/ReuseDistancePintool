/*
 * rda-sync.cc
 *
 *  Created on: Feb 23, 2010
 *      Author: dschuff
 */
#include "rda-sync.h"

//template<int stage_count, int max_threads>
int MultistageBarrier::AddThread() {
    RdaGetLock(&lock_);
    if (total_threads_ == max_threads) {
      RdaReleaseLock(&lock_);
      return -1;
    }
    int thread = total_threads_;
    total_threads_++;

    while (stage_ > 0 && count_[0] > 0) {
      int generation = generation_;
      RdaReleaseLock(&lock_);
      WaitForEnd(generation);
      RdaGetLock(&lock_);
    }
    next_threads_++;
    awake_[thread] = true;
    RdaReleaseLock(&lock_);
    return thread;
  }
//template<int stage_count, int max_threads>
  int MultistageBarrier::WaitStart(int thread) {
    RdaGetLock(&lock_);
    if (!awake_[thread]) {
      generations[thread] = generation_;
      RdaReleaseLock(&lock_);
      WaitForEnd(generations[thread]);
      return 0;
    } else {
      generations[thread] = generation_;
      if (stage_ != 0 || count_[1] != 0 || count_[2] != 0)
        throw std::runtime_error("stage or count wrong in waitstart");
      stages[thread] = 0;
      if (count_[0] == 0) {
        threads_ = next_threads_;
      }
      threads[thread] = threads_;
      count_[0]++;
      if (count_[0] == threads_) {
        count_[0] = 0;
        stage_ = 1;
        RdaReleaseLock(&lock_);
      } else {
        RdaReleaseLock(&lock_);
        while(stage_ == 0){
          asm volatile("pause" ::: "memory");
        }
        threads[thread] = threads_;
      }
      return generation_;
    }
  }
//template<int stage_count, int max_threads>
  void MultistageBarrier::WaitStage(int stage, int thread) {
    if (!awake_[thread] || stage == 0 || stage >= stage_count) {
      throw std::invalid_argument("Sleeping thread or stage 0 in wait");
    }
    RdaGetLock(&lock_);
    if (stage_ != stage)
      throw std::runtime_error("stage wrong in wait");
    stages[thread] = stage;
    count_[stage]++;
    if (count_[stage] == threads_) {
      count_[stage] = 0;
      stage_++;
      generations[thread] = generation_;
      if (stage_ == stage_count) {
        stage_ = 0;
        generation_++;
      }
      RdaReleaseLock(&lock_);
    } else {
      generations[thread] = generation_;
      RdaReleaseLock(&lock_);
      int i = 0;
      while(stage_ == stage){
        asm volatile("pause" ::: "memory");
        if (++i % 10000 == 0) WaitConsistency(thread);
      }
    }
  }
  void MultistageBarrier::WaitConsistency(int thread) {
    RdaGetLock(&lock_);
    int threads = 0;
    for (int i = 0; i < stage_count; i++) threads += count_[i];
    if (threads > threads_)
      throw std::runtime_error("sum of threads too high");
    //int gen = generations[thread];
    //for (int i = 0; i < max_threads; i++)
      //if (generations[i] != 0 && generations[i] != gen)
        //throw std::runtime_error("wrong generation");
    RdaReleaseLock(&lock_);
  }
//template<int stage_count, int max_threads>
  int MultistageBarrier::Wake(int thread) {
    if (awake_[thread] == true) {
      throw std::runtime_error("recursive wake");
    }
    //WaitForEnd();
    RdaGetLock(&lock_);
    while (stage_ > 0 || count_[0] > 0) {
      int generation = generation_;
      RdaReleaseLock(&lock_);
      WaitForEnd(generation);
      RdaGetLock(&lock_);
    }
    next_threads_++;
    awake_[thread] = true;
    int gens = generation_ - generations[thread];
    RdaReleaseLock(&lock_);
    return gens;
  }
//template<int stage_count, int max_threads>
  void MultistageBarrier::Sleep(int thread) {
    RdaGetLock(&lock_);
    next_threads_--;
    awake_[thread] = false;
    // if threads are waiting at stage 0, this sleep could release them
    if (stage_ == 0 && count_[0] != 0) {
      threads_--;
      if (count_[0] == threads_) {
        count_[0] = 0;
        stage_ = 1;
      }
    }
    else if (stage_ > 0)  {//should never happen; can't execute user instructions (syscall) during bar
      RdaReleaseLock(&lock_);
      throw std::runtime_error("Sleep during barrier");
    }
    generations[thread] = generation_;
    RdaReleaseLock(&lock_);
  }
  // returns the next-adjacent thread that is awake (if all threads but this one are asleep,
  // returns threadid)
//template<int stage_count, int max_threads>
  int MultistageBarrier::GetAdjacentSleepers(int threadid) {
    for (int i = thread_queue_->GetNextIndex(threadid); i != threadid;
        i = thread_queue_->GetNextIndex(i)) {
      if (awake_[i]) return i;
    }
    if (threads_ == 1) return threadid;
    throw std::runtime_error("wrapped around adjacent sleepers, awake[threadid] not set");
  }

//template<int stage_count, int max_threads>
void MultistageBarrier::WaitForEnd(int generation) {
  while(generation_ == generation){
    asm volatile("pause" ::: "memory" );
  }
}
