/*
 * strideprefetcher.cc
 *
 *  Created on: Nov 3, 2010
 *      Author: dschuff
 */

#include <boost/format.hpp>
#include "strideprefetcher.h"

const int StridePrefetcher::kStrideTableSize;
const int StridePrefetcher::Min_Conf;
const int StridePrefetcher::Max_Conf;
const bool StridePrefetcher::kIgnoreWrites;

address_t StridePrefetcher::Access(address_t addr, address_t PC, acc_count_t distance,
    bool write) {
  // if this is a miss or a hit to a prefetched block, maybe issue another prefetch
  if ((!write || !kIgnoreWrites) &&
      (PrefetcherInterface::is_miss(distance) || prefetch_record_.count(addr))) {
    if (PrefetcherInterface::is_miss(distance)) {
      miss_triggers_++;
    } else {
      prefetch_hit_triggers_++;
    }
    prefetch_record_.erase(addr);

    /* Search Table for instAddr Match */
    if (stride_table_.count(PC)) {
      // Hit in table
      StrideEntry *entry = stride_table_[PC];
      int new_stride = addr - entry->missAddr;
      bool stride_match = (new_stride == entry->stride);


      if (stride_match && new_stride != 0) {
          if (entry->confidence < Max_Conf)
              entry->confidence++;
      } /*else if (new_stride != 0 && new_stride == entry->lastStride) {
        //alternating stride
        entry->lastStride = entry->stride;
        entry->stride = new_stride;
        new_stride = entry->lastStride;
        if (entry->confidence < Max_Conf)
          entry->confidence++;
      }*/
      else {
        entry->lastStride = entry->stride;
        entry->stride = new_stride;
        if (entry->confidence > Min_Conf)
            entry->confidence = 0;
      }

      entry->missAddr = addr;

      if (entry->confidence <= 0)
          return kAddressMax;

      address_t new_addr = addr + new_stride;
      prefetch_record_.insert(new_addr);
      prefetches_++;
      return new_addr;
    } else {
      // Miss in table
      // Find lowest confidence and replace
      if (stride_table_.size() >= (unsigned)kStrideTableSize) {
        std::tr1::unordered_map<address_t, StrideEntry*>::iterator min_pos = stride_table_.begin();
        int min_conf = min_pos->second->confidence;
        std::tr1::unordered_map<address_t, StrideEntry*>::iterator iter;
        for (iter = min_pos, ++iter; iter != stride_table_.end(); ++iter) {
          if (iter->second->confidence < min_conf){
            min_pos = iter;
            min_conf = iter->second->confidence;
          }
        }
        StrideEntry *del = min_pos->second;
        stride_table_.erase(min_pos);
        delete del;
      }

      StrideEntry *new_entry = new StrideEntry;
      new_entry->instAddr = PC;
      new_entry->missAddr = addr;
      new_entry->stride = 0;
      new_entry->lastStride = 0;
      new_entry->confidence = 0;
      stride_table_[PC] = new_entry;
    }
  }
  return kAddressMax;
}

std::string StridePrefetcher::GetStatsString() {
  std::string out("'name':'IP stride', ");
  out = str(boost::format("'prefetches':%d, ") % prefetches_);
  out += str(boost::format("'miss_triggers':%d, ") % miss_triggers_);
  out += str(boost::format("'prefetch_hit_triggers':%d, ") % prefetch_hit_triggers_);
  return out;
}

StridePrefetcher::~StridePrefetcher() {
  std::tr1::unordered_map<address_t, StrideEntry*>::iterator iter;
  for (iter = stride_table_.begin(); iter != stride_table_.end(); ++iter) {
    delete iter->second;
  }
}
