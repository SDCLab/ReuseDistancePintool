/*
 * strideprefetcher.h
 *
 *  Created on: Nov 8, 2010
 *      Author: dschuff
 */

#ifndef STRIDEPREFETCHER_H_
#define STRIDEPREFETCHER_H_

#include <list>
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#include "prefetcher.h"

class StridePrefetcher : public PrefetcherInterface {
public:
  StridePrefetcher() : prefetches_(0), miss_triggers_(0),
    prefetch_hit_triggers_(0) {}
  virtual address_t Access(address_t addr, address_t PC, acc_count_t distance, bool write);
  virtual std::string GetStatsString();
  virtual ~StridePrefetcher();
private:
  std::tr1::unordered_set<address_t> prefetch_record_;

  // These constants need to be changed with the type of the
  // 'confidence' field below.
  static const int Max_Conf = 65535;//don't care, just a big number
  static const int Min_Conf = -65536;
  static const int kStrideTableSize = 256;
  static const bool kIgnoreWrites = false;

  class StrideEntry
  {
    public:
      address_t instAddr;
      address_t missAddr;
      int stride;
      int lastStride;
      int confidence;
  };
  std::tr1::unordered_map<address_t, StrideEntry*> stride_table_;

  acc_count_t prefetches_;
  acc_count_t miss_triggers_;
  acc_count_t prefetch_hit_triggers_;
};

#endif /* STRIDEPREFETCHER_H_ */
