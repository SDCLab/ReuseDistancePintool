/*
 * SampledReuseStack.h
 *
 *  Created on: Sep 16, 2009
 *      Author: dschuff
 */

#ifndef SAMPLEDREUSESTACK_H_
#define SAMPLEDREUSESTACK_H_

#include <cstdio>
#include <map>
#include <set>
#include <vector>
#include <tr1/memory>
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#include "reusestack.h"
#include "trace.h"

typedef std::tr1::unordered_set<address_t> AddressHashSet;
//typedef std::pair<std::tr1::shared_ptr<AddressHashSet>, int> SetAndHoles;
typedef struct {
  std::tr1::shared_ptr<AddressHashSet> set;
  acc_count_t hole_count;
  acc_count_t reference_lifetime;
  address_t PC;
} SetAndHoles;

class SampledReuseStackInterface {
public:
  const static acc_count_t kMaxDistance = 50000000;
  const static int kDefaultGranularity = 64;
  SampledReuseStackInterface() : global_enable_(true),
                                 thread_enable_(kDefaultThreadAllocation, true) {}
  virtual ~SampledReuseStackInterface() {}
  virtual void Allocate(int thread) = 0;
  virtual void NewSampledAddress(address_t address, int thread, address_t PC) = 0;
  virtual bool SampleAccess(address_t address, int thread, address_t PC, bool is_write) = 0;
  virtual int64_t DumpStats(const std::string &extra) = 0;
  virtual bool global_enable() { return global_enable_; }
  virtual void set_global_enable(bool enable) { global_enable_ = enable; }
  virtual void set_thread_enable(int thread, bool enable) { thread_enable_[thread] = enable; }
  virtual void TraceMerge(int thread) {};
protected:
  const static int kDefaultThreadAllocation = 8;
  bool global_enable_;
  std::vector<uint8_t> thread_enable_;
};

// class for one thread worth of sampled RD
class SampledReuseStack : public SampledReuseStackInterface {
public:
  SampledReuseStack(std::string output_filename, int granularity);
  virtual ~SampledReuseStack();
  virtual void Allocate(int thread);
  virtual void NewSampledAddress(address_t address, int thread, address_t PC);
  virtual bool SampleAccess(address_t address, int thread, address_t PC, bool is_write);
  virtual int64_t DumpStats(const std::string &extra);
  virtual void set_thread_enable(int thread, bool enable);
  virtual void TraceMerge(int thread);
  acc_count_t GetLastDistance() { return last_finalized_distance_;}
  acc_count_t GetGlobalTrackedAddressCount() { return global_tracked_address_count_; }
private:
  friend class SampledReuseStackTest;
  const SetAndHoles *GetSetAndHoles(int thread, address_t address);
  address_t GetBlock(address_t address) { return address / block_bytes_; }
  void DeleteAddress(address_t address, int thread, address_t PC, bool max_limit);
  void Invalidate(address_t address, int thread, address_t PC);
  std::pair<int, acc_count_t> RecordLeftovers();
  // each of these has one element per thread. stats_ is the canonical one that determines
  // whether a thread has been allocated.
  std::vector<std::tr1::shared_ptr<ReuseStackStats> > stats_;
  std::vector<acc_count_t> sampled_address_count_;
  std::vector<std::map<address_t, SetAndHoles> > sampled_addresses_;

  acc_count_t sample_count_;  // these need to be synchronized or eliminated later
  int64_t reference_lifetime_total_;
  int64_t addresses_per_sample_total_;
  int32_t global_tracked_address_count_;  // number of addresses currently tracked by all threads
  acc_count_t limit_count_;  // how many sampled addresses reach max RD limit
  acc_count_t invalidation_count_; // how many sampled addrs get invalidated while active
  acc_count_t last_finalized_distance_;  // for testing
  FILE *output_file_;
  int block_bytes_;

  PCStats PC_stats_;
  OutputTrace trace_;
  DISALLOW_COPY_AND_ASSIGN(SampledReuseStack);
};

#endif /* SAMPLEDREUSESTACK_H_ */
