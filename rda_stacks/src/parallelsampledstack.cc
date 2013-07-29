/*
 * parallelsampledstack.cc
 *
 *  Created on: Feb 9, 2010
 *      Author: dschuff
 */

#include "parallelsampledstack.h"
#include <list>
#include <stdexcept>
#include <assert.h>
#include <boost/format.hpp>
#include "version.h"

const int kMaxThreads = 16;
const int kPruneSampleThreshold = 100;
const double kPruneTarget = 0.99;

bool ParallelSampledStack::initialized_ = false;
FILE *ParallelSampledStack::output_file_;
int ParallelSampledStack::block_bytes_;
address_t ParallelSampledStack::block_mask_;
bool ParallelSampledStack::global_enabled_;
ParallelSampledStack::StackType ParallelSampledStack::stack_type_;
CircularThreadQueue<ParallelSampledStack *> ParallelSampledStack::threads_;

PssGlobalData *ParallelSampledStack::global_rw_;
ThreadedOutputTrace ParallelSampledStack::trace_;

bool ParallelSampledStack::Initialize(const std::string &output_filename, int granularity,
                                      int threads, StackType stack_type) {
  //initialize locks
  if (granularity < 1) {
    printf("bad granularity value\n");
    return false;
  }
  output_file_ = fopen(output_filename.c_str(), "w");
  if (output_file_ == NULL) {
    printf("could not open output file\n");
    return false;
  }
  block_bytes_ = granularity;
  block_mask_ = static_cast<address_t>(-1);
  while (granularity > 1) {
    block_mask_ <<= 1;
    granularity >>= 1;
  }
  if ((block_bytes_ & static_cast<int>(block_mask_)) != block_bytes_) {
    block_bytes_ = block_bytes_ & block_mask_;
    printf("warning: granularity must be a power of 2, using %d instead\n", block_bytes_);
  }
  stack_type_ = stack_type;
  threads_.Initialize(kMaxThreads);
  // in Pin, thread ids start at 0 so this array can be indexed by thread id
  //threads_.resize(threads, NULL);
  global_rw_ = new PssGlobalData();
  global_rw_->barrier.Init(&threads_);
  RdaInitLock(&global_rw_->threads_lock);
  trace_.Init(output_filename + "-trace");
  global_enabled_ = false;
  initialized_ = true;
  return true;
}

void ParallelSampledStack::CleanUp() {
  initialized_ = false;
  if (output_file_ != NULL) {
    fclose(output_file_);
    output_file_ = NULL;
  }
  for (int i = 0; i < threads_.GetThreadCount(); i++) {
    DistanceSet *ds = threads_[i]->distance_sets_;
    while(ds) {
      DistanceSet *del = ds;
      ds = ds->next;
      delete del;
    }
    WriteSet *ws = threads_[i]->write_sets_;
    while(ws) {
      WriteSet *del = ws;
      ws = ws->next;
      delete del;
    }
    delete threads_[i];
  }
  trace_.CleanUp();
  delete global_rw_;
}

ParallelSampledStack::ParallelSampledStack(int threadid) : distance_sets_(NULL), write_sets_(NULL),
    enabled_(false), threadid_(threadid), sampled_access_count_(0),
    synchronization_count_(0), addresses_per_sample_total_(0), reference_lifetime_total_(0),
    invalidation_count_(0), prune_count_(0), private_stats_(block_bytes_), private_read_stats_(block_bytes_),
    new_thread_(true) {
  if (!initialized_) {
    throw std::runtime_error("ParallelSampledStack::Initialize must be called before any instantiation");
  }
  RdaInitLock(&write_set_lock_);
}

ParallelSampledStack * ParallelSampledStack::GetThreadStack(int thread) {
  if (thread < 0) return NULL;
  if (!initialized_) return NULL;
  LockHolder lh(&global_rw_->threads_lock);
  if (global_rw_->thread_count > thread && threads_[thread] != NULL) {
    return threads_[thread];
  }
  if (thread != global_rw_->thread_count) throw std::invalid_argument("threads must be added in tid order");
  ParallelSampledStack * p = new ParallelSampledStack(thread);
  // must add thread to barrier first so that if the other threads hit the barrier they will wait
  // for this thread before touching threads_
  if (global_rw_->barrier.AddThread() != thread) {
    throw std::runtime_error("thread returned by barrier != tid");
  }
  if (threads_.AddThread(p) == -1) throw std::runtime_error("max_threads exceeded");
  global_rw_->thread_count++;
  assert(global_rw_->thread_count == threads_.GetThreadCount());
  threads_[thread]->SetThreadEnable(true);
  global_rw_->synchronize = 1;
  global_rw_->merge_needed = true;
  return p;
}

void ParallelSampledStack::MergeAllSamples() {
  global_rw_->synchronize = 1;
  global_rw_->merge_needed = true;
  events_.Log(EventBuffer::kMerge);
  trace_.TraceMerge(threadid_);
  SynchronizedOperations(kMerge, NULL);
}

void ParallelSampledStack::SynchronizedOperations(SyncAction action, DistanceSet *newDS) {
  //entered if sync flag is set
  int generation;
  synchronization_count_++;
  if ((generation = global_rw_->barrier.WaitStart(threadid_)) != 0) {
    bool merge_needed = global_rw_->merge_needed;  // cache these values so we can reset them
    bool finalize_needed = global_rw_->finalize_needed;
    //gfn = global_rw_->finalize_needed;
    // first stage. threads may update each other's write sets (each thread's sets list has a lock)
    int end_sleepers = -2;//threads_.GetNextIndex(threadid_);
    if (merge_needed || finalize_needed) {
      events_.Log(EventBuffer::kMergeNeeded);
      end_sleepers = global_rw_->barrier.GetAdjacentSleepers(threadid_);
      if (end_sleepers == threadid_) end_sleepers = -1;  // all threads but this one are asleep
      if (end_sleepers == -1) events_.Log(EventBuffer::kAdjacentSleeper);
    }
    if (finalize_needed) {
      // some thread reused/invalidated one of this thread's samples
      // remove the sample from all writesets
      for (int t = threadid_; t != end_sleepers; t = threads_.GetNextIndex(t)) {
        DistanceSet *ds = threads_[t]->distance_sets_;
        while (ds) {
          if (ds->finalize != DistanceSet::kActive) {
            RemoveWriteSetFromAll(ds, t);
          }
          ds = ds->next;
        }
        if (end_sleepers == -1 && threads_.GetNextIndex(t) == threadid_) break;
      }
    } else if (action == kFinalize){
      throw std::runtime_error("kfinalize without global finalize");
    }
    if (action == kNewSampledAddress) {
      // add to everyone's write sets (acquire lock for each)
      for (int i = threads_.GetNextIndex(threadid_); i != threadid_; i = threads_.GetNextIndex(i)) {
        WriteSet *ws = new WriteSet(newDS->sample_addr, newDS);
        ws->creation_time = threads_[i]->sampled_access_count_;
        LockHolder lh(&threads_[i]->write_set_lock_);
        ws->next = threads_[i]->write_sets_;
        threads_[i]->write_sets_ = ws;
      }
    }
    else if (new_thread_) {  // action will be kMerge in this case
      // for new threads, populate our write sets with everyone's distance set data
      LockHolder lh(&write_set_lock_);
      for (int i = threads_.GetNextIndex(threadid_); i != threadid_; i = threads_.GetNextIndex(i)) {
        DistanceSet *ds = threads_[i]->distance_sets_;
        while (ds) {
          WriteSet *ws = new WriteSet(ds->sample_addr, ds);
          ws->next = write_sets_;
          write_sets_ = ws;
          ds = ds->next;
        }
      }
      new_thread_ = false;
    }
    ValidateActiveSamples(newDS != NULL);
    global_rw_->barrier.WaitStage(1, threadid_);
    // second stage - merging and finalizing. every thread's write sets must be stable
    if (merge_needed || finalize_needed) {
      // if all threads but this are asleep, need to do all threads (but only once) so end_sleepers
      // becomes -1 and the special case below keeps it to one loop around (this also works for
      // the case of only one total thread)
      for (int t = threadid_; t != end_sleepers; t = threads_.GetNextIndex(t)) {
        DistanceSet *ds = threads_[t]->distance_sets_;
        DistanceSet *prev = NULL;
        while (ds) {
          if (ds->finalize != DistanceSet::kActive) {
            // finalize and unlink
            DistanceSet *del = ds;
            // could just unlink now and then finalize after sync period if t == threadid_
            FinalizeSample(t, ds, ds->final_pc);
            if (threads_[t]->oldest_distance_set_ == ds) {
              assert(ds->next == NULL);
              threads_[t]->oldest_distance_set_ = prev;
            }
            if (prev == NULL) {
              threads_[t]->distance_sets_ = ds->next;
            } else {
              assert(prev->next == ds);
              prev->next = ds->next;
            }
            ds = ds->next;
            delete del;
          } else {
            if (merge_needed) { // just do this anyway?
              if (stack_type_ == kPrivateStacks) MergeInvalidationsThreads(t, ds);
              else MergeDistanceSetsThreads(t, ds);
            }
            prev = ds;
            ds = ds->next;
          }
        }
        if (end_sleepers == -1 && threads_.GetNextIndex(t) == threadid_) break;
      }
      global_rw_->merge_needed = false;
      global_rw_->finalize_needed = false;
    }
    global_rw_->synchronize = 0;
    global_rw_->barrier.WaitStage(2, threadid_);

    // add our new distance set after the synchronized phase so new threads won't get double
    // write sets for it
    if (action == kNewSampledAddress) {
      if (distance_sets_ == NULL) oldest_distance_set_ = newDS;
      newDS->next = distance_sets_;
      distance_sets_ = newDS;
    }
    //global_rw_->barrier.WaitStage(3, threadid_); // not needed.
  } else {
    throw std::runtime_error("Should not hit else case in Sync");
  }
}

//only want to merge when we have a sample

DistanceSet *FindDS(DistanceSet *ds, address_t address) {
  while (ds && ds->sample_addr != address) {
    ds = ds->next;
  }
  return ds;
}

void ParallelSampledStack::NewSampledAddress(address_t address, address_t PC) {
  address = GetBlock(address);
  if (!enabled_ || !global_enabled_ || (FindDS(distance_sets_, address) != NULL)){
    // SampleTrigger has incremented this but we are going to ignore it, so decrement again
    AtomicDecrement(&global_rw_->active_sample_count);
    return;
  }
  //trace_.TraceNewSampledAddress(threadid_, address * block_bytes_);
  global_rw_->synchronize = 2;
  events_.Log(EventBuffer::kNewAddress);
  DistanceSet *newDS = new DistanceSet(address);
  newDS->creation_time = sampled_access_count_;
  newDS->final_pc = PC;
  newDS->final_ref_is_write = false;
  if(CheckOldestSample()) global_rw_->finalize_needed = 1; // check to prune
  // newDS is linked into distance_sets_ list at end of sync ops
  SynchronizedOperations(kNewSampledAddress, newDS);
}

static int ListLength(DistanceSet *ds) {
  int c = 0;
  while (ds) {
    c++;
    ds = ds->next;
  }
  return c;
}

bool ParallelSampledStack::Access(address_t address, address_t PC, bool is_write) {
  // check sync flag
  if (global_rw_->synchronize) {
    events_.Log(EventBuffer::kNoAction);
    SynchronizedOperations(kNoAction, NULL);
  }
  if (!enabled_ || !global_enabled_) return false;
  address = GetBlock(address);
  //trace_.TraceAccess(threadid_, address * block_bytes_, is_write);
  sampled_access_count_++;
  //addresses_per_sample_total_ += global_rw_->active_sample_count;

  int do_finalize = 0;
  int ll1 = ListLength(distance_sets_);
  DistanceSet *ds = distance_sets_;
  while (ds) {
    if (ds->sample_addr == address) {
      ds->finalize = DistanceSet::kReuseFinalize;
      ds->final_pc = PC;
      ds->final_ref_is_write = is_write;
      do_finalize = DistanceSet::kReuseFinalize;  // can be only one reuse finalization per access
      events_.Log(EventBuffer::kFinalizeSelf);
    } else {
      if (ds->holes > 0 && ds->set.count(address) == 0) ds->holes--;
      ds->set.insert(address);
    }
    ds = ds->next;
  }

  // do my other addresses - if write, add address to invalidation sets
  if (stack_type_ == kPrivateStacks && is_write) {
    WriteSet *ws = write_sets_;
    while (ws) {
      if (address == ws->sample_addr) {
        // invalidate the address, owning thread will finalize
        ws->owner->finalize = DistanceSet::kInvalidateFinalize;
        ws->owner->final_pc = PC;
        ws->owner->final_ref_is_write = is_write;
        do_finalize = DistanceSet::kInvalidateFinalize;
        events_.Log(EventBuffer::kFinalizeInval);
      } else {
        ws->set.insert(address);
      }
      ws = ws->next;
    }
  } else if (stack_type_ == kSharedStacks) {
    WriteSet *ws = write_sets_;
    while (ws) {
      if (address == ws->sample_addr) {
        // remote-reuse the address, owning thread will finalize
        ws->owner->finalize = DistanceSet::kRemoteReuseFinalize;
        ws->owner->final_pc = PC;
        ws->owner->final_ref_is_write = is_write;
        do_finalize = DistanceSet::kRemoteReuseFinalize;
        events_.Log(EventBuffer::kFinalizeInval);
      } else {
        ws->set.insert(address);
      }
      ws = ws->next;
    }
  }

  // must not do sync ops while traversing the ds list and ws list because they can change
  if (do_finalize) {
    global_rw_->finalize_needed = do_finalize;
    global_rw_->synchronize = 3;
    SynchronizedOperations(kFinalize, NULL);
  }

  ds = distance_sets_;
  if ( (do_finalize == DistanceSet::kReuseFinalize) && (ListLength(ds) >= ll1) )
    throw std::runtime_error("length");
  while(ds) {
    if (ds->finalize == DistanceSet::kReuseFinalize)
      throw std::runtime_error("still final");
    ds = ds->next;
  }
  return global_rw_->active_sample_count == 0;
}

void ParallelSampledStack::MergeInvalidationsThreads(int thread, DistanceSet *myDS) {
  // work on behalf of thread with id 'thread'
  // iterate over all other threads, invalidate their write sets in my distance set
  // where "my" distance set is the DS from 'thread'
  address_t address = myDS->sample_addr;
  for (int i = threads_.GetNextIndex(thread); i != thread; i = threads_.GetNextIndex(i)) {
    //not necessary to acquire, no modification to linklist and only one thread owns each address
    //LockHolder lh(&threads_[i]->write_set_lock_);
    WriteSet *ws = threads_[i]->write_sets_;
    while (ws && !(ws->sample_addr == address && ws->owner == myDS)) ws = ws->next;
    if (!ws) {
      throw std::runtime_error("merged address not found in remote thread's write sets");
    }
    for(AddressSet::iterator it = ws->set.begin(); it != ws->set.end(); ++it) {
      assert(*it != address);
      if (myDS->set.count(*it) > 0) {
        //threads_[i]->invalidation_count_++;
        myDS->set.erase(*it);
        myDS->holes++;
      }
    }
    ws->set.clear();
  }
}

void ParallelSampledStack::MergeDistanceSetsThreads(int thread, DistanceSet *myDS) {
  // work on behalf of thread with id 'thread'
  // iterate over all other threads, invalidate their write sets in my distance set
  // where "my" distance set is the DS from 'thread'
  address_t address = myDS->sample_addr;
  for (int i = threads_.GetNextIndex(thread); i != thread; i = threads_.GetNextIndex(i)) {
    //not necessary to acquire, no modification to linklist and only one thread owns each address
    //LockHolder lh(&threads_[i]->write_set_lock_);
    WriteSet *theirDS = threads_[i]->write_sets_;
    while (theirDS && !(theirDS->sample_addr == address && theirDS->owner == myDS)) {
      theirDS = theirDS->next;
    }
    if (!theirDS) {
      throw std::runtime_error("merged address not found in remote thread's write sets");
    }
    for(AddressSet::iterator it = theirDS->set.begin(); it != theirDS->set.end(); ++it) {
      assert(*it != address);
      myDS->set.insert(*it);
    }
    theirDS->set.clear();
  }
}

// merge the write sets in mergeList into the distance set myDS.
void ParallelSampledStack::MergeInvalidationsList(DistanceSet *myDS, int thread) {
  address_t address = myDS->sample_addr;
  WriteSet *ws = myDS->write_sets;
  acc_count_t total_lifetime = threads_[thread]->sampled_access_count_ - myDS->creation_time;
#ifdef UNITTEST
  if (global_rw_->last_finalized_distance == -2) return;
#endif
  while (ws) {
    // creation_time now holds the total time (access count) in the respective thread
    total_lifetime += ws->creation_time;
    assert(ws->sample_addr == address && ws->owner == myDS);
    for(AddressSet::iterator it = ws->set.begin(); it != ws->set.end(); ++it) {
      assert(*it != address);
      if (myDS->set.count(*it) > 0) {
        //threads_[thread]->invalidation_count_++;//race condition here. ignore it rather than sync
        myDS->set.erase(*it);
        myDS->holes++;
      }
    }
    WriteSet *f = ws;
    ws = ws->next;
    delete f;
  }
  //myDS->creation_time = total_lifetime;
}

void ParallelSampledStack::MergeDistanceSetsList(DistanceSet *myDS, int thread) {
  address_t address = myDS->sample_addr;
  WriteSet *mds = myDS->write_sets;
  acc_count_t total_lifetime = threads_[thread]->sampled_access_count_ - myDS->creation_time;
#ifdef UNITTEST
  if (global_rw_->last_finalized_distance == -2) return;
#endif
  while (mds) {
    // creation_time now holds the total time (access count) in the respective thread
    total_lifetime += mds->creation_time;
    assert(mds->sample_addr == address && mds->owner == myDS);
    for(AddressSet::iterator it = mds->set.begin(); it != mds->set.end(); ++it) {
      assert(*it != address);
      myDS->set.insert(*it);
    }
    WriteSet *f = mds;
    mds = mds->next;
    delete f;
  }
  //myDS->creation_time = total_lifetime;
}

//iterates over all threads (other than threadid) and removes the address from each write set
void ParallelSampledStack::RemoveWriteSetFromAll(DistanceSet *ds, int thread) {
  //fprintf(stderr, "%d removing %ld for %d gen %d\n", threadid_, address, threadid, global_rw_->barrier.generation_);
  // remove the finalized sample from everyone's write set lists
  for (int i = threads_.GetNextIndex(thread); i != thread; i = threads_.GetNextIndex(i)) {
    WriteSet *prevWS = NULL;
    LockHolder lh(&threads_[i]->write_set_lock_);
    WriteSet *ws = threads_[i]->write_sets_;
    while (ws && !(ws->sample_addr == ds->sample_addr && ws->owner == ds)) {
      prevWS = ws;
      ws = ws->next;
    }
    if (!ws) {
      throw std::runtime_error("finalized address not found in remote write set");
    }
    //unlink from write set
    if (prevWS == NULL) {
      threads_[i]->write_sets_ = ws->next;
    }
    else {
      prevWS->next = ws->next;
    }
    //calculate reference lifetime in thread i
    ws->creation_time = threads_[i]->sampled_access_count_ - ws->creation_time;
    //link into to-merge list
    ws->next = ds->write_sets;
    ds->write_sets = ws;
  }
}

//FinalizeSample(int thread)
//find distance set, pull from my distance sets
// if my reuse, already pulled from other write sets
// if discovered overwrite, have not
void ParallelSampledStack::FinalizeSample(int thread, DistanceSet *myDS, address_t PC) {
  if (myDS->write_sets == NULL)
      assert(threads_.GetThreadCount() == 1);
  if (stack_type_ == kPrivateStacks) MergeInvalidationsList(myDS, thread);
  else MergeDistanceSetsList(myDS, thread);
  acc_count_t distance;
  if (myDS->finalize == DistanceSet::kInvalidateFinalize) {
    distance = kInvalidationMiss;
  } else if( myDS->finalize == DistanceSet::kPruneFinalize) {
    distance = kColdMiss;
  } else {
    distance = myDS->set.size() + myDS->holes;
  }

  if (myDS->finalize == DistanceSet::kInvalidateFinalize) threads_[thread]->invalidation_count_++;
  threads_[thread]->private_stats_.AddSample(myDS->sample_addr, distance);
  if (!myDS->final_ref_is_write ) {
    threads_[thread]->private_read_stats_.AddSample(myDS->sample_addr, distance);
  }
  threads_[thread]->reference_lifetime_total_ +=
      threads_[thread]->sampled_access_count_ - myDS->creation_time;

  RdaGetLock(&global_rw_->stats_lock);
  global_rw_->pc_stats.AddSample(PC, distance);
  if (!myDS->final_ref_is_write) {
    global_rw_->read_pc_stats.AddSample(PC, distance);
  }
  RdaReleaseLock(&global_rw_->stats_lock);

  //fprintf(stderr, "%d finalized %ld for %d gen %d\n", threadid_,
  //        address, thread, global_rw_->barrier.generation_);
  AtomicDecrement(&global_rw_->active_sample_count);
  //RdaReleaseLock(&global_rw_->stats_lock);
#ifdef UNITTEST
  global_rw_->last_finalized_distance = distance;
#endif
}

bool ParallelSampledStack::CheckOldestSample() {
  //if we have gone a certain number of references, and the oldest sample has distance >=
  // top 1% of references (?) then finalize it
  // maybe want to include pruned samples in this 1% (they arent now) so that the threshold
  // for pruning goes up if a lot of stuff gets pruned?
  if(private_stats_.GetTotalSamples() > kPruneSampleThreshold) {
    if(oldest_distance_set_ != NULL &&
       oldest_distance_set_->finalize == DistanceSet::kActive &&
       (static_cast<acc_count_t>(oldest_distance_set_->set.size()) + oldest_distance_set_->holes >
         private_stats_.GetTargetSize(kPruneTarget))) {
      oldest_distance_set_->finalize = DistanceSet::kPruneFinalize;
      prune_count_++;
      return true;
    }
  }
  return false;
}

int ParallelSampledStack::RecordLeftovers() {
  int leftovers = 0;
  printf("Leftovers: ");
  for (int i = 0; i < threads_.GetThreadCount(); i++) {
    DistanceSet *ds = threads_[i]->distance_sets_;
    while(ds) {
      printf(" %"PRIaddr" dist %"PRIacc" lifetime %.2f%% of run, ", ds->sample_addr * block_bytes_,
             ds->set.size() + ds->holes,
             (threads_[i]->sampled_access_count_ - ds->creation_time)
               / static_cast<double>(threads_[i]->sampled_access_count_) * 100.0 );
      threads_[i]->private_stats_.AddSample(ds->sample_addr, kColdMiss);
      global_rw_->pc_stats.AddSample(ds->final_pc, kColdMiss);
      leftovers++;
      ds = ds->next;
    }
    printf("; ");
  }
  printf("\n");
  return leftovers;
}

int64_t ParallelSampledStack::DumpStatsPython(const std::string &extra) {
  if (!initialized_) {
    printf("ParallelSampledStack not initialized!\n");
    return -1;
  }
  int leftover_addresses = RecordLeftovers();
  //combine stats
  int64_t addresses_per_sample_total = 0;
  int64_t sampled_access_count = 0;
  int64_t synchronization_count = 0;
  int64_t total_distance = 0;
  int64_t total_samples = 0;
  int64_t invalidated_samples = 0;
  int64_t prune_samples = 0;
  int64_t lifetime_total = 0;
  for (int i = 0; i < threads_.GetThreadCount(); i++) {
    addresses_per_sample_total += threads_[i]->addresses_per_sample_total_;
    sampled_access_count += threads_[i]->sampled_access_count_;
    synchronization_count += threads_[i]->synchronization_count_;
    total_distance += threads_[i]->private_stats_.GetTotalDistance();
    total_samples += threads_[i]->private_stats_.GetTotalSamples();
    invalidated_samples += threads_[i]->private_stats_.GetInvalSamples();
    prune_samples += threads_[i]->prune_count_;
    if (threads_[i]->prune_count_ != threads_[i]->private_stats_.GetColdSamples()) {
      printf("Error: thread %d prune count %"PRIacc", stats cold count %"PRIacc"\n",
             i, threads_[i]->prune_count_, threads_[i]->private_stats_.GetColdSamples());
    }
    if (threads_[i]->invalidation_count_ != threads_[i]->private_stats_.GetInvalSamples()) {
      printf("Error: thread %d inval count %"PRIacc", stats inval count %"PRIacc"\n",
             i, threads_[i]->invalidation_count_, threads_[i]->private_stats_.GetInvalSamples());
    }
    lifetime_total += threads_[i]->reference_lifetime_total_;
  }
  fprintf(output_file_, "#librda version %s\n", LIBRDA_GIT_VERSION);
  fprintf(output_file_, "#Enabled samples %"PRIacc", average addresses per enabled sample %.2f\n",
          sampled_access_count, addresses_per_sample_total / static_cast<double>(sampled_access_count));
  fprintf(output_file_, "#Synchronization count %ld\n", synchronization_count);
  if (stack_type_ == kSharedStacks) {
    fprintf(output_file_, "#Using shared stacks\n");
  }
  fprintf(output_file_, "singleStacks = {}\nsimStacks = {}\ndelayStacks = {}\n"
          "preStacks = {}\nsimSharedStack = {}\n");
  // convert leftover addresses?
  fprintf(output_file_, "pairStacks = {}\n");
  fprintf(output_file_, "cacheHits = {}\npairHits = {}\nshareHits = {}\n");
  for (int thread = 0; thread < threads_.GetThreadCount(); thread++) {
    if (stack_type_ == kPrivateStacks) {
      fprintf(output_file_, "#rddata simStacks[%d] = ", thread);
    } else {
      fprintf(output_file_, "#rddata simSharedStack[%d] = ", thread);
    }
    // reusestack-like output
    fprintf(output_file_, "{");
    fprintf(output_file_, "'histogram':%s,", threads_[thread]->private_stats_.GetHistogramString().c_str());
    //  attributes
    //fprintf(output_file_, "{'sampledAddresses':%"PRIacc", ", sampled_address_count_[thread]);
    fprintf(output_file_, "'attributes':{'accessCount':%"PRIacc", ", threads_[thread]->private_stats_.GetTotalSamples());
    fprintf(output_file_, "'blockAccessCount':%"PRIacc", ", threads_[thread]->private_stats_.GetTotalSamples());
    fprintf(output_file_, "'addrPerSamp':%.2f, ",
            threads_[thread]->addresses_per_sample_total_ /
            static_cast<double>(threads_[thread]->sampled_access_count_));
    fprintf(output_file_, "'limitCount': %"PRIacc", ", threads_[thread]->prune_count_);
    fprintf(output_file_, "'invalidationCount': %"PRIacc", ",threads_[thread]->invalidation_count_);
    fprintf(output_file_, "%s", threads_[thread]->private_stats_.GetAttributes().c_str());
    fprintf(output_file_, "},");  // end attribute dict
    //new read histogram
    fprintf(output_file_, "'read_histo':{'histogram':%s, 'attributes':{%s}}, ",
        threads_[thread]->private_read_stats_.GetHistogramString().c_str(),
        threads_[thread]->private_read_stats_.GetAttributes().c_str());
    fprintf(output_file_, "}\n");
    //threads_[i]->private_stats_.DumpStatistics();
  }

  fprintf(output_file_, "PCDist = %s\n", global_rw_->pc_stats.GetStatsString().c_str());
  fprintf(output_file_, "PCDistRead = %s\n", global_rw_->read_pc_stats.GetStatsString().c_str());
  fprintf(output_file_, "%s", extra.c_str());

  printf("Enabled sampling accesses %"PRIacc", average addresses per enabled sample %.2f\n",
            sampled_access_count, addresses_per_sample_total / static_cast<double>(sampled_access_count));
  printf("Recorded %d leftover addresses as cold misses\n", leftover_addresses);
  printf("Overall avg reference lifetime %.2f\n",
         static_cast<double>(lifetime_total) / total_samples);
  printf("Overall average distance %.2f plus %.3f%% invalidated and %.3f%% pruned\n",
         total_distance / static_cast<double>(total_samples - invalidated_samples - prune_samples),
         invalidated_samples / static_cast<double>(total_samples) * 100.0,
         prune_samples / static_cast<double>(total_samples) * 100.0);

  return addresses_per_sample_total;
}

const DistanceSet *ParallelSampledStack::GetDS(address_t address) {
  DistanceSet *ds = distance_sets_;
  while (ds && ds->sample_addr != GetBlock(address)) ds = ds->next;
  return ds;
}

void ParallelSampledStack::ValidateActiveSamples(bool has_new) {
  int sample_count = 0;
  for (int i = 0; i < threads_.GetThreadCount(); i++) {
    DistanceSet *ds = threads_[i]->distance_sets_;
    while (ds) {
      sample_count++;
      ds = ds->next;
    }
  }
  if (global_rw_->active_sample_count > sample_count + threads_.GetThreadCount() + has_new + 1) {
    throw std::runtime_error("sample count mismatch");
    //printf("sample count mismatch\n");
  }
}
