/*
 * globalstreamprefetcher.h
 *
 *  Created on: Nov 14, 2010
 *      Author: Derek
 */

#ifndef GLOBALSTREAMPREFETCHER_H_
#define GLOBALSTREAMPREFETCHER_H_

#include <list>
#include <tr1/unordered_set>
#include "prefetcher.h"

class GlobalStreamPrefetcher : public PrefetcherInterface {
public:
  static const int kStreamTableSize = 8;
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write);
  virtual std::string GetStatsString();
  virtual ~GlobalStreamPrefetcher();
private:
  //confidence stats from AMD memory prefetcher docs
  static const int kMaxConfidence = 7;
  static const int kPrefetchConfidenceThreshold = 2;
  static const bool kIgnoreWrites = true;
  class StreamEntry {
  public:
    address_t last_miss;
    int stride;
    int last_stride;
    int confidence;
    StreamEntry(address_t lastMiss, int stride) : last_miss(lastMiss), stride(stride),
      last_stride(0), confidence(0) {}
  };
  typedef std::list<StreamEntry> StreamTable;
  std::tr1::unordered_set<address_t> prefetch_record_;
  StreamTable streams_;
  acc_count_t prefetches_;
  acc_count_t miss_triggers_;
  acc_count_t prefetch_hit_triggers_;
};

#endif /* GLOBALSTREAMPREFETCHER_H_ */
