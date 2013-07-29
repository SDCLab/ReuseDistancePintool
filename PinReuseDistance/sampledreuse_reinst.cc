/*
 * sampledreuse_reinst.cc
 *
 *  Created on: Sep 23, 2009
 *      Author: dschuff
 */

#include <semaphore.h>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <tr1/unordered_map>
#include "pin.H"

PIN_LOCK sampler_lock;
int64_t global_refcount = 100000;
enum InstrumentationMode { REFCOUNT, SAMPLE_CHOICE, ANALYSIS };
InstrumentationMode current_mode = REFCOUNT;
std::tr1::unordered_map<ADDRINT, int> bbl_refcount;

int sample_refcount;
ADDRINT sampled_addr;
sem_t sample_sem;
THREADID sample_thread;

int test_next_sample = 10000;
inline static VOID DoSample(ADDRINT addr, THREADID tid, bool is_write) {
  GetLock(&sampler_lock, 1);
  //printf("tid %d addr %lx sampling!\n", tid, addr);
  if (addr == sampled_addr) printf("got reuse!\n");
  //sampler_->SampleAccess(addr, tid, is_write);
  if (--test_next_sample == 0) {
    current_mode = REFCOUNT;
    PIN_RemoveInstrumentation();
    test_next_sample = 1000;
    global_refcount = 100000;
  }
  ReleaseLock(&sampler_lock);
}

void InitSampler() {
  InitLock(&sampler_lock);
  if (sem_init(&sample_sem, 0, 0) != 0) perror("sem_init");
//  sampler_ = new SampledReuseStack(KnobOutputFile.Value() + "_sampled", KnobGranularity.Value());
//  boost::rand48 generator;
//  boost::uniform_int<int> distribution(1, 1000);
//  UniRand48 uniform(generator, distribution);
//  sample_generator = &uniform;
//  next_sample = uniform();
//  sampler_->set_global_enable(false);
//  printf("init next %d\n", next_sample);
//  return 0;
}


VOID PIN_FAST_ANALYSIS_CALL IncrementReferenceCount(INT32 count, ADDRINT bbl_addr) {
  __sync_sub_and_fetch(&global_refcount, count);
//  int64_t mycount = __sync_sub_and_fetch(&global_refcount, count);
//  if (mycount <= 0 ) {  // need to go into trace mode
//    if (mycount > -count) {  // this thread's BBL has the address
//      global_refcount = 100000;
//      return;
//      current_mode = SAMPLE_CHOICE;
//      //printf("thread %d sampled trace addr %lx\n", PIN_ThreadId(), bbl_addr);
//      sample_refcount = 11;//some random number?
//      sample_thread = PIN_ThreadId();
//      PIN_RemoveInstrumentation(); // re-instrument. this BBL will not get instrumented again
//    } else if (PIN_ThreadId() != sample_thread){
//      //printf("thread %d waiting at addr %lx mode %d\n", PIN_ThreadId(), bbl_addr, current_mode);
//      // wait to get sampled address
//      //sem_wait(&sample_sem);
//    }
//  }
}

VOID SelectSampleAddress(ADDRINT addr, ADDRINT ref_address) {
  GetLock(&sampler_lock, 1);
  //printf("thread %d counting %d\n", PIN_ThreadId(), sample_refcount);
  if (--sample_refcount == 0) {
    //printf("thread %d selected sample address %lx\n", PIN_ThreadId(), ref_address);
    sampled_addr = ref_address;
    current_mode = ANALYSIS;
    //sampler_->NewSampledAddress(addr, tid);
    PIN_RemoveInstrumentation();
    //notify other threads
    //for (int i = 0; i < 3; i++) sem_post(&sample_sem);
  }
  ReleaseLock(&sampler_lock);
}

VOID InstrumentTraceForRefcount(TRACE trace, VOID *v) {
  // Visit every basic block  in the trace
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    int refcount = 0;
    // Count number of memory references in BBL
    for (INS ins= BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      refcount += INS_IsMemoryRead(ins) + INS_IsMemoryWrite(ins) + INS_HasMemoryRead2(ins);
    }
    bbl_refcount[BBL_Address(bbl)] = refcount;
    if (refcount > 0) {
      // Insert a call to docount before every bbl, passing the number of references
      BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)IncrementReferenceCount, IARG_FAST_ANALYSIS_CALL,
                     IARG_UINT32, refcount, IARG_ADDRINT, TRACE_Address(trace), IARG_END);
    }
  }
}

VOID InstrumentTraceForSampleChoice(TRACE trace, VOID *v) {
  //printf("thread %d Instrumenting trace %lx, mode %d for sample\n", PIN_ThreadId(),
         //TRACE_Address(trace), current_mode);
  // Visit every basic block in the trace, count the references
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    for (INS ins= BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins) ) {
      if (INS_IsMemoryRead(ins)) {
        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SelectSampleAddress,
                                 IARG_MEMORYREAD_EA, IARG_END);
      }
      if (INS_HasMemoryRead2(ins)) {
        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SelectSampleAddress,
                                 IARG_MEMORYREAD2_EA, IARG_END);
      }
      if (INS_IsMemoryWrite(ins)) {
        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SelectSampleAddress,
                                 IARG_MEMORYWRITE_EA, IARG_END);
      }
    }
  }
}

// Analyze a memory read record
VOID AnalyzeMemRead(VOID * ip, ADDRINT addr, ADDRINT size, THREADID tid)
{
  //if (size > 8) printf("Large read sz %ld at %p\n", size, ip);
  //GetLock(&stacks_lock, 1);
  DoSample(addr, tid, false);
  //ReleaseLock(&stacks_lock);
}

// Analyze a memory write record
VOID AnalyzeMemWrite(VOID * ip, ADDRINT addr, ADDRINT size, THREADID tid)
{
  //if (size > 8) printf("Large write sz %ld at %p\n", size, ip);
  //GetLock(&stacks_lock,2);
  DoSample(addr, tid, true);
  //ReleaseLock(&stacks_lock);
}

VOID InstrumentTraceForAnalysis(TRACE trace, VOID *v) {
  //printf("Instrumenting trace %lx, mode %d for anal\n", TRACE_Address(trace), current_mode);
  // Visit every basic block  in the trace
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    for (INS ins= BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      // instruments loads using a predicated call, i.e.
      // the call happens iff the load will be actually executed
      // (this does not matter for ia32 but arm and ipf have predicated instructions)
      if (INS_IsMemoryRead(ins)) {
          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)AnalyzeMemRead,
                                   IARG_INST_PTR,
                                   IARG_MEMORYREAD_EA,
                                   IARG_MEMORYREAD_SIZE,
                                   IARG_THREAD_ID,
                                   IARG_END);

      }
      if (INS_HasMemoryRead2(ins)) {
          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)AnalyzeMemRead,
                                   IARG_INST_PTR,
                                   IARG_MEMORYREAD2_EA,
                                   IARG_MEMORYREAD_SIZE,
                                   IARG_THREAD_ID,
                                   IARG_END);

      }
      // instruments stores using a predicated call, i.e.
      // the call happens iff the store will be actually executed
      if (INS_IsMemoryWrite(ins)) {
          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)AnalyzeMemWrite,
                                   IARG_INST_PTR,
                                   IARG_MEMORYWRITE_EA,
                                   IARG_MEMORYWRITE_SIZE,
                                   IARG_THREAD_ID,
                                   IARG_END);
      }
    }
  }
}

VOID InstrumentTrace(TRACE trace, VOID *v) {
  //printf("Instrumenting trace %lx, mode %d\n", TRACE_Address(trace), current_mode);
  switch(current_mode) {
    case REFCOUNT:
      InstrumentTraceForRefcount(trace, v);
      break;
    case SAMPLE_CHOICE:
      InstrumentTraceForSampleChoice(trace, v);
      break;
    case ANALYSIS:
      InstrumentTraceForAnalysis(trace, v);
      break;
  }
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    GetLock(&sampler_lock, threadid+1);
    printf("thread begin %d\n", threadid);
    //sampler_->Allocate(threadid);
    ReleaseLock(&sampler_lock);
}

int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);

    InitSampler();

    //INS_AddInstrumentFunction(InstrumentMagicInstruction, 0);
    //PIN_AddThreadStartFunction(ThreadStart, 0);
    //PIN_AddFiniFunction(Fini, 0);
    TRACE_AddInstrumentFunction(InstrumentTrace, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);

    // Activate alarm, must be done before PIN_StartProgram
    //controller.CheckKnobs(Handler, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
