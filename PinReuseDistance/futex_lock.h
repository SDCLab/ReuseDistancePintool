/*
 * futex_lock.h
 *
 *  Created on: Mar 17, 2010
 *      Author: Derek
 */
#include <linux/futex.h>
#include <sys/time.h>
#include <linux/unistd.h>
#include <sys/syscall.h>

//_syscall6(int, futex, int *, int, int, const struct timespec *, int* int);

typedef struct futex_lock {
  int lock __attribute__((aligned(64)));
} FutexLock;

//inline void FutexGetLock(FutexLock *lock) {
//  int val = __sync_sub_and_fetch(&lock->lock, 1);
//  if (val != 0) {
//    lock->lock = -1;
//    syscall(SYS_futex, &lock->lock, FUTEX_WAIT, val, NULL, NULL, 0);
//  }
//}
//
//inline void FutexReleaseLock(FutexLock *lock) {
//  int val = __sync_add_and_fetch(&lock->lock, 1);
//  if (val != 1) {
//    lock->lock = 1;
//    syscall(SYS_futex, &lock->lock, FUTEX_WAKE, 1, NULL, NULL, 0);
//  }
//}

inline void FutexInitLock(FutexLock *lock) {
  //lock->lock = 1;
  lock->lock = 0;
}

inline bool FutexTryLock(FutexLock *lock) {
  return false;
}


static inline void FutexGetLock (FutexLock *mutex)
{
  if (!__sync_bool_compare_and_swap (&mutex->lock, 0, 1)) {
    do
      {
        int oldval = __sync_val_compare_and_swap (&mutex->lock, 1, 2);
        if (oldval != 0)
          //futex_wait(&mutex->lock, 2);
          syscall(SYS_futex,  &mutex->lock, FUTEX_WAIT, 2, NULL, NULL, 0);
      }
    while (!__sync_bool_compare_and_swap (&mutex->lock, 0, 2));
  }
}
static inline void FutexReleaseLock (FutexLock *mutex)
{
  /* Warning: By definition __sync_lock_test_and_set() does not have
     proper memory barrier semantics for a mutex unlock operation.
     However, this default implementation is written assuming that it
54       does, which is true for some targets.
55
56       Targets that require additional memory barriers before
57       __sync_lock_test_and_set to achieve the release semantics of
58       mutex unlock, are encouraged to include
59       "config/linux/ia64/mutex.h" in a target specific mutex.h instead
60       of using this file.  */
  int val = __sync_lock_test_and_set (&mutex->lock, 0);
  if (__builtin_expect (val > 1, 0))
    //futex_wake(mutex, 1);
    syscall(SYS_futex, &mutex->lock, FUTEX_WAKE, 1, NULL, NULL, 0);
}
