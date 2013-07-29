/*
 * threadqueue.h
 *
 *  Created on: Feb 16, 2010
 *      Author: dschuff
 */

#ifndef THREADQUEUE_H_
#define THREADQUEUE_H_

template<class PtrType> class CircularThreadQueue {
public:
  CircularThreadQueue() : thread_count_(0), max_threads_(0) {}
  void Initialize(int max_threads) {
    thread_count_ = 0;
    max_threads_ = max_threads;
    threads_ = new PtrType[max_threads];
    for (int i = 0; i < max_threads; i++) threads_[i] = NULL;
  }
  int GetNextIndex(int n) const { return (n + 1) % thread_count_; }
  int GetThreadCount() const { return thread_count_;}
  const PtrType& operator[](int n) const { return threads_[n]; }
  int AddThread(const PtrType p) {
    if (thread_count_ == max_threads_) return -1;
    threads_[thread_count_] = p;
    thread_count_++;
    return thread_count_;
  }
private:
  PtrType *threads_;
  int thread_count_;
  int max_threads_;
};

#endif /* THREADQUEUE_H_ */
