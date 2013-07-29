/*
 * sampledreuse_perref.cc
 *
 *  Created on: Sep 24, 2009
 *      Author: dschuff
 */

#include <boost/random/variate_generator.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/geometric_distribution.hpp>
#include <linux/futex.h>
//#include <asm/unistd_64.h>
#include <deque>
#include <sys/syscall.h>
#include <sys/time.h>
#include "pin.H"
#include "librarymap.h"
#include "magicinstruction.h"
#include "parallelsampledstack.h"
#include "ticket_lock.h"
#include "version.h"

typedef boost::variate_generator<boost::rand48, boost::geometric_distribution<int, double> >
    GeomRand48;

static TLS_KEY tls_key;
INT32 numThreads = 0;

LibraryMap library_map;

INT32 bbl_count = 0;
INT32 bbl_ref_count = 0;
INT32 ins_count = 0;
INT32 ref_ins = 0;
INT32 ref_ins_refs = 0;
struct timeval start_time;
struct timeval elapsed;
int stack_type;

#ifdef SEQUENTIAL
ticket_spinlock_t global_lock __attribute__((aligned(64)));
#define GET_LOCK(X) __ticket_spin_lock(X)
#define RELEASE_LOCK(X) __ticket_spin_unlock(X)
#define INIT_LOCK(X) __ticket_spin_init(X)
#else
#define GET_LOCK(X)
#define RELEASE_LOCK(X)
#define INIT_LOCK(X)
#endif

//class global_data_t {
//public:
//  PIN_LOCK sampler_lock; // protects active_samples and the sampler itself
//  UINT8 _pad[64 - sizeof(PIN_LOCK)];
//  global_data_t() : sampling(0), active_samples(0) {
//    InitLock(&sampler_lock);
//  }
//};
//global_data_t global_data;

const std::string kOutputFileName = "reusedistance.out";
const INT32 kDefaultSampleInterval = 1000000;
const std::string kDefaultStackType = "private";
const INT32 kDefaultSyncInterval = 0;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", kOutputFileName,
                            "specify output file name");

KNOB<int> KnobGranularity(KNOB_MODE_WRITEONCE, "pintool", "g",
                          decstr(ParallelSampledStack::kDefaultGranularity),
                          "specify analysis granularity");

KNOB<int> KnobSampleInterval(KNOB_MODE_WRITEONCE, "pintool", "i",
                             decstr(kDefaultSampleInterval), "mean sampling interval");

KNOB<string>KnobSampledStackType(KNOB_MODE_WRITEONCE, "pintool", "s", kDefaultStackType,
                                 "sampled stack type");

KNOB<int>KnobSyncInterval(KNOB_MODE_WRITEONCE, "pintool", "si", decstr(kDefaultSyncInterval),
                          "synchronization interval");

// Force each thread's data to be in its own data cache line so that
// multiple threads do not contend for the same data cache line.
// This avoids the false sharing problem.
// 64 byte line size:
class thread_data_t
{
  public:
    INT64 refcount;  // number of refs before sample is triggered
    INT64 samplecount;  // number of refs which were active (i.e. during sampling mode)
    INT64 totalrefs;  // count of total references this thread has made
    INT32 samplechoice;  // determines which ref in the BBL is the sample
    INT32 is_samplechoice;  // true if this thread is the one triggering the sample
    boost::rand48 generator;
    boost::geometric_distribution<int, double> distribution;
    GeomRand48 sample_generator;
    ParallelSampledStack *stack;
    bool sleeping;
    std::deque<INT32> samplequeue;
    UINT8 _pad[128 - 3*8 - 2*4 - sizeof(boost::rand48)
               - sizeof(boost::rand48) - sizeof(boost::geometric_distribution<int, double>)
               - sizeof(ParallelSampledStack *) - sizeof(bool)];
  explicit thread_data_t(int threadid) :
      refcount(0), samplecount(0), totalrefs(0), samplechoice(0), is_samplechoice(0),
      generator(threadid), distribution((KnobSampleInterval.Value() - 1.0) / KnobSampleInterval.Value()),
      sample_generator(generator, distribution), stack(NULL), sleeping(false) {}
};

// function to access thread-specific data
static thread_data_t* get_tls(THREADID threadid) {
  thread_data_t* tdata =
      static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
  return tdata;
}

ADDRINT PIN_FAST_ANALYSIS_CALL IncrementReferenceCount(INT32 count, THREADID threadid) {
  INT64 rcount = get_tls(threadid)->refcount -= count;
  return rcount <= 0;
}

VOID SampleTrigger(THREADID threadid) {
  thread_data_t *tls = get_tls(threadid);
  if (tls->is_samplechoice) {
    tls->samplequeue.push_back(-tls->refcount);
  } else {
    tls->samplechoice = -tls->refcount;
  }
  tls->is_samplechoice++;

  tls->refcount = tls->sample_generator();
  tls->totalrefs += tls->refcount;
  //printf("sample %ld, is_samp %d\n", tls->refcount, tls->is_samplechoice);

  //printf("trigger\n");
  ParallelSampledStack::ActivateSampledAddress();
}

VOID InstrumentTraceForRefcount(TRACE trace, VOID *v) {
  // Visit every basic block  in the trace
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    int refcount = 0;
    // Count number of memory references in BBL
    for (INS ins= BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      refcount += INS_IsMemoryRead(ins) + INS_IsMemoryWrite(ins) + INS_HasMemoryRead2(ins);
    }
    if (refcount > 0) {
      // Insert a call to docount before every bbl, passing the number of references
      BBL_InsertIfCall(bbl, IPOINT_BEFORE, (AFUNPTR)IncrementReferenceCount, IARG_FAST_ANALYSIS_CALL,
                     IARG_UINT32, refcount, IARG_THREAD_ID, IARG_END);
      BBL_InsertThenCall(bbl, IPOINT_BEFORE, (AFUNPTR)SampleTrigger, IARG_THREAD_ID, IARG_END);
    }
  }
}

VOID Fini(INT32 code, VOID *v);
ADDRINT PIN_FAST_ANALYSIS_CALL MemRef() {
  return ParallelSampledStack::HasActiveSamples();
}
VOID PIN_FAST_ANALYSIS_CALL SampledMemRef(THREADID threadid, ADDRINT address,// UINT32 size,
                                          BOOL is_write, VOID * PC) {
  thread_data_t *tls = get_tls(threadid);
  if (tls->sleeping) {
    tls->sleeping = false;
    tls->stack->Wake();
  }
  tls->samplecount++;
  if (KnobSyncInterval.Value() && tls->samplecount % KnobSyncInterval.Value() == 0) {
    GET_LOCK(&global_lock);
    tls->stack->MergeAllSamples();
    RELEASE_LOCK(&global_lock);
  }
//  try {
  GET_LOCK(&global_lock);
  tls->stack->Access(address, reinterpret_cast<address_t>(PC), is_write);
  RELEASE_LOCK(&global_lock);

  // the trigger thread chooses its sample from its BBL before incrementing active_samples
  if (tls->is_samplechoice) {
    if (tls->samplechoice-- == 0) {
      // sample this address
      //printf("sampling %lx\n", address);
      GET_LOCK(&global_lock);
      tls->stack->NewSampledAddress(address, reinterpret_cast<address_t>(PC));
      RELEASE_LOCK(&global_lock);
      tls->is_samplechoice--;
      if (tls->is_samplechoice) {
        tls->samplechoice = tls->samplequeue.front();
        tls->samplequeue.pop_front();
      }
    }
  }

//  } catch (std::runtime_error ex) {
//    printf("Caught exception: %s\n", ex.what());
//    //Fini(0, NULL);
//    abort();
//  }
}

VOID InstrumentTraceForAnalysis(TRACE trace, VOID *v) {
  //printf("Instrumenting trace %lx, mode %d for anal\n", TRACE_Address(trace), current_mode);
  // Visit every basic block  in the trace
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    bbl_count++;
    for (INS ins= BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      ins_count++;
      int is_ref = 0;
      // instruments loads using a predicated call, i.e.
      // the call happens iff the load will be actually executed
      // (this does not matter for ia32 but arm and ipf have predicated instructions)
      if (INS_IsMemoryRead(ins)) {
        bbl_ref_count++;
        is_ref++;
        INS_InsertIfPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRef,
                                   IARG_FAST_ANALYSIS_CALL, IARG_END);
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SampledMemRef,
                                     IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID,
                                     IARG_MEMORYREAD_EA,
                                     //IARG_MEMORYREAD_SIZE,
                                     IARG_BOOL, false,
                                     IARG_INST_PTR, IARG_END);
//          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SampledMemRef,
//                                   IARG_INST_PTR,
//                                   IARG_MEMORYREAD_EA,
//                                   IARG_MEMORYREAD_SIZE,
//                                   IARG_THREAD_ID,
//                                   IARG_END);

      }
      if (INS_HasMemoryRead2(ins)) {
        is_ref++;
        bbl_ref_count++;
        INS_InsertIfPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRef,
                                   IARG_FAST_ANALYSIS_CALL, IARG_END);
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SampledMemRef,
                                     IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID,
                                     IARG_MEMORYREAD2_EA,
                                     //IARG_MEMORYREAD_SIZE,
                                     IARG_BOOL, false,
                                     IARG_INST_PTR, IARG_END);
//          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)AnalyzeMemRead,
//                                   IARG_INST_PTR,
//                                   IARG_MEMORYREAD2_EA,
//                                   IARG_MEMORYREAD_SIZE,
//                                   IARG_THREAD_ID,
//                                   IARG_END);

      }
      // instruments stores using a predicated call, i.e.
      // the call happens iff the store will be actually executed
      if (INS_IsMemoryWrite(ins)) {
        is_ref++;
        bbl_ref_count++;
        INS_InsertIfPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)MemRef,
                                   IARG_FAST_ANALYSIS_CALL, IARG_END);
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)SampledMemRef,
                                     IARG_FAST_ANALYSIS_CALL, IARG_THREAD_ID,
                                     IARG_MEMORYWRITE_EA,
                                     IARG_MEMORYWRITE_SIZE,
                                     IARG_BOOL, true,
                                     IARG_INST_PTR, IARG_END);
//          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)AnalyzeMemWrite,
//                                   IARG_INST_PTR,
//                                   IARG_MEMORYWRITE_EA,
//                                   IARG_MEMORYWRITE_SIZE,
//                                   IARG_THREAD_ID,
//                                   IARG_END);
      }
      if (is_ref) {
        ref_ins++;
        ref_ins_refs += is_ref;
      }
    }
  }
}

VOID EmulateMagicInstruction(ADDRINT rax_value, THREADID tid) {
  thread_data_t *tls = get_tls(tid);
  if (tls->sleeping) {
    tls->sleeping = false;
    tls->stack->Wake();
  }
  switch (rax_value) {
    // same cases as in simics memstat.cc but not all of them are implemented here
    // the CSM counter-setting case (rax & 0x8000000000000000) is unused, so we can do a switch
    // case 0 reads the thread register. don't bother implementing, it's not used
    case 0:
      break;
    case CSM_CODE_START_TIMER:
      // start memory instrumentation. i think this can be controlled but dont know how yet.
      // for now, just use an enable in the stackholder.
      printf("stacks global enable\n");
      gettimeofday(&start_time, NULL);
      ParallelSampledStack::SetGlobalEnable(true);
      break;
    case CSM_CODE_STOP_TIMER:
      // call EndParallelRegion etc and stop instrumentation. same as above
      printf("stacks global disable\n");
      GET_LOCK(&global_lock);
      tls->stack->MergeAllSamples();
      ParallelSampledStack::SetGlobalEnable(false);
      struct timeval end_time;
      gettimeofday(&end_time, NULL);
      timersub(&end_time, &start_time, &elapsed);
      RELEASE_LOCK(&global_lock);
      break;
    // These cases are legal but ignored or shouldn't be seen in the pintool
    case CSM_CODE_RANGE_DEC:
      // this one is leftover in some apps but just ignore it
      break;
    case CSM_CODE_SET_FINEGRAIN_RANGE:  // fall through
    case CSM_CODE_SET_PID:  // fall through
    case CSM_CODE_DO_FIRSTTOUCH:
      printf("Something weird going on... unused CSM code %lx\n", rax_value);
      break;
    case CSM_CODE_START_PERIOD:  // fall through
    case CSM_CODE_END_PERIOD:
      // r8 tells us the function here but we don't use it right now
      //printf("Period start\n");
      if (stack_type == ParallelSampledStack::kPrivateStacks) {
	GET_LOCK(&global_lock);
	tls->stack->MergeAllSamples();
	RELEASE_LOCK(&global_lock);
      }
      break;
    case CSM_CODE_LOCAL_START_PERIOD:
      tls->stack->SetThreadEnable(true);
      break;
    case CSM_CODE_LOCAL_END_PERIOD:
      tls->stack->SetThreadEnable(false);
      break;
    default:
      printf("Error: default case in EmulateMagicInstruction, rax %lx\n", rax_value);
  }
}

// Inserts code to implement the Simics magic instruction
// Magic instruction is xchg %bx, %bx
VOID InstrumentMagicInstruction(INS ins, VOID *v)
{
  if (INS_IsXchg(ins)) {
    if (INS_RegR(ins, 0) == REG_BX && INS_RegR(ins, 1) == REG_BX) {
      //printf("Magic instruction at %lx\n", INS_Address(ins));
      INS_InsertPredicatedCall(ins, IPOINT_AFTER,
                               reinterpret_cast<AFUNPTR>(EmulateMagicInstruction),
                               IARG_REG_VALUE, REG_RAX,
                               IARG_THREAD_ID,
                               //IARG_INST_PTR,
                               IARG_END);
    }
  }
}

VOID InstrumentLibraryMap(IMG image, VOID *v) {
  printf("image %s %lx %lx\n", IMG_Name(image).c_str(), IMG_LowAddress(image), IMG_HighAddress(image));
  library_map.AddImage(IMG_LowAddress(image), IMG_HighAddress(image), IMG_Name(image));
}

VOID SyscallEntry(THREADID thread, CONTEXT *ctxt, SYSCALL_STANDARD standard, VOID *v) {
  if (PIN_GetSyscallNumber(ctxt, standard) == SYS_futex) {
    //printf("futex call %d arg %ld\n", thread, PIN_GetSyscallArgument(ctxt, standard, 1));
    // newer kernels support different futex options such as process-private which are or-ed
    // with the futex op. if FUTEX_CMD_MASK isn't on your system, it can be deleted
    if ((PIN_GetSyscallArgument(ctxt, standard, 1) & FUTEX_CMD_MASK) == FUTEX_WAIT){
      thread_data_t *tls = get_tls(thread);
      //printf(" futex wait %d\n", thread);
//      if (tls->stack->global_rw_->barrier.stage_ > 0) {
//        ADDRINT rip = PIN_GetContextReg(ctxt, REG_RIP);
//        // Get Pin client lock according to description of PIN_GetSourceLocation()
//        PIN_LockClient();
//        INT32 lineNumber;
//        string fileName;
//        // Get line info
//        PIN_GetSourceLocation(rip, NULL, &lineNumber, &fileName);
//        PIN_UnlockClient();
//
//        printf("sleep during barrier at %s:%d\n", fileName.c_str(), lineNumber);
//      }
      if (tls->sleeping) {
        //printf("recursive sleep thread %d\n", thread);
        return;
      }
      tls->sleeping = true;
      tls->stack->Sleep();
    }
  } else {
    //printf("syscall %ld\n",PIN_GetSyscallNumber(ctxt, standard) );
  }
}

VOID SyscallExit(THREADID thread, CONTEXT *ctxt, SYSCALL_STANDARD standard, VOID *v) {
    thread_data_t *tls = get_tls(thread);
    if (tls->sleeping){
      printf(" futex waking %d\n", thread);
      //tls->sleeping = false;
      //tls->stack->Wake();
    }
}

VOID ContextChange(THREADID thread, CONTEXT_CHANGE_REASON reason, const CONTEXT *from, CONTEXT *to,
                   INT32 info, VOID *v) {
  switch (reason) {
    case CONTEXT_CHANGE_REASON_SIGNAL:
      printf("Received handled unix signal\n");
      break;
    case CONTEXT_CHANGE_REASON_SIGRETURN:
      printf("Return from handled unix signal\n");
      break;
    default:
      printf("wtf, got context_chage_reason %d\n", reason);
  }
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    printf("thread begin %d\n", threadid);
    thread_data_t *tdata = new thread_data_t(threadid);
    PIN_SetThreadData(tls_key, tdata, threadid);
    numThreads++;
    tdata->refcount = KnobSampleInterval.Value();
    GET_LOCK(&global_lock);
    tdata->stack = ParallelSampledStack::GetThreadStack(threadid);
    RELEASE_LOCK(&global_lock);
}

VOID ThreadExit(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v) {
  printf("thread %d exited\n", threadid);
  thread_data_t *tls = get_tls(threadid);
  if (!tls->sleeping) {
    tls->stack->Sleep();
  }
}

VOID Fini(INT32 code, VOID *v) {
  INT64 total_refs = 0;
  INT64 total_samples = 0;
  for (INT32 t = 0; t < numThreads; t++) {
       thread_data_t* tdata = get_tls(t);
       printf("thread %d samplecount %ld totalrefs %ld, refcount %ld, sum %ld\n", t,
              tdata->samplecount, tdata->totalrefs, tdata->refcount,
              tdata->totalrefs - tdata->refcount);
       total_refs += tdata->totalrefs - tdata->refcount;
       total_samples += tdata->samplecount;
  }
  //printf("global samplecount %ld\n", global_data.samplecount);
  printf("%d BBLs %.2f refs/BBL\n", bbl_count, static_cast<double>(bbl_ref_count) / bbl_count);
  printf("%d ref ins, %.2f refs/ref ins\n",
         bbl_ref_count, static_cast<double>(bbl_ref_count) / ref_ins);
  std::string version(std::string("#") + __FILE__ + " version " + PINRD_GIT_VERSION + "\n");
  std::string samplerate(std::string("#sample interval ") + decstr(KnobSampleInterval.Value())
                         + "\n");
  std::string syncinterval(std::string("#sync interval ") + decstr(KnobSyncInterval.Value())
                           + "\n");
  std::string totalrefs(std::string("#totalrefs ") + decstr(total_refs) + "\n");
  INT64 address_samples = ParallelSampledStack::DumpStatsPython(
      library_map.GetPythonString() + version + samplerate + syncinterval + totalrefs);
  printf("total samples %ld, refs %ld, sampling %.2f%% of time\n",
         total_samples, total_refs, static_cast<double>(total_samples) / total_refs * 100.0);
  printf("overall average sampled addrs per ref: %.2f\n",
         static_cast<double>(address_samples) / total_refs);
  printf("%.2f seconds elapsed ROI time===================================\n", 
	     elapsed.tv_sec + static_cast<float>(elapsed.tv_usec) / 1000000);

  ParallelSampledStack::CleanUp();
}

void InitSampler() {
  char *omp_threads = getenv("OMP_NUM_THREADS");
  int threads;
  if (omp_threads == NULL) {
    threads = 4;
    printf("warning: OMP_NUM_THREADS not set, using 4 threads");
  } else {
    threads = atoi(omp_threads);  // no error checking but this has to go eventually anyway.
    if (threads < 0) throw std::invalid_argument("OMP_NUM_THREADS env var is invalid");
  }
  if (KnobSampledStackType.Value() == "private") {
    if (ParallelSampledStack::Initialize(KnobOutputFile.Value() + "-sampled",
          KnobGranularity.Value(), threads, ParallelSampledStack::kPrivateStacks) == false) {
      throw std::runtime_error("Error initializing ParallelSampledStack");
    }
    stack_type = ParallelSampledStack::kPrivateStacks;
  } else if (KnobSampledStackType.Value() == "shared") {
    if (ParallelSampledStack::Initialize(KnobOutputFile.Value() + "-shared-sampled",
          KnobGranularity.Value(), threads, ParallelSampledStack::kSharedStacks) == false) {
      throw std::runtime_error("Error initializing ParallelSampledStack");
    }
    stack_type = ParallelSampledStack::kSharedStacks;
  } else {
    throw std::invalid_argument("stack type must be \"private\" or \"shared\"");
  }
  ParallelSampledStack::SetGlobalEnable(false);
  INIT_LOCK(&global_lock);
}

int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(0);
    InitSampler();

    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadExit, NULL);
    PIN_AddFiniFunction(Fini, 0);
    INS_AddInstrumentFunction(InstrumentMagicInstruction, 0);
    TRACE_AddInstrumentFunction(InstrumentTraceForRefcount, 0);
    TRACE_AddInstrumentFunction(InstrumentTraceForAnalysis, 0);
    IMG_AddInstrumentFunction(InstrumentLibraryMap, 0);
    PIN_AddSyscallEntryFunction(SyscallEntry, NULL);
    //PIN_AddSyscallExitFunction(SyscallExit, NULL);
    PIN_AddContextChangeFunction(ContextChange, NULL);
    PIN_InitSymbols();
    // Activate alarm, must be done before PIN_StartProgram
    //controller.CheckKnobs(Handler, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
