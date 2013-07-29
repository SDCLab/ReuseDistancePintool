/*
 * stackholder.cc
 *
 *  Created on: Aug 27, 2009
 *      Author: dschuff
 */

#include "stackholder.h"
#include "version.h"
#include <algorithm>

using std::map;
using std::string;
using std::vector;
using std::tr1::unordered_map;

const int StackHolder::kDefaultGranularity;
// index 0 not used
const int StackHolder::share_map_[9] = {0,0,0,1,1,2,2,3,3};

StackHolder::StackHolder(const string& statsfile_name, int granularity,
                         const std::string& stack_type)
    throw(std::invalid_argument)
    : do_inval_(true), do_shared_(false), do_single_stacks_(true), do_sim_stacks_(true),
      do_lazy_stacks_(false), do_oracular_stacks_(false), merge_interleave_(1),
      global_enable_(true), do_prefetch_(false), do_fetch_(false), simulated_shared_stack_(NULL),
      statsfile_name_(statsfile_name), statsfile_(NULL), granularity_(granularity), PC_stats_(),
      PC_read_stats_() {
  statsfile_ = fopen(statsfile_name_.c_str(), "w");
  if (statsfile_ == NULL) throw std::invalid_argument("Could not open file for writing");
  if (stack_type == "exact") {
    stack_type_ = ReuseStack::kTreeStack;
  }
  else if (stack_type == "approximate") {
    stack_type_ = ReuseStack::kApproximateStack;
  }
  else {
    throw std::invalid_argument("stack type must be \"exact\" or \"approximate\"");
  }
}

StackHolder::~StackHolder() {
  fclose(statsfile_);
  // delay the deletion until after the dump in case we crashed, we might still get the info
  for (std::vector<int>::iterator iter(threads_seen_.begin());
       iter != threads_seen_.end(); ++iter) {
    int i = *iter;
    if (do_single_stacks()) delete single_stacks_[i];
    if (do_sim_stacks()) delete sim_stacks_[i];
    if (do_lazy_stacks()) delete lazy_stacks_[i];
    if (do_oracular_stacks()) delete oracular_stacks_[i];
  }
  delete simulated_shared_stack_;
  //delete pair shared stacks?
}

void StackHolder::Allocate(int thread) {
  threads_seen_.push_back(thread);
  threads_enabled_[thread] = true;

  if (do_inval()) {
    if (do_single_stacks()) {
      single_stacks_[thread] = new ReuseStack(statsfile_, granularity_, stack_type_);
      single_stacks_[thread]->SetRatioPredictionSizes(ratio_prediction_sizes_);
    }

    if (do_sim_stacks()) {
        sim_stacks_[thread] = new ReuseStack(statsfile_, granularity_, stack_type_);
        sim_stacks_[thread]->SetRatioPredictionSizes(ratio_prediction_sizes_);
        prefetchers_[thread] = new PrefetchArbiter();
        prefetchers_[thread]->AddPrefetcher(new StridePrefetcher());
        //prefetchers_[thread]->AddPrefetcher(new DCUPrefetcher());
    }

    if (do_lazy_stacks()) {
        lazy_stacks_[thread] = new ReuseStack(statsfile_, granularity_, stack_type_);
        lazy_stacks_[thread]->SetRatioPredictionSizes(ratio_prediction_sizes_);
    }
    if (do_oracular_stacks()) {
        oracular_stacks_[thread] = new ReuseStack(statsfile_, granularity_, stack_type_);
        oracular_stacks_[thread]->SetRatioPredictionSizes(ratio_prediction_sizes_);
    }
  }

  if (do_shared()) {
    if (pair_share_stacks_.count(share_map_[thread]) == 0) {
      pair_share_stacks_[share_map_[thread]] = new ReuseStack(statsfile_, granularity_, stack_type_);
      pair_share_stacks_[share_map_[thread]]->SetRatioPredictionSizes(pair_prediction_sizes_);
    }
    if (simulated_shared_stack_ == NULL) {
      simulated_shared_stack_ = new ReuseStack(statsfile_, granularity_, stack_type_);
      simulated_shared_stack_->SetRatioPredictionSizes(shared_prediction_sizes_);
    }
  }
}

void StackHolder::Fetch(int thread, address_t PC, int size) {
  if ( !global_enable_ || !threads_enabled_[thread]) return;
  if (do_fetch()) sim_stacks_[thread]->Access(PC, size, ReuseStack::kFetch);
}

acc_count_t StackHolder::Access(int thread, address_t address, int size, address_t PC, bool is_write) {
  if ( !global_enable_ || !threads_enabled_[thread]) return 0;
  acc_count_t distance = 0;
  try {

    if (do_inval()) {
      if (do_single_stacks()) {
        single_stacks_[thread]->Access(address, size,
            is_write ? ReuseStack::kWrite : ReuseStack::kRead);
      }
      if (do_sim_stacks()) {
        distance = sim_stacks_[thread]->Access(address, size, 
					       is_write ? ReuseStack::kWrite : ReuseStack::kRead);
        if (is_write) {
          for (vector<int>::iterator iter(threads_seen_.begin());
               iter != threads_seen_.end(); ++iter) {
            int i = *iter;
            if (i != thread) sim_stacks_[i]->Snoop(address, size);
          }
        }
        address_t addr;
        if (do_prefetch() &&
            (addr = prefetchers_[thread]->Access(address / granularity_, PC, distance,
						 is_write ? ReuseStack::kWrite : ReuseStack::kRead)) != kAddressMax){
          sim_stacks_[thread]->Prefetch(addr * granularity_);
        }
      }
      if (do_lazy_stacks()) lazy_stacks_[thread]->Access(address, size, is_write ? ReuseStack::kWrite : ReuseStack::kRead);
      if (do_lazy_stacks() || do_oracular_stacks()) {
        //pre-inval buffering
        BufferedRef bRef;
        bRef.address = address;
        bRef.is_write = is_write;
        //bRef.cpu = thread;
        bRef.size = size;
        buffered_accesses_[thread].push_back(bRef);
      }
    }

    if (do_shared()) {
      acc_count_t dist = simulated_shared_stack_->Access(address, size, is_write ? ReuseStack::kWrite : ReuseStack::kRead);
      if (!do_inval() || !do_sim_stacks()) distance = dist;  // private overrides shared in stats keeping
      pair_share_stacks_[share_map_[thread]]->Access(address, size, is_write ? ReuseStack::kWrite : ReuseStack::kRead);
      if (is_write) {
        for (vector<int>::iterator iter(threads_seen_.begin());
             iter != threads_seen_.end(); ++iter) {
          int i = *iter;
          if (share_map_[i] != share_map_[thread]) {
            pair_share_stacks_[share_map_[i]]->Snoop(address, size);
          }
        }
      }
    }
    PC_stats_.AddSample(PC, distance);
    if (!is_write) {
      PC_read_stats_.AddSample(PC, distance);
    }
  } catch (std::bad_alloc ex) {
    DumpStatsPython(""); //make sure we dump our stats because they are still useful
    throw;//TODO: figure out what to do here, if anything
  }
  return distance;
}

void StackHolder::AddRatioPredictionSize(int size) {
  if (do_inval()) {
    for (vector<int>::iterator iter(threads_seen_.begin()); iter != threads_seen_.end(); ++iter) {
      int j = *iter;
      if (do_single_stacks()) single_stacks_[j]->AddRatioPredictionSize(size);
      if (do_sim_stacks()) sim_stacks_[j]->AddRatioPredictionSize(size);
      if (do_lazy_stacks()) lazy_stacks_[j]->AddRatioPredictionSize(size);
      if (do_oracular_stacks()) oracular_stacks_[j]->AddRatioPredictionSize(size);
    }
  }
  ratio_prediction_sizes_.push_back(size);
}

void StackHolder::AddPairPredictionSize(int size) {
  for (map<int, ReuseStackBase *>::iterator iter(pair_share_stacks_.begin());
       iter != pair_share_stacks_.end(); ++iter) {
    iter->second->AddRatioPredictionSize(size);
  }
  pair_prediction_sizes_.push_back(size);
}

void StackHolder::AddSharedPredictionSize(int size) {
  if (simulated_shared_stack_ != NULL) {
    simulated_shared_stack_->AddRatioPredictionSize(size);
  }
  shared_prediction_sizes_.push_back(size);
}

void StackHolder::EndParallelRegion() {
  try {
//        for(std::set<int>::iterator iter = memhier->cpp->threadsSeen.begin(); iter !=
//               memhier->cpp->threadsSeen.end(); ++iter){
//            int i = *iter;
//            memhier->cpp->simStacks[i]->checkRace();
//        }

//        if(memhier->cpp->stackMode == MERGE) {
//            std::map<int, std::vector<bufferedRef>::iterator> threadsLeft;
//            if(memhier->cpp->threadsSeen.size() == 1) return;
//            //blocked merging: foreach thread, do all that thread's accesses on all sharing threads
//            for(std::set<int>::iterator threadIter = memhier->cpp->threadsSeen.begin();
//                    threadIter !=  memhier->cpp->threadsSeen.end(); ++threadIter){
//                int thread = *threadIter;
//                for(std::vector<bufferedRef>::iterator refIter = memhier->cpp->bufferedAccesses[thread].begin();
//                        refIter != memhier->cpp->bufferedAccesses[thread].end(); ++refIter) {
//                    memhier->cpp->blockedMergedSharedStack->Access(refIter->addr, refIter->size);
//                }
//                threadsLeft[thread] = memhier->cpp->bufferedAccesses[thread].begin();
//            }
//
//            //interleaved merging: while !done: foreach thread, do k of that thread's Access on all sharing threads
//            std::vector<bufferedRef>::iterator refIter;
//
//            while(threadsLeft.size() > 0) {
//                for(std::map<int, std::vector<bufferedRef>::iterator>::iterator threadIter = threadsLeft.begin();
//                        threadIter !=  threadsLeft.end(); ++threadIter){
//                    int thread = threadIter->first;
//                    refIter = threadIter->second;
//                    int i;
//                    for(refIter = threadsLeft[thread], i = 0;
//                            i < memhier->cpp->stackInterleaveK && refIter != memhier->cpp->bufferedAccesses[thread].end(); ++refIter, ++i) {
//                        memhier->cpp->interleavedMergedSharedStack->Access(refIter->addr, refIter->size);
//                    }
//                    if(refIter == memhier->cpp->bufferedAccesses[thread].end()){ //reached the end of this thread's accesses before getting to k
//                        threadsLeft.erase(thread);
//                    }
//                    threadIter->second = refIter;
//                }
//            }
//            //if(dynamic_cast<TreeReuseStack *>(memhier->cpp->interleavedMergeStacks[1])->CheckDuplicateNodes(dynamic_cast<TreeReuseStack *>(memhier->cpp->interleavedMergeStacks[2])) == false) printf("interleaveStacks did not match!\n");
//
//            for(std::set<int>::iterator iter = memhier->cpp->threadsSeen.begin();
//                    iter !=  memhier->cpp->threadsSeen.end(); ++iter){
//                int thread = *iter;
//                TreeReuseStack *s = dynamic_cast<TreeReuseStack *>(memhier->cpp->blockedMergeStacks[thread]);
//                if(s)
//                    s->ReplaceTree(*(dynamic_cast<TreeReuseStack *>(memhier->cpp->blockedMergedSharedStack)));
//                s = dynamic_cast<TreeReuseStack *>(memhier->cpp->interleavedMergeStacks[thread]);
//                if(s)
//                    s->ReplaceTree(*(dynamic_cast<TreeReuseStack *>(memhier->cpp->interleavedMergedSharedStack)));
//            }
//        }
    if (do_inval()) {
      if (do_lazy_stacks() || do_oracular_stacks()){
        //for inval stacks, pass over references in all threads for writes,
        //doing invalidations for previous interval
        for (vector<int>::iterator thread_iter(threads_seen_.begin());
             thread_iter !=  threads_seen_.end(); ++thread_iter){
          int thread = *thread_iter;
          for (vector<BufferedRef>::iterator ref_iter(buffered_accesses_[thread].begin());
               ref_iter != buffered_accesses_[thread].end(); ++ref_iter) {
            if ((*ref_iter).is_write) {
              //each write invalidates all other threads
              for (vector<int>::iterator iter(threads_seen_.begin());
                   iter != threads_seen_.end(); ++iter){
                int i = *iter;
                if (i != thread) {
                  if (do_oracular_stacks()) {
                    oracular_stacks_[i]->Snoop((*ref_iter).address, (*ref_iter).size);
                  }
                  //also do invals for post_inval (already did accesses)
                  if (do_lazy_stacks()) {
                    lazy_stacks_[i]->Snoop((*ref_iter).address, (*ref_iter).size);
                  }
                }
              }
            }
          }
        }
      }
      if (do_oracular_stacks()){
        //pass over all references, doing accesses for previous interval
        for (vector<int>::iterator thread_iter(threads_seen_.begin());
             thread_iter !=  threads_seen_.end(); ++thread_iter){
          int thread = *thread_iter;
          for (vector<BufferedRef>::iterator ref_iter(buffered_accesses_[thread].begin());
               ref_iter != buffered_accesses_[thread].end(); ++ref_iter) {
            oracular_stacks_[thread]->Access(ref_iter->address, ref_iter->size, 
                ref_iter->is_write ? ReuseStack::kWrite : ReuseStack::kRead);
          }
          buffered_accesses_[thread].clear();
        }
      }
      else {
        //printf("warning: reuse_timedec called with doSimOnly\n");
      }
    }
  } catch (std::bad_alloc ex) {
//    attr_value_t attr = SIM_make_attr_string("");
//    set_dist_file_attribute(NULL, (conf_object_t *)memhier, &attr, NULL);
//    SIM_frontend_exception(SimExc_Memory, "bad alloc");
    DumpStatsPython("");
    throw;//TODO: do something smart?
 }
}

void StackHolder::UpdateRatioPredictions() {
  if (do_inval()) {
    for (vector<int>::iterator thread_iter(threads_seen_.begin());
         thread_iter != threads_seen_.end(); ++thread_iter) {
      int thread = *thread_iter;
      if (do_single_stacks()) single_stacks_[thread]->UpdateRatioPredictions();
      if (do_sim_stacks()) sim_stacks_[thread]->UpdateRatioPredictions();
      if (do_lazy_stacks()) lazy_stacks_[thread]->UpdateRatioPredictions();
      if (do_oracular_stacks()) oracular_stacks_[thread]->UpdateRatioPredictions();
    }
  }
  // track total/region accesses here? or leave to caches as currently?
  if (do_shared()) {
    for (map<int, ReuseStackBase *>::iterator it(pair_share_stacks_.begin());
         it != pair_share_stacks_.end(); ++it) {
      int i = it->first;
      pair_share_stacks_[i]->UpdateRatioPredictions();
    }
    simulated_shared_stack_->UpdateRatioPredictions();
  }
}

void StackHolder::DumpStatsPython(const std::string &extra) {
  //fprintf(memhier->cpp->stackOutfile, "from appendArray import appendArray\n");
  fprintf(statsfile_, "#librda version %s\n", LIBRDA_GIT_VERSION);
  fprintf(statsfile_, "singleStacks = {}\nsimStacks = {}\ndelayStacks = {}\n"
          "preStacks = {}\n");
  fprintf(statsfile_, "pairStacks = {}\n");
  fprintf(statsfile_, "cacheHits = {}\npairHits = {}\nshareHits = {}\n");
  fprintf(statsfile_, "prefetchStats = {}\n");
  //d4fprintf(statsfile_,"cacheHits[%d] = {}\n", 1);
  if (do_inval()) {
    for (vector<int>::iterator iter(threads_seen_.begin()); iter != threads_seen_.end(); ++iter){
      int i = *iter;
      if (do_single_stacks()) {
        fprintf(statsfile_, "#rddata singleStacks[%d] = ", i);
        single_stacks_[i]->DumpStatistics();
      }
      if (do_sim_stacks()){
        fprintf(statsfile_, "#rddata simStacks[%d] = ", i);
        sim_stacks_[i]->DumpStatistics();
        fprintf(statsfile_, "prefetchStats[%d] = %s\n", i, prefetchers_[i]->GetStatsString().c_str());
      }
      if (do_lazy_stacks()){
        fprintf(statsfile_, "#rddata delayStacks[%d] = ", i);
        lazy_stacks_[i]->DumpStatistics();
      }
      if (do_oracular_stacks()){
        fprintf(statsfile_, "#rddata preStacks[%d] =  ", i);
        oracular_stacks_[i]->DumpStatistics();
      }

  //    printf("%d local period ends (barriers)\n", memhier->cpp->localEndPeriodCount[i]);
  //            //d4 stuff
  //            fprintf(statsfile_, "cacheHits[%d][%d] = [",1, i);
  //                for(int j = 0; j< (int)memhier->cpp->hitRatios[100+i].size(); j++){
  //                    //each Access
  //                    fprintf(statsfile_, "%f, ",
  //                        memhier->cpp->hitRatios[100+i][j]);
  //                }
  //                fprintf(statsfile_, "]\n");
    }
  }
  if (do_shared()){
    fprintf(statsfile_, "#rddata simSharedStack = ");
    simulated_shared_stack_->DumpStatistics();
    for (map<int, ReuseStackBase *>::iterator it(pair_share_stacks_.begin());
         it != pair_share_stacks_.end(); ++it) {
      int i = it->first;
      fprintf(statsfile_, "#rddata pairStacks[%d] = ", i);
      //memhier->cpp->pairShareStacks[memhier->cpp->shareMap[i]]->DumpStatistics();
      it->second->DumpStatistics();
    }
  }
  fprintf(statsfile_, "PCDist = %s\n", PC_stats_.GetStatsString().c_str());
  fprintf(statsfile_, "PCDistRead = %s\n", PC_read_stats_.GetStatsString().c_str());
  fprintf(statsfile_, "%s", extra.c_str());
}

bool StackHolder::IsNewThread(int thread) {
  return std::find(threads_seen_.begin(), threads_seen_.end(), thread) == threads_seen_.end();
}
