/*
 * stackholder.h
 *
 *  Created on: Aug 27, 2009
 *      Author: dschuff
 */

#ifndef STACKHOLDER_H_
#define STACKHOLDER_H_

#include <stdio.h>

#include <map>
#include <stdexcept>
#include <string>
#include <tr1/unordered_map>
#include <vector>

#include "reusestack.h"
#include "strideprefetcher.h"
#include "globalstreamprefetcher.h"

/*
 * Abstract out handling of reuse stacks. Include allocation and Access, specify what kinds
 * of stacks are used, and dump stats.
 */
class StackHolder {
public:
  const static int kDefaultGranularity = 64;

  StackHolder(const std::string& statsfile_name, int granularity, const std::string& stack_type)
      throw(std::invalid_argument);
  ~StackHolder();
  void Allocate(int thread);
  acc_count_t Access(int thread, address_t address, int size, address_t PC, bool is_write);
  void Fetch(int thread, address_t PC, int size);
  void AddRatioPredictionSize(int size);
  void AddPairPredictionSize(int size);
  void AddSharedPredictionSize(int size);
  void EndParallelRegion();
  // called at the same time as reuse_hit_ratios() in memstat
  void UpdateRatioPredictions();
  void DumpStatsPython(const std::string &extra);
  void FlushStats() { fflush(statsfile_); }
  std::string GetStatsFileName() { return statsfile_name_; }
  FILE * GetStatsFileHandle() { return statsfile_; }
  // Returns true if 'thread' is not yet tracked by the stackholder
  bool IsNewThread(int thread);

  void SetThreadEnabled(int thread, bool enable) { threads_enabled_[thread] = enable; }
  bool GetThreadEnabled(int thread) { return threads_enabled_[thread]; }
  int GetPairCache(int thread) { return share_map_[thread]; }

  bool do_inval() { return do_inval_; }
  void set_do_inval(bool inval) { do_inval_ = inval; }
  bool do_shared() {return do_shared_; }
  void set_do_shared(bool shared) { do_shared_ = shared; }
  bool do_single_stacks() { return do_single_stacks_; }
  void set_do_single_stacks(bool single) { do_single_stacks_ = single; }
  bool do_sim_stacks() { return do_sim_stacks_; }
  void set_do_sim_stacks(bool sim) { do_sim_stacks_ = sim; }
  bool do_lazy_stacks() { return do_lazy_stacks_; }
  void set_do_lazy_stacks(bool lazy) { do_lazy_stacks_ = lazy; }
  bool do_oracular_stacks() { return do_oracular_stacks_; }
  void set_do_oracular_stacks(bool oracular) { do_oracular_stacks_ = oracular; }
  int merge_interleave() { return merge_interleave_; }
  void set_merge_interleave(int interleave) { merge_interleave_ = interleave; }
  bool get_global_enable() { return global_enable_; }
  void set_global_enable(bool enable) { global_enable_ = enable; }
  bool do_prefetch() { return do_prefetch_; }
  void set_do_prefetch(bool prefetch) { do_prefetch_ = prefetch; }
  bool do_fetch() { return do_fetch_; }
  void set_do_fetch(bool fetch) { do_fetch_ = fetch; }
  int granularity() { return granularity_; }

private:
  const static int share_map_[9];

  bool do_inval_;
  bool do_shared_;
  bool do_single_stacks_;
  bool do_sim_stacks_;
  bool do_lazy_stacks_;
  bool do_oracular_stacks_;
  int merge_interleave_;
  bool global_enable_;
  bool do_prefetch_;
  bool do_fetch_;

  // invalidation stacks
  std::map<int, ReuseStackBase *> single_stacks_;
  std::map<int, ReuseStackBase *> sim_stacks_;
  std::map<int, ReuseStackBase *> lazy_stacks_;
  std::map<int, ReuseStackBase *> oracular_stacks_;
  // Shared stack
  ReuseStackBase* simulated_shared_stack_;
  std::map<int, ReuseStackBase *> pair_share_stacks_;

  std::map<int, PrefetchArbiter *> prefetchers_;//just private for now

  std::vector<int> threads_seen_;
  std::map<int, bool> threads_enabled_;

  std::string statsfile_name_;
  FILE * statsfile_;
  int granularity_;

  std::map<int, std::vector<BufferedRef> > buffered_accesses_;

  std::vector<int> ratio_prediction_sizes_;
  std::vector<int> shared_prediction_sizes_;
  std::vector<int> pair_prediction_sizes_;

  ReuseStack::StackImplementationTypes stack_type_;

  // For now these are still in memstat.cc because the cache simulator needs them too but we might
  // want them in here later.
  //std::vector<int64_t> region_times_;
  //std::vector<int64_t> region_accesses_;

  PCStats PC_stats_;
  PCStats PC_read_stats_;

  DISALLOW_COPY_AND_ASSIGN(StackHolder);
};

#endif /* STACKHOLDER_H_ */
