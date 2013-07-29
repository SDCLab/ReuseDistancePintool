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
#include <stdint.h>
#include <boost/lexical_cast.hpp>
#include "pin.H"
#include "instlib.H"
#include "librarymap.h"
#include "magicinstruction.h"
#include "stackholder.h"
#include "version.h"

using INSTLIB::CONTROL;
using INSTLIB::CONTROL_EVENT;
using INSTLIB::CONTROL_START;
using INSTLIB::CONTROL_STOP;

CONTROL controller;
PIN_LOCK stacks_lock;
StackHolder *stacks1;
StackHolder *stacks2;
LibraryMap library_map;

acc_count_t ref_count = 0;
double total_error = 0.0;
double worst_error = 0.0;

std::string kOutputFileName = "reusedistance.out";
std::string kDefaultStackType = "approximate";

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", kOutputFileName,
                            "specify output file name");

KNOB<int> KnobGranularity(KNOB_MODE_WRITEONCE, "pintool", "g",
                          boost::lexical_cast<std::string>(StackHolder::kDefaultGranularity),
                          "specify analysis granularity");

KNOB<string> KnobStackType(KNOB_MODE_WRITEONCE, "pintool", "s", kDefaultStackType,
                           "specify stack implementation type");

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

// Print a memory read record
VOID RecordMemRead(VOID * ip, ADDRINT addr, ADDRINT size, THREADID tid) {
  //if (size > 8) printf("Large read sz %ld at %p\n", size, ip);
  GetLock(&stacks_lock, 1);
  acc_count_t d1 = stacks1->Access(tid, addr, size, reinterpret_cast<address_t>(ip), false);
  acc_count_t d2 = stacks2->Access(tid, addr, size, reinterpret_cast<address_t>(ip), false);
  double error = d1 == 0 ? 0 : static_cast<double>(d2 - d1) / d1;
  if (error > worst_error) worst_error = error;
  total_error += error;
  ref_count++;
  ReleaseLock(&stacks_lock);
}

// Print a memory write record
VOID RecordMemWrite(VOID * ip, ADDRINT addr, ADDRINT size, THREADID tid) {
  //if (size > 8) printf("Large write sz %ld at %p\n", size, ip);
  GetLock(&stacks_lock,2);
  acc_count_t d1 = stacks1->Access(tid, addr, size, reinterpret_cast<address_t>(ip), true);
  acc_count_t d2 = stacks2->Access(tid, addr, size, reinterpret_cast<address_t>(ip), true);
  double error = d1 == 0 ? 0 : static_cast<double>(d2 - d1) / d1;
  if (error > worst_error) worst_error = error;
  total_error += error;
  ref_count++;
  ReleaseLock(&stacks_lock);
}

// Is called for every instruction and instruments reads and writes
VOID InstrumentReadsWrites(INS ins, VOID *v) {
    // instruments loads using a predicated call, i.e.
    // the call happens iff the load will be actually executed
    // (this does not matter for ia32 but arm and ipf have predicated instructions)
    if (INS_IsMemoryRead(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
            IARG_INST_PTR,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_THREAD_ID,
            IARG_END);
	
    }
    if (INS_HasMemoryRead2(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
            IARG_INST_PTR,
            IARG_MEMORYREAD2_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_THREAD_ID,
            IARG_END);

    }

    // instruments stores using a predicated call, i.e.
    // the call happens iff the store will be actually executed
    if (INS_IsMemoryWrite(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
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
      stacks1->set_global_enable(true);
      stacks2->set_global_enable(true);
      break;
    case CSM_CODE_STOP_TIMER:
      // call EndParallelRegion etc and stop instrumentation. same as above
      stacks1->EndParallelRegion();
      stacks1->UpdateRatioPredictions();
      stacks2->EndParallelRegion();
      stacks2->UpdateRatioPredictions();
      printf("stacks global disable\n");
      stacks1->set_global_enable(false);
      stacks2->set_global_enable(false);
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
      stacks1->EndParallelRegion();
      stacks2->EndParallelRegion();
      break;
    case CSM_CODE_END_PERIOD:
      stacks1->EndParallelRegion();
      stacks1->UpdateRatioPredictions();
      stacks2->EndParallelRegion();
      stacks2->UpdateRatioPredictions();
      break;
    case CSM_CODE_LOCAL_START_PERIOD:
      stacks1->SetThreadEnabled(tid, true);
      stacks2->SetThreadEnabled(tid, true);
      break;
    case CSM_CODE_LOCAL_END_PERIOD:
      stacks1->SetThreadEnabled(tid, false);
      stacks2->SetThreadEnabled(tid, false);
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
  stacks1->EndParallelRegion();
  stacks1->UpdateRatioPredictions();
  std::string version(std::string("#") + __FILE__ + " version " + PINRD_GIT_VERSION + "\n");
  stacks1->DumpStatsPython(library_map.GetPythonString() + version);
  stacks2->EndParallelRegion();
  stacks2->UpdateRatioPredictions();
  stacks2->DumpStatsPython(library_map.GetPythonString() + version);
  delete stacks1;
  delete stacks2;
  printf("worst error %.2f, avg error %.3f\n", worst_error, total_error / ref_count);
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    GetLock(&stacks_lock, threadid+1);
    printf("thread begin %d\n", threadid);
    stacks1->Allocate(threadid);
    stacks2->Allocate(threadid);
    ReleaseLock(&stacks_lock);
}

int InitStackHolder(StackHolder **stacks, const string &filename, const string &stacktype) {
  //const int kRatioPredictionSizes[3] = {262144, 524288, 1048576};
  if (KnobGranularity.Value() != StackHolder::kDefaultGranularity) {
    printf("Using granularity of %d\n", KnobGranularity.Value());
  }
  try {
    *stacks = new StackHolder(filename, KnobGranularity.Value(),
                             stacktype);
  } catch (std::exception& e) {
    fprintf(stderr, "StackHolder constructor threw exception: %s\n", e.what());
    return -1;
  }
  StackHolder *s = *stacks;
  s->set_do_inval(true);
  s->set_do_shared(false);
  s->set_do_sim_stacks(true);
  s->set_do_single_stacks(false);
  s->set_do_lazy_stacks(false);
  s->set_do_oracular_stacks(false);
  // for now use this instead of enabling or disabling instrumentation
  s->set_global_enable(false);

//  for (int i = 0; i < 3; i++) {
//    // this is only setup for 4 threads
//    stacks->AddRatioPredictionSize(kRatioPredictionSizes[i]);
//    stacks->AddPairPredictionSize(kRatioPredictionSizes[i] * 2);
//    stacks->AddSharedPredictionSize(kRatioPredictionSizes[i] * 4);
//  }
  return 0;
}

int main(int argc, char *argv[])
{
    PIN_Init(argc, argv);

    InitLock(&stacks_lock);
    if (InitStackHolder(&stacks1, KnobOutputFile.Value() + "_exact", "exact") != 0 ||
        InitStackHolder(&stacks2, KnobOutputFile.Value() + "_approx", "approximate") != 0) {
      return 0;
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
