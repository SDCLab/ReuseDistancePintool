/*
 * reusestackstats.cc
 *
 *  Created on: Oct 26, 2009
 *      Author: dschuff
 */

#include "reusestackstats.h"
#include <cmath>
#include <stdexcept>
#include <boost/format.hpp>

void PCStats::AddSample(address_t PC, acc_count_t distance) {
  if (stats_.count(PC) == 0) {
    //stats_.insert(std::pair<address_t, PCStats::DistanceStats>(PC,PCStats::DistanceStats()));
    stats_[PC] = new DistanceStats();
  }
  stats_[PC]->AddSample(distance);
}

std::string PCStats::GetStatsString() const {
  std::string out("{");
  for (std::tr1::unordered_map<address_t, DistanceStats*>::const_iterator iter = stats_.begin();
       iter != stats_.end(); ++iter) {
    out += boost::str(boost::format("0x%lx:%s, ") % iter->first % iter->second->GetStatsString());
  }
  out += "}";
  return out;
}

PCStats::~PCStats() {
  for (std::tr1::unordered_map<address_t, DistanceStats*>::iterator iter = stats_.begin();
       iter != stats_.end(); ++iter) {
    delete iter->second;
  }
}

const double PCStats::DistanceStats::kDumpColdMissValue = pow(2, 63);
const double PCStats::DistanceStats::kDumpInvalMissValue = pow(2, 62);

PCStats::DistanceStats::DistanceStats() : total_distance_(0), sample_count_(0), cold_miss_count_(0),
        inval_miss_count_(0) {
      distance_histogram_.resize(kHistogramSize);
      bucket_avg_.resize(kHistogramSize);
}

std::string PCStats::DistanceStats::GetStatsString() const {
  std::string out = boost::str(boost::format("(%d,%u,") % total_distance_ % sample_count_);
  out += "{";  // begin histo data dict
  if (distance_histogram_[0]) out += str(boost::format("0:(%u,0.0),") % distance_histogram_[0]);
  for (unsigned int i = 1; i < distance_histogram_.size(); i++){
    if (distance_histogram_[i]) {
      out += str(boost::format("%f:(%u,%f),")
          % pow(2, (i-1) / static_cast<double>(kHistogramDensity))
          % distance_histogram_[i]
          % bucket_avg_[i]);
    }
  }
  out += str(boost::format("%f:%u,") % kDumpInvalMissValue % inval_miss_count_);
  out += str(boost::format("%f:%u") % kDumpColdMissValue % cold_miss_count_);
  // could make the inf dist bucket match the format of others but its actually different usage
  // so will leave it as an int rather than a tuple for now, until/unless that creates problems
  //out += str(boost::format("%f:(%u,%f)") % kDumpInfDistValue % cold_miss_count_ % kDumpInfDistValue);
  out += "})";  // end histo data dict
  return out;
}

void PCStats::DistanceStats::AddSample(acc_count_t distance) {
  if (sample_count_++ >= kAccessCountMax) throw std::overflow_error("Per-PC sample count overflow");
  if (distance < kAccessCountMax) {
    total_distance_ += distance;
    int bucket;
    if (distance == 0) bucket = 0;
    else {
      bucket = static_cast<int>((log2(distance) * (double) kHistogramDensity) + 1);
    }
    while(static_cast<unsigned>(bucket) >= distance_histogram_.size()) {
      distance_histogram_.push_back(0);
      bucket_avg_.push_back(0.0f);
    }
    if (distance_histogram_[bucket] >= kAccessCountMax) {
      throw std::overflow_error("Per-PC distance histogram bucket overflow");
    }
    int64_t bucket_total_dist = static_cast<int64_t>(distance_histogram_[bucket]
                                                     * bucket_avg_[bucket]);
    distance_histogram_[bucket]++;
    bucket_avg_[bucket] = (bucket_total_dist + distance) / distance_histogram_[bucket];
  } else {
    if (distance == kColdMiss) {
      cold_miss_count_++;
    } else if (distance == kInvalidationMiss) {
      inval_miss_count_++;
    } else {
      throw std::invalid_argument("Bad distance value");
    }
  }
}

const int ReuseStackStats::kHistogramDensity;
const int ReuseStackStats::kHistogramSize;
const double ReuseStackStats::kDumpColdMissValue = pow(2, 63);
const double ReuseStackStats::kDumpInvalMissValue = pow(2, 62);

ReuseStackStats::ReuseStackStats(int block_size)
    : kBlockSize(block_size), sample_count_(0), cold_miss_count_(0), inval_miss_count_(0),
      total_distance_(0), current_prediction_accesses_(0), total_prediction_accesses_(0),
      total_prediction_hits_(0)
{
    distance_histogram_.resize(kHistogramSize, 0);
    target_hit_rates_.push_back(0.5);
    target_hit_rates_.push_back(0.9);
    target_hit_rates_.push_back(0.95);
    target_hit_rates_.push_back(0.99);
}

void ReuseStackStats::AddSample(address_t address, acc_count_t distance) {
  if (sample_count_++ >= kAccessCountMax) throw std::overflow_error("Sample count overflow");
  if (distance < kAccessCountMax) {
    total_distance_ += distance;
    int bucket;
    if (distance == 0) bucket = 0;
    else {
      bucket = static_cast<int>((log2(distance) * (double) kHistogramDensity) + 1);
    }
    while(static_cast<unsigned>(bucket) >= distance_histogram_.size()) {
      distance_histogram_.push_back(0);
    }
    if (distance_histogram_[bucket] >= kAccessCountMax) {
      throw std::overflow_error(str(boost::format("%s %d") %
                                    "Distance histogram bucket overflow, bucket " % bucket));
    }
    distance_histogram_[bucket]++;
  } else {
    if (distance == kColdMiss) {
      cold_miss_count_++;
    } else if (distance == kInvalidationMiss) {
      inval_miss_count_++;
    } else {
      throw std::invalid_argument("Bad distance value");
    }
  }

  //update distance predictions
  current_prediction_accesses_++;
  total_prediction_accesses_++;
  if (distance != kStackNotFound) {
    for (std::map<acc_count_t, acc_count_t>::iterator iter = current_prediction_hits_.begin(); iter
        != current_prediction_hits_.end(); ++iter) {
      if ((distance * kBlockSize) < iter->first) {
        iter->second++;
        total_prediction_hits_++;
      }
    }
  }
}

void ReuseStackStats::SetRatioPredictionSizes(const std::vector<int> &sizes) {
  current_prediction_hits_.clear();
  for (unsigned int i = 0; i < sizes.size(); i++){
    current_prediction_hits_[sizes[i]] = 0;
  }
}

std::vector<int> ReuseStackStats::GetRatioPredictionSizes() const {
    std::vector<int> sizes(current_prediction_hits_.size());
    for(std::map<acc_count_t, acc_count_t>::const_iterator iter = current_prediction_hits_.begin();
            iter != current_prediction_hits_.end(); ++iter) {
        sizes.push_back(iter->first);
    }
    return sizes;
}

void ReuseStackStats::AddRatioPredictionSize(int size) {
  if (current_prediction_hits_.count(size) == 0) {
    current_prediction_hits_[size] = 0;
    ratio_predictions_[size].clear();
  }
}

void ReuseStackStats::ResetRatioPredictions() {
    current_prediction_accesses_ = 0;
    for(std::map<acc_count_t, acc_count_t>::iterator iter = current_prediction_hits_.begin();
            iter != current_prediction_hits_.end(); ++iter) {
        iter->second = 0;
    }
}

void ReuseStackStats::UpdateRatioPredictions() {
    for(std::map<acc_count_t, acc_count_t>::iterator iter = current_prediction_hits_.begin();
            iter != current_prediction_hits_.end(); ++iter) {
        int size = iter->first;
        ratio_predictions_[size].push_back(iter->second);
        iter->second = 0;
    }
    prediction_accesses_.push_back(current_prediction_accesses_);
    current_prediction_accesses_ = 0;
}

std::vector<ReuseStackStats::HistogramEntry> ReuseStackStats::GetHistogram() const {
  std::vector<HistogramEntry> histogram;
  if (distance_histogram_[0]) histogram.push_back(HistogramEntry(0.0, distance_histogram_[0]));
  for (unsigned int i = 1; i < distance_histogram_.size(); i++){
    if (distance_histogram_[i]) {
      histogram.push_back(HistogramEntry(pow(2, (i-1) / static_cast<double>(kHistogramDensity)),
                                         distance_histogram_[i]));
    }
  }
  histogram.push_back(HistogramEntry(kDumpInvalMissValue, inval_miss_count_));
  histogram.push_back(HistogramEntry(kDumpColdMissValue, cold_miss_count_));
  return histogram;
}

std::string ReuseStackStats::GetHistogramString() const {
  std::string out = "{";  // begin histo data dict
  if (distance_histogram_[0]) out += str(boost::format("%u:%u, ") % 0 % distance_histogram_[0]);
  for (unsigned int i = 1; i < distance_histogram_.size(); i++){
    if (distance_histogram_[i]) {
      out += str(boost::format("%lf:%u, ")
          % pow(2, (i-1) / static_cast<double>(kHistogramDensity))
          % distance_histogram_[i]);
    }
  }
  out += str(boost::format("%lf:%u, ") % kDumpInvalMissValue % inval_miss_count_);
  out += str(boost::format("%lf:%u") % kDumpColdMissValue % cold_miss_count_);
  out += "}";  // end histo data dict
  return out;
}

acc_count_t ReuseStackStats::GetTargetSize(double target_hit_rate) {
  if (target_hit_rate >= 1.0) throw std::invalid_argument("target hit rate must be < 1.0");
  acc_count_t cumulative_hitcount = 0;
  acc_count_t target_hits =
      static_cast<acc_count_t>((sample_count_ - cold_miss_count_ - inval_miss_count_)
      * target_hit_rate);
  cumulative_hitcount += distance_histogram_[0];
  if (cumulative_hitcount >= target_hits) return 1;
  for (unsigned int i = 1; i < distance_histogram_.size(); i++) {
    cumulative_hitcount += distance_histogram_[i];
    if (cumulative_hitcount >= target_hits) {
      return static_cast<acc_count_t>(
        pow(2, (i-1)/static_cast<double>(kHistogramDensity)));
    }
  }
  throw std::runtime_error("got to end of histogram without finding target size");
}

std::string ReuseStackStats::GetAttributes() const {
  acc_count_t cumulative_hitcount = 0;
  // convert hit rate to hit count
  std::vector<acc_count_t> target_hits(target_hit_rates_.size(), 0);
  // size required for target rate
  std::vector<acc_count_t> target_sizes(target_hit_rates_.size(), 0);

  for (unsigned int j = 0; j < target_hit_rates_.size();j++) {
    target_hits[j] = static_cast<acc_count_t>(sample_count_ * target_hit_rates_[j]);
  }
  cumulative_hitcount += distance_histogram_[0];
  for (unsigned int j = 0;j < target_hit_rates_.size(); j++) {
    if(target_sizes[j] == 0 && cumulative_hitcount >= target_hits[j]) target_sizes[j] = 1;
  }
  for (unsigned int i = 1; i < distance_histogram_.size(); i++) {
    cumulative_hitcount += distance_histogram_[i];
    for (unsigned int j = 0;j < target_hit_rates_.size(); j++) {
      if (target_sizes[j] == 0 && cumulative_hitcount >= target_hits[j]) {
        target_sizes[j] = static_cast<acc_count_t>(
            pow(2, (i-1)/static_cast<double>(kHistogramDensity)));
      }
    }
  }

  //begin attributes
  std::string out = str(boost::format("'sampleCount':%d, ") % sample_count_);
  out += str(boost::format("'totalDist':%d, ") % total_distance_);
  out += str(boost::format("'avgDist':%.2f, ") %
             (static_cast<double>(total_distance_) /
             (sample_count_ - cold_miss_count_ - inval_miss_count_)));
  out += str(boost::format("'coldStatCount':%d, ") % cold_miss_count_);
  out += str(boost::format("'invalStatCount':%d, ") % inval_miss_count_);
  out += str(boost::format("'medianDist':%d, ") % target_sizes[0]);
  out += str(boost::format("'totalPredictionAccesses':%d, ") % total_prediction_accesses_);
  out += str(boost::format("'totalPredictionHits':%d, ") % total_prediction_hits_);

  for (unsigned int j = 0; j < target_hit_rates_.size(); j++) {
    out += str(boost::format("'hit%dpct':%d, ") % static_cast<int>(target_hit_rates_[j]*100)
        % target_sizes[j]);
  }
  // don't terminate attribute dict, will be terminated by caller
  return out;
}

std::string ReuseStackStats::GetPredictions() const {
  std::string out = "{ ";
  for (std::map<acc_count_t, std::vector<acc_count_t> >::const_iterator iter
       = ratio_predictions_.begin();
       iter != ratio_predictions_.end(); ++iter) { //for each prediction size
    out += str(boost::format("%d: [") % iter->first);  // map size->list of predictions
    for (unsigned int i = 0; i < iter->second.size(); i++) {
      out += str(boost::format("%u, ") % iter->second[i]);
    }
    out += "], ";
  }
  out += "'accesses': [";
  for (unsigned int i = 0; i < prediction_accesses_.size(); i++) {
    out += str(boost::format("%u, ") % prediction_accesses_[i]);
  }
  out += "]}\n";
  return out;
}
