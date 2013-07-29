/*
 * sampledreuse_perref.cc
 *
 *  Created on: Sep 24, 2009
 *      Author: dschuff
 */

#include <boost/random/variate_generator.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/geometric_distribution.hpp>
#include "pin.H"
#include "librarymap.h"
#include "magicinstruction.h"
#include "sampledreusestack.h"
#include "sharedsampledreusestack.h"
#include "version.h"

typedef boost::variate_generator<boost::rand48, boost::geometric_distribution<int, double> >
    GeomRand48;

static TLS_KEY tls_key;
INT32 numThreads = 0;

SampledReuseStackInterface *sampler;
LibraryMap library_map;

INT32 bbl_count = 0;
INT32 bbl_ref_count = 0;
INT32 ins_count = 0;
INT32 ref_ins = 0;
INT32 ref_ins_refs = 0;


class global_data_t {
public:
  INT32 sampling;  // true if any thread has an active sample. checked on every reference
  INT32 active_samples;  // number of outstanding sampled references.
                         // set once the trigger thread has chosen its sample from its BBL
  PIN_LOCK sampler_lock; // protects active_samples and the sampler itself
  UINT8 _pad[56 - sizeof(PIN_LOCK)];
  global_data_t() : sampling(0), active_samples(0) {
    InitLock(&sampler_lock);
  }
};
global_data_t global_data;

const std::string kOutputFileName = "reusedistance.out";
const INT32 kDefaultSampleInterval = 100000;
const std::string kDefaultStackType = "private";

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", kOutputFileName,
                            "specify output file name");

KNOB<int> KnobGranularity(KNOB_MODE_WRITEONCE, "pintool", "g",
                          decstr(SampledReuseStack::kDefaultGranularity),
                          "specify analysis granularity");

KNOB<int> KnobSampleInterval(KNOB_MODE_WRITEONCE, "pintool", "i",
                             decstr(kDefaultSampleInterval), "mean sampling interval");

KNOB<string>KnobSampledStackType(KNOB_MODE_WRITEONCE, "pintool", "s", kDefaultStackType,
                                 "sampled stack type");

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
    UINT8 _pad[128 - 3*8 - 2*4 - sizeof(GeomRand48)
               - sizeof(boost::rand48) - sizeof(boost::geometric_distribution<int, double>)];
  explicit thread_data_t(int threadid) :
      refcount(0), samplecount(0), totalrefs(0), samplechoice(0), is_samplechoice(0),
      generator(threadid), distribution((KnobSampleInterval.Value() - 1.0) / KnobSampleInterval.Value()),
      sample_generator(generator, distribution) {}
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
  INT64 samplechoice = -tls->refcount;
  tls->refcount = tls->sample_generator();
  //printf("sample %ld\n", tls->refcount);
  tls->totalrefs += tls->refcount;
  //printf("trigger\n");
  global_data.sampling = 1;
  tls->samplechoice = samplechoice;
  tls->is_samplechoice = 1;
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

ADDRINT PIN_FAST_ANALYSIS_CALL MemRef() {
  return global_data.sampling == 1;
}
VOID PIN_FAST_ANALYSIS_CALL SampledMemRef(THREADID threadid, ADDRINT address, ADDRINT size,
                                          BOOL is_write, VOID * PC) {
  thread_data_t *tls = get_tls(threadid);
  // every thread checks if there are any active samples before doing work
  if (global_data.active_samples) {
    GetLock(&global_data.sampler_lock, threadid+1);
    if (global_data.active_samples) {
      if (global_data.active_samples < 0) printf("sample_chosen %d\n", global_data.active_samples);
      bool ret = sampler->SampleAccess(address, threadid, reinterpret_cast<address_t>(PC), is_write);
      if (ret) {
        global_data.sampling = 0;
        global_data.active_samples--;
      }
      tls->samplecount++;
    }
    ReleaseLock(&global_data.sampler_lock);
  }
  // the trigger thread chooses its sample from its BBL before incrementing active_samples
  if (tls->is_samplechoice && tls->samplechoice-- == 0) {
    // sample this address
    GetLock(&global_data.sampler_lock, threadid+1);
    //printf("sampling %lx\n", address);
    sampler->NewSampledAddress(address, threadid, reinterpret_cast<address_t>(PC));
    global_data.sampling = 1;
    global_data.active_samples++;
    ReleaseLock(&global_data.sampler_lock);
    tls->is_samplechoice = 0;
  }
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
                                     IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
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
                                     IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE,
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
                                     IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
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
      sampler->set_global_enable(true);
      break;
    case CSM_CODE_STOP_TIMER:
      // call EndParallelRegion etc and stop instrumentation. same as above
      printf("stacks global disable\n");
      sampler->set_global_enable(false);
      break;
    // These cases are legal but ignored or shouldn't be seen in the pintool
    case CSM_CODE_RANGE_DEC:
      // this one is leftover in some apps but just ignore it
      break;
    case CSM_CODE_SET_FINEGRAIN_RANGE:  // fall through
    case CSM_CODE_SET_PID:  // fall through
    case CSM_CODE_DO_FIRSTTOUCH:  // fall through
      printf("Something weird going on... unused CSM code %lx\n", rax_value);
      break;
    case CSM_CODE_START_PERIOD:
      // r8 tells us the function here but we don't use it right now
      GetLock(&global_data.sampler_lock, tid+1);
      sampler->TraceMerge(tid);
      ReleaseLock(&global_data.sampler_lock);
      break;
    case CSM_CODE_END_PERIOD:
      GetLock(&global_data.sampler_lock, tid+1);
      sampler->TraceMerge(tid);
      ReleaseLock(&global_data.sampler_lock);
      break;
    case CSM_CODE_LOCAL_START_PERIOD:
      GetLock(&global_data.sampler_lock, tid+1);
      sampler->set_thread_enable(tid, true);
      ReleaseLock(&global_data.sampler_lock);
      break;
    case CSM_CODE_LOCAL_END_PERIOD:
      GetLock(&global_data.sampler_lock, tid+1);
      sampler->set_thread_enable(tid, false);
      ReleaseLock(&global_data.sampler_lock);
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

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    GetLock(&global_data.sampler_lock, threadid+1);
    printf("thread begin %d\n", threadid);
    thread_data_t *tdata = new thread_data_t(threadid);
    PIN_SetThreadData(tls_key, tdata, threadid);
    numThreads++;
    get_tls(threadid)->refcount = KnobSampleInterval.Value();
    sampler->Allocate(threadid);
    ReleaseLock(&global_data.sampler_lock);
}

VOID Fini(INT32 code, VOID *v) {
  INT64 total_refs = 0;
  INT64 total_samples = 0;
  for (INT32 t = 0; t < numThreads; t++) {
       thread_data_t* tdata = get_tls(t);
       printf("thread %d samplecount %ld totalrefs %ld, refcount %ld, sum %ld\n", t,
              tdata->samplecount, tdata->totalrefs, tdata->refcount,
              tdata->totalrefs - tdata->refcount);
       total_refs += tdata->totalrefs;// - tdata->refcount;
       total_samples += tdata->samplecount;
  }
  //printf("global samplecount %ld\n", global_data.samplecount);
  printf("%d BBLs %.2f refs/BBL\n", bbl_count, static_cast<double>(bbl_ref_count) / bbl_count);
  printf("%d ref ins, %.2f refs/ref ins\n",
         bbl_ref_count, static_cast<double>(bbl_ref_count) / ref_ins);
  std::string version(std::string("#") + __FILE__ + " version " + PINRD_GIT_VERSION + "\n");
  std::string samplerate(std::string("#sample interval ") + decstr(KnobSampleInterval.Value()));
  INT64 address_samples = sampler->DumpStats(library_map.GetPythonString() + version + samplerate);
  printf("total samples %ld, refs %ld, sampling %.2f%% of time\n",
         total_samples, total_refs, static_cast<double>(total_samples) / total_refs * 100.0);
  printf("overall average sampled addrs per ref: %.2f\n",
         static_cast<double>(address_samples) / total_refs);
  delete sampler;
}

void InitSampler() {
  if (KnobSampledStackType.Value() == "private") {
    sampler = new SampledReuseStack(KnobOutputFile.Value() + "_sampled", KnobGranularity.Value());
  } else if (KnobSampledStackType.Value() == "shared") {
    sampler = new SharedSampledReuseStack(KnobOutputFile.Value() + "_sampled",
                                          KnobGranularity.Value());
  } else {
    throw std::invalid_argument("stack type must be \"private\" or \"shared\"");
  }
  sampler->set_global_enable(false);
}

int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(0);
    InitSampler();

    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddFiniFunction(Fini, 0);
    INS_AddInstrumentFunction(InstrumentMagicInstruction, 0);
    TRACE_AddInstrumentFunction(InstrumentTraceForRefcount, 0);
    TRACE_AddInstrumentFunction(InstrumentTraceForAnalysis, 0);
    IMG_AddInstrumentFunction(InstrumentLibraryMap, 0);

    // Activate alarm, must be done before PIN_StartProgram
    //controller.CheckKnobs(Handler, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
