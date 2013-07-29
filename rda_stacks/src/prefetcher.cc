/*
 * prefetcher.cc
 *
 *  Created on: Oct 6, 2010
 *      Author: dschuff
 */

#include <boost/format.hpp>
#include "prefetcher.h"

using std::string;
using boost::format;

const int DCUPrefetcher::kRequestProximity;
const int PrefetchArbiter::kPrefetchRepeatFrequency;

PrefetcherInterface::~PrefetcherInterface() {}

void PrefetchArbiter::AddPrefetcher(PrefetcherInterface *pf) {
  prefetchers_.push_back(pf);
  accepted_requests_.push_back(0);
  dropped_requests_.push_back(0);
}

address_t PrefetchArbiter::Access(address_t addr, address_t PC, acc_count_t distance, bool write) {
  IncrementTime();
  //check for hits against the prefetch queue
  bool cancel_hit;
  //erasing invalidates all iterators but there could be more than one queue entry which hits
  do {
    cancel_hit = false;
    for (PrefetchQueue::iterator iter = prefetch_queue_.begin();
         iter != prefetch_queue_.end(); ++iter) {
      if (addr == iter->addr) {
        queue_cancels_++;
        prefetch_queue_.erase(iter);
        cancel_hit = true;
        break;
      }
    }
  } while (cancel_hit);

  //update stats
  if (prefetched_addrs_.count(addr)) {
    if (pc_stats_.count(PC) == 0) pc_stats_.insert(std::pair<address_t, PrefetchStats>(PC,PrefetchStats()));
    PrefetchedPCStats::iterator stats = pc_stats_.find(PC);
    stats->second.prefetched_hits += !is_miss(distance);
    stats->second.prefetched_misses += is_miss(distance);
    prefetched_addrs_.erase(addr);
  }
  //get requests from all prefetchers
  int i = 0;
  for (PrefetcherList::iterator iter = prefetchers_.begin();
       iter != prefetchers_.end(); ++iter, i++) {
    address_t ret = (*iter)->Access(addr, PC, distance, write);
    if (ret != kAddressMax) {
      SetRequestInput(ret, PC, i);
    }
  }
  return GetRequestOutput();
}

void PrefetchArbiter::SetRequestInput(address_t addr, address_t PC, int index) {
  // age out recent prefetches
  while (recent_prefetches_.size() > 0 && recent_prefetches_.front().expire_time <= current_time_) {
    recent_prefetch_addrs_.erase(recent_prefetches_.front().addr);
    recent_prefetches_.pop_front();
  }
  if (recent_prefetch_addrs_.count(addr) == 0) {
    prefetch_queue_.push_back(PrefetchRequest(addr, PC, current_time_ + kPrefetchDelay, index));
    recent_prefetch_addrs_.insert(addr);// = current_time_ + kPrefetchRepeatFrequency;
    acc_count_t this_prefetch_expire = current_time_ + kPrefetchRepeatFrequency;
    recent_prefetches_.push_back(RecentPrefetch(addr, this_prefetch_expire));
  }
}

address_t PrefetchArbiter::GetRequestOutput() {
  address_t ret = kAddressMax;
  // for now, insertion order will arbitrate between prefetchers
  if (prefetch_queue_.size() > 0 && prefetch_queue_.front().time <= current_time_) {
    PrefetchQueue::iterator front = prefetch_queue_.begin();
    ret = front->addr;
    accepted_requests_[front->requester_index]++;
    pc_stats_[front->PC].requests++;
    prefetched_addrs_.insert(ret);
    prefetch_queue_.pop_front();
  }
  while (prefetch_queue_.size() > 0 && prefetch_queue_.front().time <= current_time_) {
    dropped_requests_[prefetch_queue_.front().requester_index]++;
    prefetch_queue_.pop_front();
  }
  return ret;
}

string PrefetchArbiter::GetStatsString() {
  acc_count_t total_accepted = 0;
  acc_count_t total_dropped = 0;
  for (unsigned int i = 0; i < prefetchers_.size(); i++) {
    total_accepted += accepted_requests_[i];
    total_dropped += dropped_requests_[i];
  }
  string out = str(format("{'prefetches':%u, 'dropped':%u, 'canceled':%u,") % 
		   total_accepted % total_dropped % queue_cancels_);
  out += "'prefetchers':[";
  int i = 0;
  for (PrefetcherList::iterator iter = prefetchers_.begin(); iter != prefetchers_.end();
       ++iter, i++) {
    out += str(format("{'accepted':%u, 'dropped':%u, ") % accepted_requests_[i] % dropped_requests_[i])
        +  (*iter)->GetStatsString() + "}, ";
  }
  out += "],";
  out += "'PCs':{";
  for (PrefetchedPCStats::iterator iter = pc_stats_.begin();
       iter != pc_stats_.end(); ++iter) {
    out += str(format("%u:(%u, %u, %u), ") % iter->first % iter->second.requests %
               iter->second.prefetched_hits % iter->second.prefetched_misses);
  }
  out += "},";
  out += "}\n";
  return out;
}

PrefetchArbiter::~PrefetchArbiter() {
  for (PrefetcherList::iterator iter = prefetchers_.begin(); iter != prefetchers_.end(); ++iter) {
    delete *iter;
  }
}

DCUPrefetcher::DCUPrefetcher() : prefetches_(0), miss_triggers_(0), prefetch_hit_triggers_(),
    current_access_(0), recent_accesses_(kRequestProximity, kStackNotFound) {
}

address_t DCUPrefetcher::Access(address_t addr, address_t PC, acc_count_t distance, bool write) {
  address_t ret = kAddressMax;
  // check for 2 accesses to the same block within short time, and issue
  // prefetch for the next line
  // if kRequestProximity gets large, probably want a more efficient way to do this
  for (int i = 0; i < kRequestProximity; i++) {
    if (recent_accesses_[i] == addr) {
      ret = addr + 1;
      //prefetch_record_.insert(ret);
      prefetches_++;
      break;
    }
  }
  recent_accesses_[current_access_++] = addr;
  if (current_access_ >= kRequestProximity) current_access_ = 0;

  //issue any outstanding prefetches
  return ret;
}

string DCUPrefetcher::GetStatsString() {
  string out("'name':'DCU', ");
  out += str(format("'prefetches':%u, ") % prefetches_);
  out += str(boost::format("'miss_triggers':%d, ") % miss_triggers_);
  out += str(boost::format("'prefetch_hit_triggers':%d, ") % prefetch_hit_triggers_);
  return out;
}

