/*
 * prefetcher.h
 *
 *  Created on: Oct 6, 2010
 *      Author: dschuff
 */

#ifndef PREFETCHER_H_
#define PREFETCHER_H_

#include <deque>
#include <map>
#include <string>
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#include <vector>
#include "reusestack-common.h"

/* Interface for prefetchers, including the arbiter*/
class PrefetcherInterface {
public:
  /* Records access and returns an address to prefetch, if any */
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write) = 0;
  /* Returns string containing stats */
  virtual std::string GetStatsString() = 0;
  virtual ~PrefetcherInterface() = 0;
 protected:
  const static acc_count_t kMissDistance = (64 * 1024) / 64;//specify this in BLOCKS
  static bool is_miss(acc_count_t distance) { return distance > kMissDistance; }
};

/* Determines when to issue prefetches, and filters duplicate requests */
class PrefetchArbiter : public PrefetcherInterface {
public:
  // the same block can only be prefetched once in this many references
  const static int kPrefetchRepeatFrequency = 10 * 10;
  const int kPrefetchDelay;
  PrefetchArbiter() : kPrefetchDelay(0), current_time_(0), queue_cancels_(0) {}
  PrefetchArbiter(int delay) : kPrefetchDelay(delay), current_time_(0), queue_cancels_(0) {}
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write);
  virtual std::string GetStatsString();
  void AddPrefetcher(PrefetcherInterface *pf);
  virtual ~PrefetchArbiter();
private:
  class PrefetchRequest {
  public:
    address_t addr;
    address_t PC;
    acc_count_t time;
    int requester_index;
    PrefetchRequest(address_t address, acc_count_t pc, acc_count_t time, int index) :
      addr(address), PC(pc), time(time), requester_index(index) {}
  };
  class RecentPrefetch {
  public:
    address_t addr;
    acc_count_t expire_time;
    RecentPrefetch(address_t a, acc_count_t t) : addr(a), expire_time(t) {}
  };
  class PrefetchStats {
  public:
    acc_count_t requests;
    acc_count_t prefetched_hits;
    acc_count_t prefetched_misses;
  };
  typedef std::vector<PrefetcherInterface *> PrefetcherList;
  typedef std::deque<PrefetchRequest> PrefetchQueue;
  typedef std::tr1::unordered_map<address_t, PrefetchStats> PrefetchedPCStats;
  void IncrementTime() {
    current_time_++;
  }
  void SetRequestInput(address_t block, address_t PC, int index);
  address_t GetRequestOutput();
  acc_count_t current_time_;
  acc_count_t queue_cancels_;
  std::vector<acc_count_t> accepted_requests_;
  std::vector<acc_count_t> dropped_requests_;
  acc_count_t prefetched_hits_;
  acc_count_t prefetched_misses_;
  PrefetchedPCStats pc_stats_;
  std::tr1::unordered_set<address_t> prefetched_addrs_;
  PrefetcherList prefetchers_;
  std::deque<RecentPrefetch> recent_prefetches_;
  std::tr1::unordered_set<address_t> recent_prefetch_addrs_;
  PrefetchQueue prefetch_queue_;


};

class DCUPrefetcher : public PrefetcherInterface {
public:
  // 2 refs must be this close together to trigger prefetch
  const static int kRequestProximity = 10;
  DCUPrefetcher();
  /* Records access and returns an address to prefetch, if any */
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write);
  /* Returns string containing stats */
  virtual std::string GetStatsString();
private:
  acc_count_t prefetches_;
  acc_count_t miss_triggers_;
  acc_count_t prefetch_hit_triggers_;
  int current_access_;
  std::vector<address_t> recent_accesses_;
  //std::tr1::unordered_set<address_t> prefetch_record_;
};

#endif /* PREFETCHER_H_ */
