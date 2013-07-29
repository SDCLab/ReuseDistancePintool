/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2007 Intel Corporation 
All rights reserved. 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/*
 *  This file contains a PIN tool for reuse distance analysis.
 */

#include "reusedistance.h"

#include <stdio.h>
#include <sched.h>
#include <tr1/unordered_map>
#include <boost/lexical_cast.hpp>
#include "pin.H"
#include "instlib.H"
#include "librarymap.h"
#include "magicinstruction.h"
#include "stackholder.h"
#include "ticket_lock.h"
#include "futex_lock.h"
#include "version.h"

using INSTLIB::CONTROL;
using INSTLIB::CONTROL_EVENT;
using INSTLIB::CONTROL_START;
using INSTLIB::CONTROL_STOP;

#define SINGLE_THREAD
//#define USE_TICKETLOCK

#ifndef SINGLE_THREAD
#ifdef USE_TICKETLOCK
#define LOCK_T ticket_spinlock_t
#define GET_LOCK(l) __ticket_spin_lock(l)
#define RELEASE_LOCK(l) __ticket_spin_unlock(l)
#define INIT_LOCK(l) __ticket_spin_init(l)
#elif defined(USE_FUTEXLOCK)
#define LOCK_T FutexLock
#define GET_LOCK(l) FutexGetLock(l)
#define RELEASE_LOCK(l) FutexReleaseLock(l)
#define INIT_LOCK(l) FutexInitLock(l)
#else
#define LOCK_T PIN_LOCK
#define GET_LOCK(l) GetLock(l, 1)
#define RELEASE_LOCK(l) ReleaseLock(l)
#define INIT_LOCK(l) InitLock(l)
#endif //USE_TICKET/FUTEX
#else // SINGLE_THREAD
#define LOCK_T long
#define GET_LOCK(l)
#define RELEASE_LOCK(l)
#define INIT_LOCK(l)
#endif


#define CONSECUTIVE_REFS_THRESH 32

CONTROL controller;
LOCK_T stacks_lock;
address_t inst_buffers[8] = {0, 0, 0, 0, 0, 0, 0, 0};

bool enabled;
THREADID last_thread = 0;
//int consecutive_references = 0;
StackHolder *stacks;
bool fetch_enabled;
LibraryMap library_map;


std::string kOutputFileName = "reusedistance.out";
std::string kDefaultStackImpl = "exact";
std::string kDefaultStackSharing = "private";

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", kOutputFileName,
                            "specify output file name");

KNOB<int> KnobGranularity(KNOB_MODE_WRITEONCE, "pintool", "g",
                          boost::lexical_cast<std::string>(StackHolder::kDefaultGranularity),
                          "specify analysis granularity");

KNOB<string> KnobStackImpl(KNOB_MODE_WRITEONCE, "pintool", "sti", kDefaultStackImpl,
                           "specify stack implementation type");

KNOB<string> KnobStackSharing(KNOB_MODE_WRITEONCE, "pintool", "s", kDefaultStackSharing,
                              "specify private or shared stacks");

KNOB<BOOL> KnobDoFetch(KNOB_MODE_WRITEONCE, "pintool", "f", "false",
                         "specify whether to include instruction fetches");

KNOB<BOOL> KnobDoPrefetch(KNOB_MODE_WRITEONCE, "pintool", "p", "false",
                         "specify whether to perform prefetches");


//handler to set/unset instrumentation
VOID Handler(CONTROL_EVENT ev, VOID * v, CONTEXT * ctxt, VOID * ip, THREADID tid)
{
  printf("PC %p", ip);
  switch(ev) {
  case CONTROL_START:
    printf(" Start\n");
    break;
  case CONTROL_STOP:
    printf(" Stop\n");
    break;
  default:
    ASSERTX(false);
    break;
  }
}
address_t kInstMask = ~0xF;
//address_t
VOID PIN_FAST_ANALYSIS_CALL RecordFetch(VOID * ip, UINT32 size, THREADID tid) {
  if (!enabled) return;
  address_t pc = reinterpret_cast<address_t>(ip);;
  GET_LOCK(&stacks_lock);
  //  printf("tid %d ip %p size %d buf %lx %s\n", tid, ip, size, inst_buffers[tid], (pc & kInstMask) == inst_buffers[tid] ? "hit" : "miss");
  if ((pc & kInstMask) != inst_buffers[tid]) {
    inst_buffers[tid] = pc & kInstMask;
    stacks->Fetch(tid, inst_buffers[tid], 16);
  }
  if (((pc + size) & kInstMask) != inst_buffers[tid]) {
    inst_buffers[tid] = (pc + size) & kInstMask;
    stacks->Fetch(tid, inst_buffers[tid], 16);
  }
  RELEASE_LOCK(&stacks_lock);
}

// Print a memory read record
VOID PIN_FAST_ANALYSIS_CALL RecordMemRead(VOID * ip, ADDRINT addr, UINT32 size, THREADID tid) {
  //if (size > 8) printf("Large read sz %ld at %p\n", size, ip);
  if (!enabled) return;
  GET_LOCK(&stacks_lock);
  //  if (last_thread == tid) consecutive_references++;
  //else consecutive_references = 0;
  //int refs = consecutive_references;
  //last_thread = tid;
  stacks->Access(tid, addr, size, reinterpret_cast<address_t>(ip), false);
  RELEASE_LOCK(&stacks_lock);
  //if (refs > CONSECUTIVE_REFS_THRESH) sched_yield();
}

// Print a memory write record
VOID PIN_FAST_ANALYSIS_CALL RecordMemWrite(VOID * ip, ADDRINT addr, UINT32 size, THREADID tid) {
  //if (size > 8) printf("Large write sz %ld at %p\n", size, ip);
  if (!enabled) return;
  GET_LOCK(&stacks_lock);
  //if (last_thread == tid) consecutive_references++;
  //else consecutive_references = 0;
  //int refs = consecutive_references;
  //last_thread = tid;
  stacks->Access(tid, addr, size, reinterpret_cast<address_t>(ip), true);
  RELEASE_LOCK(&stacks_lock);
  //if (refs > CONSECUTIVE_REFS_THRESH) sched_yield();
}

VOID InstrumentFetches(INS ins, VOID *v) {
  INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordFetch, IARG_FAST_ANALYSIS_CALL,
			   IARG_INST_PTR, IARG_UINT32, INS_Size(ins), IARG_THREAD_ID, IARG_END);
}

// Is called for every instruction and instruments reads and writes
VOID InstrumentReadsWrites(INS ins, VOID *v) {
    // instruments loads using a predicated call, i.e.
    // the call happens iff the load will be actually executed
    // (this does not matter for ia32 but arm and ipf have predicated instructions)
    int reads = 0;
    if (INS_IsMemoryRead(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_FAST_ANALYSIS_CALL,
            IARG_INST_PTR,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_THREAD_ID,
            IARG_END);
        reads++;
    }
    if (INS_HasMemoryRead2(ins))
    {
      // fetched always = 1 here
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead, IARG_FAST_ANALYSIS_CALL,
            IARG_INST_PTR,
            IARG_MEMORYREAD2_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_THREAD_ID,
            IARG_END);
        reads++;
    }

    // instruments stores using a predicated call, i.e.
    // the call happens iff the store will be actually executed
    if (INS_IsMemoryWrite(ins))
    {
      INS_InsertPredicatedCall(
          ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite, IARG_FAST_ANALYSIS_CALL,
          IARG_INST_PTR,
          IARG_MEMORYWRITE_EA,
          IARG_MEMORYWRITE_SIZE,
          IARG_THREAD_ID,
          IARG_END);
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
      stacks->set_global_enable(true);
      enabled = true;
      break;
    case CSM_CODE_STOP_TIMER:
      // call EndParallelRegion etc and stop instrumentation. same as above
      enabled = false;
      GET_LOCK(&stacks_lock);
      if (stacks->get_global_enable()) {
        stacks->EndParallelRegion();
        stacks->UpdateRatioPredictions();
        stacks->set_global_enable(false);
      }
      RELEASE_LOCK(&stacks_lock);
      printf("stacks global disable\n");
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
      stacks->EndParallelRegion();
      break;
    case CSM_CODE_END_PERIOD:
      stacks->EndParallelRegion();
      stacks->UpdateRatioPredictions();
      break;
    case CSM_CODE_LOCAL_START_PERIOD:
#ifndef SINGLE_THREAD
      stacks->SetThreadEnabled(tid, true);
#endif
      break;
    case CSM_CODE_LOCAL_END_PERIOD:
#ifndef SINGLE_THREAD
      stacks->SetThreadEnabled(tid, false);
#endif
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

VOID Fini(INT32 code, VOID *v)
{
  stacks->EndParallelRegion();
  stacks->UpdateRatioPredictions();
  std::string version(std::string("#") + __FILE__ + " version " + PINRD_GIT_VERSION + "\n");
  stacks->DumpStatsPython(library_map.GetPythonString() + version);
  delete stacks;
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
#ifdef SINGLE_THREAD
    static int thread_count = 0;
    if (__sync_add_and_fetch(&thread_count, 1) > 1) {
      throw std::runtime_error(">1 thread with SINGLE_THREAD defined");
    }
#endif
    GET_LOCK(&stacks_lock);
    printf("thread begin %d\n", threadid);
    stacks->Allocate(threadid);
    RELEASE_LOCK(&stacks_lock);
}

int InitStackHolder() {
  //  const int kRatioPredictionSizes[5] = {32768, 65536, 524288, 2*1024*1024, 4*1024*1024 };
  if (KnobGranularity.Value() != StackHolder::kDefaultGranularity) {
    printf("Using granularity of %d\n", KnobGranularity.Value());
  }
  if (KnobStackImpl.Value() != kDefaultStackImpl) {
    printf("Using %s stacks\n", KnobStackImpl.Value().c_str());
  }
  try {
    stacks = new StackHolder(KnobOutputFile.Value(), KnobGranularity.Value(), KnobStackImpl.Value());
  } catch (std::exception& e) {
    fprintf(stderr, "StackHolder constructor threw exception: %s\n", e.what());
    return -1;
  }
  if (KnobStackSharing.Value() == "private") {
    stacks->set_do_inval(true);
    stacks->set_do_shared(false);
    stacks->set_do_sim_stacks(true);
    stacks->set_do_single_stacks(false);
    stacks->set_do_lazy_stacks(false);
    stacks->set_do_oracular_stacks(false);
    stacks->set_do_fetch(KnobDoFetch.Value());
    stacks->set_do_prefetch(KnobDoPrefetch.Value());
  } else if (KnobStackSharing.Value() == "shared") {
    stacks->set_do_inval(false);
    stacks->set_do_shared(true);
    stacks->set_do_sim_stacks(false);
    stacks->set_do_single_stacks(false);
    stacks->set_do_lazy_stacks(false);
    stacks->set_do_oracular_stacks(false);
  } else {
    fprintf(stderr, "bad value for stack sharing type: must be private or shared\n");
    delete stacks;
    return -1;
  }
  // for now use this instead of enabling or disabling instrumentation
  stacks->set_global_enable(false);
  enabled = false;

  for (int i = 0; i < 5; i++) {
//    // this is only setup for 4 threads
//    stacks->AddRatioPredictionSize(kRatioPredictionSizes[i]);
//    stacks->AddPairPredictionSize(kRatioPredictionSizes[i] * 2);
//    stacks->AddSharedPredictionSize(kRatioPredictionSizes[i] * 4);
  }
  return 0;
}

int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);

    INIT_LOCK(&stacks_lock);
    if (InitStackHolder() != 0) {
      return 0;
    }

    if (KnobDoFetch.Value()) {
      INS_AddInstrumentFunction(InstrumentFetches, 0);
    }

    INS_AddInstrumentFunction(InstrumentReadsWrites, 0);
    INS_AddInstrumentFunction(InstrumentMagicInstruction, 0);
    IMG_AddInstrumentFunction(InstrumentLibraryMap, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Activate alarm, must be done before PIN_StartProgram
    controller.CheckKnobs(Handler, 0);

    // Never returns
    PIN_StartProgram();
    
    return 0;
}
