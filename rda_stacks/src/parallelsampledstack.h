/*
 * parallelsampledstack.h
 *
 *  Created on: Feb 9, 2010
 *      Author: dschuff
 */

#ifndef PARALLELSAMPLEDSTACK_H_
#define PARALLELSAMPLEDSTACK_H_

#include <stdio.h>
#include <string>
#include <tr1/unordered_set>
#include "rda-sync.h"
#include "reusestack-common.h"
#include "reusestackstats.h"
#include "trace.h"

#define CACHE_LINE_SIZE 64
#define DISTANCE_SET_INIT_BUCKETS 4000
#define WRITE_SET_INIT_BUCKETS 400

// put shared writeable global data in a struct to control the layout (well, control it better)
struct PssGlobalData {
  PssGlobalData() : thread_count(0), synchronize(false), finalize_needed(false),
      merge_needed(false), barrier(),
#if defined(UNITTEST)
      last_finalized_distance(0),
#endif
      active_sample_count(0) {
    RdaInitLock(&threads_lock);
    RdaInitLock(&stats_lock);
  }
    RdaLock threads_lock;
    int thread_count;
    volatile int synchronize;
    volatile int finalize_needed;
    volatile int merge_needed;
    char buf1[CACHE_LINE_SIZE - sizeof(RdaLock) - sizeof(int) - sizeof(int) - sizeof(int)];
  //MultistageBarrier<3> barrier __attribute__((aligned(64)));
#if !defined(UNITTEST) && !defined(SEQUENTIAL)
  MultistageBarrier barrier __attribute__((aligned(64)));
  acc_count_t last_finalized_distance; // for testing
#else
  FakeBarrier barrier;
  acc_count_t last_finalized_distance; // for testing
#endif
  //char buf2[CACHE_LINE_SIZE - sizeof(Barrier)];
  //Barrier barrier2 __attribute__((aligned(64)));
  //char buf3[CACHE_LINE_SIZE - sizeof(Barrier)];
  volatile int active_sample_count __attribute__((aligned(64)));
  //char buf4[CACHE_LINE_SIZE - sizeof(int)];
  RdaLock stats_lock __attribute__((aligned(64)));
  //ReuseStackStats stats;
  PCStats pc_stats;
  PCStats read_pc_stats;
};

class ParallelSampledStack;
typedef std::tr1::unordered_set<address_t> AddressSet;
struct WriteSet;

struct DistanceSet {
  enum FinalizeStatus { kActive = 0, kReuseFinalize, kInvalidateFinalize,
                        kPruneFinalize, kRemoteReuseFinalize };
  explicit DistanceSet(address_t addr) : sample_addr(addr), set(DISTANCE_SET_INIT_BUCKETS), holes(0),
      finalize(kActive), final_pc(0), write_sets(NULL), next(NULL) {}
  ~DistanceSet() {next = NULL; write_sets = (WriteSet *)-1; finalize = (FinalizeStatus)-2;}
  const address_t sample_addr;
  AddressSet set;
  int holes;
  acc_count_t creation_time; //creation time measured in references, also holds lifetime in finalize
  // used for finalizing
  FinalizeStatus finalize;
  address_t final_pc; // PC of the finalizing access. for pruned/cold referenes, use the initial PC
  bool final_ref_is_write;
  WriteSet *write_sets; // link to a chain of write sets, used for finalizing
  // for distance_sets_ list
  DistanceSet *next;
};

struct WriteSet {
  WriteSet(address_t addr, DistanceSet *owning_ds) : sample_addr(addr), set(WRITE_SET_INIT_BUCKETS),
      owner(owning_ds), next(NULL) {}
  address_t sample_addr;
  AddressSet set;
  acc_count_t creation_time;
  DistanceSet *owner;
  WriteSet *next;
};

#define DEBUG
class EventBuffer {
public:
  enum Event {
    kNewAddress, kFinalizeSelf, kFinalizeInval, kMerge, kMergeNeeded, kAdjacentSleeper, kNoAction
  };
  EventBuffer() : position_(0) {}
#ifdef DEBUG
  const static int kBufferSize = 32;
  void Log(Event ev) {
    log_[position_] = ev;
    position_ = (position_ + 1) % kBufferSize;
  }
#else
  const static int kBufferSize = 1;
  void Log(Event ev) {}
#endif
private:
  //use array so gdb can handle it
  int log_[kBufferSize];
  int position_;
};

class ParallelSampledStack {
public:
  enum StackType { kPrivateStacks, kSharedStacks };
  const static int kDefaultGranularity = 64;
  const static int kDefaultThreads = 4;
  friend class ParallelSampledStackTest;
  static bool Initialize(const std::string &output_filename, int granularity,
                         int threads, StackType stack_type);
  static void CleanUp();
  static void SetGlobalEnable(bool enable) { global_enabled_ = enable; }
  static ParallelSampledStack * GetThreadStack(int thread);
  void MergeAllSamples();
  static int64_t DumpStatsPython(const std::string &extra);//fully synchronized, no need for better
  static bool HasActiveSamples() { return global_rw_->active_sample_count != 0; }
  static void ActivateSampledAddress() { AtomicIncrement(&global_rw_->active_sample_count); }
  void NewSampledAddress(address_t address, address_t PC);//fully synchronized, or finegrain?
  bool Access(address_t address, address_t PC, bool is_write);// update local set
  void SetThreadEnable(bool enable) { enabled_ = enable; }
  void Sleep() { global_rw_->barrier.Sleep(threadid_); }
  void Wake() { global_rw_->barrier.Wake(threadid_); }
private:
  const DistanceSet *GetDS(address_t address);
  static void ValidateActiveSamples(bool has_new);
  enum SyncAction { kNoAction, kNewSampledAddress, kMerge, kFinalize };
  ParallelSampledStack(int threadid);
  address_t GetBlock(address_t address) { return address & block_mask_; }
  void SynchronizedOperations(SyncAction action, DistanceSet *newDS);
  static void RemoveWriteSetFromAll(DistanceSet *ds, int thread);
  static void MergeInvalidationsThreads(int thread, DistanceSet *myDS);
  static void MergeInvalidationsList(DistanceSet *myDS, int thread);
  static void MergeDistanceSetsThreads(int thread, DistanceSet *myDS);
  static void MergeDistanceSetsList(DistanceSet *myDS, int thread);
  /*static*/ void FinalizeSample(int thread, DistanceSet *myDS, address_t PC);
  static int RecordLeftovers();
  bool CheckOldestSample();

  //local data: vector of sets, for each active sampled address
  DistanceSet *distance_sets_;  //one for each of my sampled addresses
  DistanceSet *oldest_distance_set_;
  RdaLock write_set_lock_;
  WriteSet *write_sets_; //one for each of other threads's sampled addresses
  bool enabled_;
  int threadid_;
  EventBuffer events_;
  static ThreadedOutputTrace trace_;
  //local stats
  //acc_count_t sample_count_;  // number of distance samples tracked in stats
  acc_count_t sampled_access_count_;  // number of accesses with an active sample
  acc_count_t synchronization_count_;
  int64_t addresses_per_sample_total_;
  int64_t reference_lifetime_total_;
  acc_count_t invalidation_count_; // how many sampled addrs get invalidated while active
  acc_count_t prune_count_;
  ReuseStackStats private_stats_;
  ReuseStackStats private_read_stats_;
  bool new_thread_;

  // write-once(/rarely) global data
  static bool initialized_;
  static FILE *output_file_;
  static int block_bytes_;
  static address_t block_mask_;
  static bool global_enabled_;
  static StackType stack_type_;
  static CircularThreadQueue<ParallelSampledStack *> threads_;

  //read-write global data
  static PssGlobalData *global_rw_;


  DISALLOW_COPY_AND_ASSIGN(ParallelSampledStack);
};

#endif /* PARALLELSAMPLEDSTACK_H_ */
