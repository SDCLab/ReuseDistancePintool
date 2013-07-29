/*
 * sharedsampledreusestack.h
 *
 *  Created on: Feb 2, 2010
 *      Author: dschuff
 */

#ifndef SHAREDSAMPLEDREUSESTACK_H_
#define SHAREDSAMPLEDREUSESTACK_H_

#include <map>
#include <boost/scoped_ptr.hpp>
#include "sampledreusestack.h"

class SharedSampledReuseStack : public SampledReuseStackInterface {
public:
  SharedSampledReuseStack(std::string output_filename, int granularity);
  virtual ~SharedSampledReuseStack();
  virtual void Allocate(int thread);
  virtual void NewSampledAddress(address_t address, int thread, address_t PC);
  virtual bool SampleAccess(address_t address, int thread, address_t PC, bool is_write);
  virtual int64_t DumpStats(const std::string &extra);
private:
  address_t GetBlock(address_t address) { return address / block_bytes_; }
  void DeleteAddress(address_t address, address_t PC, bool max_limit);
  std::pair<int, acc_count_t> RecordLeftovers();
  int block_bytes_;
  boost::scoped_ptr<ReuseStackStats> stats_;
  acc_count_t sampled_address_count_;
  std::map<address_t, SetAndHoles> sampled_addresses_;

  acc_count_t sample_count_;  // these need to be synchronized or eliminated later
  int64_t reference_lifetime_total_;
  int64_t addresses_per_sample_total_;
  int32_t global_tracked_address_count_;  // number of addresses currently tracked by all threads
  acc_count_t limit_count_;  // how many sampled addresses reach max RD limit
  FILE *output_file_;

  PCStats PC_stats_;
  DISALLOW_COPY_AND_ASSIGN(SharedSampledReuseStack);
};

#endif /* SHAREDSAMPLEDREUSESTACK_H_ */
