/*
 * sampledreusestack.cc
 *
 *  Created on: Sep 16, 2009
 *      Author: dschuff
 */

#include "sampledreusestack.h"
#include <stdexcept>
#include "version.h"

using std::tr1::unordered_map;

const acc_count_t SampledReuseStackInterface::kMaxDistance;
const int SampledReuseStackInterface::kDefaultThreadAllocation;

SampledReuseStack::SampledReuseStack(std::string output_filename, int granularity)
    : stats_(kDefaultThreadAllocation), sampled_address_count_(kDefaultThreadAllocation, 0),
      sampled_addresses_(kDefaultThreadAllocation), sample_count_(0), reference_lifetime_total_(0),
      addresses_per_sample_total_(0), global_tracked_address_count_(0),
      limit_count_(0), invalidation_count_(0), last_finalized_distance_(0), output_file_(NULL),
      block_bytes_(granularity), trace_(output_filename + "-trace") {
  output_file_ = fopen(output_filename.c_str(), "w");
  if (output_file_ == NULL) {
    throw std::invalid_argument("Could not open output file " + output_filename);
  }
}

SampledReuseStack::~SampledReuseStack() {
  //free stats_ stuff
  fclose(output_file_);
}

void SampledReuseStack::Allocate(int thread) {
  // this is good if threads get allocated in order, as is the case with Pin. But still allow
  // for the case when the don't. keep the three per-thread vectors the same size
  if (static_cast<unsigned int>(thread) >= stats_.size()) {
    stats_.resize(thread);
    sampled_address_count_.resize(thread);
    sampled_addresses_.resize(thread);
    thread_enable_.resize(thread);
  }
  stats_[thread].reset(new ReuseStackStats(block_bytes_));
  sampled_address_count_[thread] = 0;
  sampled_addresses_[thread].clear();
  thread_enable_[thread] = true;
}

void SampledReuseStack::NewSampledAddress(address_t address, int thread, address_t PC) {
  if (!global_enable_ || !thread_enable_[thread]) return;
  address = GetBlock(address);
  if (sampled_addresses_[thread].count(address) != 0) {
    return;  //will eventually have to handle this but ignore for now
  }
  //printf("sampling address %lx\n", address);
  sampled_address_count_[thread]++;
  sampled_addresses_[thread][address].set.reset(new AddressHashSet());
  sampled_addresses_[thread][address].hole_count = 0;
  sampled_addresses_[thread][address].reference_lifetime = 0;
  sampled_addresses_[thread][address].PC = PC;
  global_tracked_address_count_++;
  trace_.TraceNewSampledAddress(thread, address * block_bytes_);
}

void SampledReuseStack::DeleteAddress(address_t address, int thread, address_t PC, bool max_limit) {
  acc_count_t distance;
  if (max_limit) {
    distance = kColdMiss;
  } else {
    distance = sampled_addresses_[thread][address].set->size() +  // size of set
      sampled_addresses_[thread][address].hole_count; // number of holes
  }
  //remove from set and record stat sample
  stats_[thread]->AddSample(address, distance);
  PC_stats_.AddSample(PC, distance);
  reference_lifetime_total_ += sampled_addresses_[thread][address].reference_lifetime;
  sampled_addresses_[thread].erase(address);
  global_tracked_address_count_--;
  last_finalized_distance_ = distance;
  //printf("address %lx RD %u\n", address, distance);
}


bool SampledReuseStack::SampleAccess(address_t address, int thread, address_t PC, bool is_write) {
  if (!global_enable_ || !thread_enable_[thread]) return false;
  address = GetBlock(address);
  sample_count_++;
  addresses_per_sample_total_ += global_tracked_address_count_;
  if (sampled_addresses_[thread].count(address) > 0) {  // this is a reuse
    DeleteAddress(address, thread, PC, false);
  }
  if (is_write) {
    // check for invalidations on the other threads
    for (unsigned int i = 0; i < stats_.size(); i++) {
      if (stats_[i].get() != NULL && i != static_cast<unsigned int>(thread)) {
        Invalidate(address, i, PC);
      }
    }
  }
  trace_.TraceAccess(thread, address * block_bytes_, is_write);
  if (sampled_addresses_[thread].size() == 0) return global_tracked_address_count_ == 0;
  std::vector<address_t> full_sets;
  for (std::map<address_t, SetAndHoles>::iterator iter = sampled_addresses_[thread].begin();
       iter != sampled_addresses_[thread].end(); ++iter) {
    iter->second.reference_lifetime++;
    // if it's not already in the set and there are any holes, a hole is filled
    if (iter->second.hole_count > 0 && iter->second.set->count(address) == 0) {
      iter->second.hole_count--;
    }
    // insert it into the set
    iter->second.set->insert(address);
    if (static_cast<int>(iter->second.set->size()) + iter->second.hole_count > kMaxDistance) {  // reached limit, give it max distance
      full_sets.push_back(iter->first);
    }
  }
  // process the full sets here, we dont want to remove it while we're still using the iterator
  for (int i = 0; i < (int)full_sets.size(); i++) {
    // since we hit the limit, treat this as a cold miss
    DeleteAddress(full_sets[i], thread, PC, true);
    limit_count_++;
  }
  return global_tracked_address_count_ == 0;
}

std::pair<int, acc_count_t> SampledReuseStack::RecordLeftovers() {
  int leftover_count = 0;
  acc_count_t leftover_distance = 0;
  // clean up any remaining sampled addresses from all the threads
  for (unsigned int i = 0; i < stats_.size(); i++) {
    if (stats_[i].get() != NULL) {
      while (!sampled_addresses_[i].empty()) {
        // like invalidate, delete the address a cold miss
        std::map<address_t, SetAndHoles>::iterator iter = sampled_addresses_[i].begin();
        limit_count_++;
        leftover_count++;
        leftover_distance += iter->second.set->size();
        printf(" %"PRIaddr" dist %"PRIacc" lifetime %"PRIacc ", ", iter->first * block_bytes_,
                     iter->second.set->size() + iter->second.hole_count, iter->second.reference_lifetime);
        DeleteAddress(iter->first, i, 0, true);
      }
      printf("; ");
    }
  }
  printf("\n");
  return std::pair<int, acc_count_t>(leftover_count, leftover_distance);
}

void SampledReuseStack::Invalidate(address_t address, int thread, address_t PC) {
  bool remove_address = false;
  // for every address tracked by thread i, must invalidate it if this address is in that set
  for (std::map<address_t, SetAndHoles>::iterator iter = sampled_addresses_[thread].begin();
       iter != sampled_addresses_[thread].end(); ++iter) {
    // if the address being tracked is invalidated, can remove it right away
    if (iter->first == address) {
      remove_address = true;
    }
    else if (iter->second.set->count(address)) {
      // remove it from the set, and add a hole
      iter->second.set->erase(address);
      iter->second.hole_count++;
      invalidation_count_++;
    }
  }
  if (remove_address) {
    invalidation_count_++;
    // Delete the address as a cold miss
    DeleteAddress(address, thread, PC, true);
  }
}

int64_t SampledReuseStack::DumpStats(const std::string &extra) {
  printf("Enabled sampling accesses %"PRIacc", average addresses per enabled sample %.2f\n",
         sample_count_, addresses_per_sample_total_ / static_cast<double>(sample_count_));
  std::pair<int, acc_count_t> leftover_stats(RecordLeftovers());
  printf("Recorded %d leftover addresses as cold misses\n", leftover_stats.first);
  if (global_tracked_address_count_ != 0) {
    printf("Error, global_tracked_address_count = %d\n", global_tracked_address_count_);
  }
  acc_count_t total_distance = 0;
  acc_count_t addr_sample_count = 0;
  acc_count_t sampled_address_count = 0;
  acc_count_t inf_dist_samples = 0;
  // add accessCount and blockAccessCount, as well as initializations
  // for single, sim, delayed, pre
  fprintf(output_file_, "#librda version %s\n", LIBRDA_GIT_VERSION);
  fprintf(output_file_, "singleStacks = {}\n");
  fprintf(output_file_, "simStacks = {}\n");
  fprintf(output_file_, "delayStacks = {}\n");
  fprintf(output_file_, "preStacks = {}\n");
  for (unsigned int thread = 0; thread < stats_.size(); thread++) {
    if (stats_[thread].get() != NULL) {
      // stackholder-like output
      fprintf(output_file_, "#rddata simStacks[%d] = ", thread);
      // reusestack-like output
      fprintf(output_file_, "[");
      fprintf(output_file_, "%s,", stats_[thread]->GetHistogramString().c_str());
      //  attributes
      fprintf(output_file_, "{'sampledAddresses':%"PRIacc", ", sampled_address_count_[thread]);
      fprintf(output_file_, "'accessCount':%"PRIacc", ", stats_[thread]->GetTotalSamples());
      fprintf(output_file_, "'blockAccessCount':%"PRIacc", ", stats_[thread]->GetTotalSamples());
      fprintf(output_file_, "'addrPerSamp':%.2f, ",
              addresses_per_sample_total_ / static_cast<double>(sample_count_));
      fprintf(output_file_, "'limitCount': %"PRIacc", ", limit_count_);
      fprintf(output_file_, "'invalidationCount': %"PRIacc", ", invalidation_count_);
      fprintf(output_file_, "%s", stats_[thread]->GetAttributes().c_str());
      fprintf(output_file_, "}]\n");  // end attribute dict and rddata list

      sampled_address_count += sampled_address_count_[thread];
      total_distance += stats_[thread]->GetTotalDistance();
      addr_sample_count += stats_[thread]->GetTotalSamples();
      inf_dist_samples += stats_[thread]->GetColdSamples();
    }
  }

  fprintf(output_file_, "%s", PC_stats_.GetStatsString().c_str());
  fprintf(output_file_, "%s", extra.c_str());

  if (sampled_address_count != addr_sample_count) printf("Error, sampled addr count mismatch\n");
  printf("Overall avg reference lifetime %.2f\n",
         static_cast<double>(reference_lifetime_total_) / addr_sample_count);
  printf("Overall avg life distance %.2f plus %.5f%% inf dist\n",
         (total_distance)
         / static_cast<double>(addr_sample_count - inf_dist_samples),
         inf_dist_samples / static_cast<double>(addr_sample_count));
  return addresses_per_sample_total_;
}

void SampledReuseStack::set_thread_enable(int thread, bool enable) {
  trace_.TraceThreadEnable(thread, enable);
  SampledReuseStackInterface::set_thread_enable(thread, enable);
}

void SampledReuseStack::TraceMerge(int thread) {
  trace_.TraceMerge(thread);
}

const SetAndHoles *SampledReuseStack::GetSetAndHoles(int thread, address_t address) {
  if (sampled_addresses_[thread].count(GetBlock(address)) == 0) return NULL;
  return &sampled_addresses_[thread][GetBlock(address)];
}
