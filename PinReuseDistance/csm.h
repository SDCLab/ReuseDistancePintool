/* This version of csm.h is for oracular finegrain CSM */
/* defines for mprotect arguments */

#ifndef USING_CSM
#define USING_CSM

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define PROT_THREAD_SHARED       0x10   /* all threads can access this page */
#define PROT_THREAD_PRIVATE      0x20   /* only the calling thread can access */
#define PROT_THREAD_RSHARED      0x40   /* only the caller can write (not imp)*/
#define PROT_THREAD_ACTION_1     0x80
#define PROT_THREAD_ACTION_2     0x100
#define PROT_THREAD_FATAL_ERROR  0x80   /* sharing violations are fatal */
#define PROT_THREAD_WARNING      0x100  /* sharing violations yield warnings*/
#define PROT_VERBOSE             0x200  /* extra printks in mprotect */

#define PROT_TID_SHIFT           12
#define PROT_TID_MASK            0x7ff  /* 11 bits */
#define PROT_TID(tid)            ((tid & PROT_TID_MASK) << PROT_TID_SHIFT)

#define CSM_PAGE_SZ              4096

//if these csm codes change, will have to also update csm-kernel which doesnt use this header!
#define CSM_CODE_BREAK           55555
#define CSM_CODE_FLUSH_TLBS      55556
#define CSM_CODE_START_TIMER     55557
#define CSM_CODE_STOP_TIMER      55558
#define CSM_CODE_RANGE_DEC       55559
#define CSM_CODE_SET_FINEGRAIN_RANGE 55560
#define CSM_CODE_SET_PID         55561
#define CSM_CODE_DO_FIRSTTOUCH   55562
#define CSM_CODE_START_PERIOD    55563
#define CSM_CODE_END_PERIOD      55564
#define CSM_CODE_LOCAL_START_PERIOD 55565
#define CSM_CODE_LOCAL_END_PERIOD 55566

#define CHUNK_NONE -1
#define CHUNK_PRIVATE 0
#define CHUNK_STACK -2


// static inline unsigned int threadreg(unsigned int n)
// {
//         asm volatile ("movl %0, %%eax" : : "g" (n) : "eax");
//         asm volatile ("xchg %bx,%bx");
//         asm volatile ("movl %%eax, %0" : "=g" (n) :  );
//         return n;
// }
#ifdef __x86_64
static inline unsigned int threadreg(unsigned int n)
{
        unsigned long val;
        val = n;
        asm volatile ("movq %0, %%rax" : : "g" (val) : "rax");
        asm volatile ("xchg %bx,%bx");
        asm volatile ("movq %%rax, %0" : "=g" (val) :  );
        return (unsigned int) val;
}

static inline unsigned int csm_finegrain(unsigned long code, unsigned long start, unsigned long end, unsigned long pieces)
    __attribute__((always_inline));
static inline unsigned int csm_finegrain(unsigned long code, unsigned long start, unsigned long end, unsigned long pieces)
{
        unsigned long out;
        asm volatile ( "movq %1, %%rax;\n"
              "movq %2, %%r8;\n"
              "movq %3, %%r9;\n"
              "movq %4, %%r10;\n"
              "xchg %%bx, %%bx;\n"
              "movq %%rax, %0;"
             : "=g" (out)
             : "g" (code), "g" (start), "g" (end), "g" (pieces)
             : "rax", "r8", "r9", "r10"
            );
        return (unsigned int) out;
}

static inline void start_sim_timer()
{
        threadreg(CSM_CODE_START_TIMER);
}
static inline void stop_sim_timer()
{
        threadreg(CSM_CODE_STOP_TIMER);
}

#ifdef __cplusplus
extern "C" {
#endif
void csm_start_sim_timer_();
void csm_stop_sim_timer_();
#ifdef __cplusplus
}
#endif

//static inline unsigned int csm_addrange(void * start, void * end)
#define csm_addrange(start, end)\
do{\
        csm_finegrain(CSM_CODE_RANGE_DEC, (unsigned long)start, (unsigned long)end, CHUNK_PRIVATE);\
} while(0);
//static inline unsigned int csm_addlen(void *start, size_t len)
#define csm_addlen(start, len) \
do{\
        csm_finegrain(CSM_CODE_RANGE_DEC, (unsigned long)start, (unsigned long)start+len, CHUNK_PRIVATE);\
} while(0);

//static inline unsigned int csm_decchunklen(void *start, size_t len, int chunksize)
#define csm_decchunklen(start, len, chunksize)\
 do {\
        csm_finegrain(CSM_CODE_RANGE_DEC, (unsigned long)start,(unsigned long)start+len, chunksize);\
} while(0);
//static inline unsigned int csm_decchunklen(void *start, size_t len, int chunksize)
#define csm_decchunknum(start,  len, chunks)\
do{\
        csm_finegrain(CSM_CODE_RANGE_DEC, (unsigned long)start,(unsigned long)start+len, len/chunks);\
} while(0);

//static inline unsigned int csm_decobject(void * start, size_t len)
#define csm_decobject(start, len)\
do{\
        csm_finegrain(CSM_CODE_RANGE_DEC, (unsigned long)start, (unsigned long)start+len, CHUNK_NONE);\
} while(0);
static inline unsigned int csm_set_finegrain_range(void * start, void * end)
{
        return csm_finegrain(CSM_CODE_SET_FINEGRAIN_RANGE, (unsigned long)start, (unsigned long)end, 0);
}
static inline unsigned int csm_firsttouch_range(void * start, void * end, unsigned long pieces)
{
        return csm_finegrain(CSM_CODE_DO_FIRSTTOUCH, (unsigned long)start, (unsigned long)end, pieces);
}
static inline unsigned int csm_firsttouch_len(void * start, size_t len, unsigned long pieces)
{
        return csm_finegrain(CSM_CODE_DO_FIRSTTOUCH, (unsigned long)start, (unsigned long)start+len, pieces);
}

static inline void *csm_shared_malloc(size_t size)
    __attribute__((always_inline));
static inline void *csm_shared_malloc(size_t size)
{
    void * ret;
    ret = malloc(size);
    if(ret) csm_decobject(ret, size);
    return ret;
}

static inline void csm_start_period(void * function)
{
	//threadreg(55563);
	csm_finegrain(CSM_CODE_START_PERIOD, (unsigned long)function, 0UL, 0UL);
}
static inline void csm_end_period()
{
	threadreg(CSM_CODE_END_PERIOD);
}
static inline void csm_local_start_period()
{
  threadreg(CSM_CODE_LOCAL_START_PERIOD);
}
static inline void csm_local_end_period()
{
  threadreg(CSM_CODE_LOCAL_END_PERIOD);
}

static inline void csm_break()
{
        threadreg(CSM_CODE_BREAK);
}

static inline unsigned int read_threadreg(void)
{
        return threadreg(0);
}

static inline void *private_malloc_finegrain(size_t sz)
{
        void *p = malloc(sz);
        if(p) csm_addlen(p, sz);
        return p;
}
static inline void *private_calloc_finegrain(size_t n, size_t sz)
{
        void *p = calloc(n, sz);
        if(p) csm_addlen(p, n*sz);
        return p;
}
extern int pthread_getattr_np (pthread_t __th, pthread_attr_t *__attr) __THROW;
extern int pthread_attr_getstack (__const pthread_attr_t *__restrict __attr,
                                  void **__restrict __stackaddr,
                                  size_t *__restrict __stacksize) __THROW;
static inline int csm_protect_stack()
{
        pthread_attr_t retattr;
        void * stackaddr; size_t stacksize;
        if(pthread_getattr_np(pthread_self(), &retattr)) perror("getattr error: ");
        if(pthread_attr_getstack(&retattr, &stackaddr, &stacksize)) perror("getstack error: ");
        printf("protecting thread %d stack at %p size %zd\n", threadreg(0), stackaddr, stacksize);
        csm_decobject(stackaddr, stacksize);
        csm_addlen(stackaddr, stacksize);
        return 0;
}
static inline int csm_declare_stack()
{
        pthread_attr_t retattr;
        void * stackaddr; size_t stacksize;
        if(pthread_getattr_np(pthread_self(), &retattr)) perror("getattr error: ");
        if(pthread_attr_getstack(&retattr, &stackaddr, &stacksize)) perror("getstack error: ");
        printf("declaring thread %d stack at %p size %zd\n", threadreg(0), stackaddr, stacksize);
        //csm_decobject(stackaddr, stacksize);
        csm_finegrain(CSM_CODE_RANGE_DEC, (unsigned long)stackaddr, (unsigned long)(stackaddr)+stacksize, CHUNK_STACK);
        return 0;
}	

#else
//this is for building the 32-bit gcc libs... can't figure out how to make it not do that
static inline void csm_start_period()
{
	
}
static inline void csm_end_period()
{
        
}
static inline void csm_local_start_period()
{
}
static inline void csm_local_end_period()
{
}

#endif

#endif
