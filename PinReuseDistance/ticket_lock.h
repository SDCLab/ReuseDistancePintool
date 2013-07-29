/*
 * ticket_lock.h
 *
 *  Created on: Mar 17, 2010
 *      Author: Derek
 *      // This code ripped (almost) directly from the Linux kernel ca. 2.6.28
 */

#ifndef TICKET_LOCK_H_
#define TICKET_LOCK_H_


#define TICKET_SHIFT 8
typedef struct raw_spinlock {
        unsigned int slock;
} ticket_spinlock_t;

#define __RAW_SPIN_LOCK_UNLOCKED        { 0 }
#define LOCK_PREFIX "lock "
#define LOCK_PTR_REG "D"
#define REG_PTR_MODE "q"

static inline void __ticket_spin_lock(ticket_spinlock_t *lock)
{
        short inc = 0x0100;

        asm volatile (
                LOCK_PREFIX "xaddw %w0, %1\n"
                "1:\t"
                "cmpb %h0, %b0\n\t"
                "je 2f\n\t"
                "rep ; nop\n\t"
                "movb %1, %b0\n\t"
                /* don't need lfence here, because loads are in-order */
                "jmp 1b\n"
                "2:"
                : "+Q" (inc), "+m" (lock->slock)
                :
                : "memory", "cc");
}

static inline int __ticket_spin_trylock(ticket_spinlock_t *lock)
{
        int tmp, nw;

        asm volatile("movzwl %2, %0\n\t"
                     "cmpb %h0,%b0\n\t"
                     "leal 0x100(%" REG_PTR_MODE "0), %1\n\t"
                     "jne 1f\n\t"
                     LOCK_PREFIX "cmpxchgw %w1,%2\n\t"
                     "1:"
                     "sete %b1\n\t"
                     "movzbl %b1,%0\n\t"
                     : "=&a" (tmp), "=&q" (nw), "+m" (lock->slock)
                     :
                     : "memory", "cc");

        return tmp;
}

static inline void __ticket_spin_unlock(ticket_spinlock_t *lock)
{
        asm volatile(LOCK_PREFIX "incb %0"
                     : "+m" (lock->slock)
                     :
                     : "memory", "cc");
}

static inline void __ticket_spin_init(ticket_spinlock_t *lock)
{
        lock->slock = 0;
}

#endif /* TICKET_LOCK_H_ */
