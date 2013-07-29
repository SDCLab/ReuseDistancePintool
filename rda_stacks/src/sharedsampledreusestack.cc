/*
 * sharedsampledreusestack.cc
 *
 *  Created on: Feb 2, 2010
 *      Author: dschuff
 */
#include "sharedsampledreusestack.h"
#include <stdexcept>
#include "version.h"

SharedSampledReuseStack::SharedSampledReuseStack(std::string output_filename, int granularity)
    : block_bytes_(granularity), stats_(new ReuseStackStats(block_bytes_)),
      sampled_address_count_(0), sampled_addresses_(),
      sample_count_(0), addresses_per_sample_total_(0), global_tracked_address_count_(0),
      limit_count_(0), output_file_(NULL) {
  output_file_ = fopen(output_filename.c_str(), "w");
  if (output_file_ == NULL) {
    throw std::invalid_argument("Could not open output file " + output_filename);
  }
}

SharedSampledReuseStack::~SharedSampledReuseStack() {
  //free stats_ stuff
  fclose(output_file_);
}

void SharedSampledReuseStack::Allocate(int thread) {
  thread_enable_[thread] = true;
}

void SharedSampledReuseStack::NewSampledAddress(address_t address, int thread, address_t PC) {
  if (!global_enable_ || !thread_enable_[thread]) return;
  address = GetBlock(address);
  if (sampled_addresses_.count(address) != 0) {
    return;  //will eventually have to handle this but ignore for now
  }
  //printf("sampling address %lx\n", address);
  sampled_address_count_++;
  sampled_addresses_[address].set.reset(new AddressHashSet());
  sampled_addresses_[address].hole_count = 0;
  sampled_addresses_[address].reference_lifetime = 0;
  sampled_addresses_[address].PC = PC;
  global_tracked_address_count_++;
}

void SharedSampledReuseStack::DeleteAddress(address_t address, address_t PC, bool max_limit) {
  acc_count_t distance;
  if (max_limit) {
    distance = kStackNotFound;
  } else {
    distance = sampled_addresses_[address].set->size() +  // size of set
      sampled_addresses_[address].hole_count; // number of holes
  }
  //remove from set and record stat sample
  stats_->AddSample(address, distance);
  PC_stats_.AddSample(PC, distance);
  reference_lifetime_total_ += sampled_addresses_[address].reference_lifetime;
  sampled_addresses_.erase(address);
  global_tracked_address_count_--;
  //printf("address %lx RD %u\n", address, distance);
}

bool SharedSampledReuseStack::SampleAccess(address_t address, int thread,
                                           address_t PC, bool is_write) {
  if (!global_enable_ || !thread_enable_[thread]) return false;
  address = GetBlock(address);
  sample_count_++;
  addresses_per_sample_total_ += global_tracked_address_count_;
  if (sampled_addresses_.count(address) > 0) {  // this is a reuse
    DeleteAddress(address, PC, false);
  }

  if (sampled_addresses_.size() == 0) return global_tracked_address_count_ == 0;
  std::vector<address_t> full_sets;
  for (std::map<address_t, SetAndHoles>::iterator iter = sampled_addresses_.begin();
       iter != sampled_addresses_.end(); ++iter) {
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
    DeleteAddress(full_sets[i], PC, true);
    limit_count_++;
  }
  return global_tracked_address_count_ == 0;
}

std::pair<int, acc_count_t> SharedSampledReuseStack::RecordLeftovers() {
  int leftover_count = 0;
  acc_count_t leftover_distance = 0;
  // clean up any remaining sampled addresses from all the threads

  while (!sampled_addresses_.empty()) {
    // like invalidate, delete the address a cold miss
    std::map<address_t, SetAndHoles>::iterator iter = sampled_addresses_.begin();
    limit_count_++;
    leftover_count++;
    leftover_distance += iter->second.set->size();
    DeleteAddress(iter->first, 0, true);
  }
  return std::pair<int, acc_count_t>(leftover_count, leftover_distance);
}

int64_t SharedSampledReuseStack::DumpStats(const std::string &extra) {
  printf("Enabled samples %"PRIacc", average addresses per enabled sample %.2f\n",
         sample_count_, addresses_per_sample_total_ / static_cast<double>(sample_count_));
  std::pair<int, acc_count_t> leftover_stats(RecordLeftovers());
  printf("Recorded %d leftover addresses as cold misses\n", leftover_stats.first);
  if (global_tracked_address_count_ != 0) {
    printf("Error, global_tracked_address_count = %d\n", global_tracked_address_count_);
  }
  acc_count_t total_distance = 0;
  acc_count_t addr_sample_count = 0;
  acc_count_t sampled_address_count = 0;
  // add accessCount and blockAccessCount, as well as initializations
  // for single, sim, delayed, pre
  fprintf(output_file_, "#librda version %s\n", LIBRDA_GIT_VERSION);
  fprintf(output_file_, "singleStacks = {}\n");
  fprintf(output_file_, "simStacks = {}\n");
  fprintf(output_file_, "delayStacks = {}\n");
  fprintf(output_file_, "preStacks = {}\n");
  fprintf(output_file_, "pairStacks = {}\n");

  // stackholder-like output
  fprintf(output_file_, "#rddata simSharedStack = ");
  // reusestack-like output
  fprintf(output_file_, "[");
  fprintf(output_file_, "%s,", stats_->GetHistogramString().c_str());
  //  attributes
  fprintf(output_file_, "{'sampledAddresses':%"PRIacc", ", sampled_address_count_);
  fprintf(output_file_, "'accessCount':%"PRIacc", ", stats_->GetTotalSamples());
  fprintf(output_file_, "'blockAccessCount':%"PRIacc", ", stats_->GetTotalSamples());
  fprintf(output_file_, "'addrPerSamp':%.2f, ",
          addresses_per_sample_total_ / static_cast<double>(sample_count_));
  fprintf(output_file_, "'limitCount': %"PRIacc", ", limit_count_);
  fprintf(output_file_, "%s", stats_->GetAttributes().c_str());
  fprintf(output_file_, "}]\n");  // end attribute dict and rddata list

  sampled_address_count += sampled_address_count_;
  total_distance += stats_->GetTotalDistance();
  addr_sample_count += stats_->GetTotalSamples();

  fprintf(output_file_, "%s", PC_stats_.GetStatsString().c_str());
  fprintf(output_file_, "%s", extra.c_str());

  if (sampled_address_count != addr_sample_count) printf("Error, sampled addr count mismatch\n");
  printf("Overall avg reference lifetime %.2f\n",
         static_cast<double>(reference_lifetime_total_) / addr_sample_count);
  printf("Overall avg life distance %.2f\n",
         (total_distance + (limit_count_ - leftover_stats.first) * kMaxDistance + leftover_stats.second)
         / static_cast<double>(addr_sample_count));
  return addresses_per_sample_total_;
}
