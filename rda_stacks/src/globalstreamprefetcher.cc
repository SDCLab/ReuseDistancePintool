/*
 * globalstreamprefetcher.cc
 *
 *  Created on: Nov 15, 2010
 *      Author: Derek
 */

#include "globalstreamprefetcher.h"

const int GlobalStreamPrefetcher::kMaxConfidence;
const int GlobalStreamPrefetcher::kPrefetchConfidenceThreshold;
const int GlobalStreamPrefetcher::kStreamTableSize;
const bool GlobalStreamPrefetcher::kIgnoreWrites;

address_t GlobalStreamPrefetcher::Access(address_t addr, address_t PC, acc_count_t distance,
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

    bool stride_match = false;
    bool stream_match = false;
    int new_stride = 0;
    /* search table for stride match*/
    StreamTable::iterator iter;
    for (iter = streams_.begin(); iter != streams_.end(); ++iter) {
      if (iter->stride  != 0 && iter->last_miss + iter->stride == addr) {
        stride_match = true;
        if (iter->confidence < kMaxConfidence) iter->confidence++;
        new_stride = iter->stride;
        break;
      } else if (iter->last_stride != 0 && iter->last_miss + iter->last_stride == addr) {
        //alternating stride
        stride_match = true;
        if (iter->confidence < kMaxConfidence) iter->confidence++;
        iter->last_stride = iter->stride;
        iter->stride = addr - iter->last_miss;
        new_stride = iter->last_stride;
        break;
      }
    }
    if (stride_match && iter->confidence >= kPrefetchConfidenceThreshold) {
      address_t new_addr = addr + new_stride;
      iter->last_miss = addr;
      prefetch_record_.insert(new_addr);
      prefetches_++;
      return new_addr;
    } else if (!stride_match) {
      //try to find match in uninitialized stream
      for (iter = streams_.begin(); iter != streams_.end(); ++iter) {
        if (addr >= iter->last_miss - 4 && addr <= iter->last_miss + 4) {
          stream_match = true;
          if (iter->stride == 0) { //uninitialized
            iter->stride = addr - iter->last_miss;
            iter->last_miss = addr;
            iter->confidence++;
            break;
          } else if (iter->last_stride == 0) {
            //could be alternating pattern
            iter->last_stride = iter->stride;
            iter->stride = addr - iter->last_miss;
            iter->last_miss = addr;
            break;
          } else {
            // near an existing stream but stride didn't match
            if (iter->confidence > 0) iter->confidence--;
            if (iter->confidence > kPrefetchConfidenceThreshold) {
              address_t new_addr = addr + iter->stride;
              iter->last_miss = addr;
              prefetch_record_.insert(new_addr);
              prefetches_++;
              return new_addr;
            }
          }
        }
      }
      if (!stream_match) {
        if (streams_.size() >= (unsigned)kStreamTableSize) {
          //find lowest confidence and replace
          StreamTable::iterator min_pos = streams_.begin();
          int min_conf = min_pos->confidence;
          for(iter = min_pos; iter != streams_.end(); ++iter) {
            if (iter->confidence < min_conf) {
              min_pos = iter;
              min_conf = iter->confidence;
            }
          }
          streams_.erase(min_pos);
        }
        streams_.push_back(StreamEntry(addr, 0));
      }
    }
  }
  return kAddressMax;
}

std::string GlobalStreamPrefetcher::GetStatsString() {
  return std::string("");
}

GlobalStreamPrefetcher::~GlobalStreamPrefetcher() {

}
