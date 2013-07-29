/*
 * reusestackstats.h
 *
 *  Created on: Oct 26, 2009
 *      Author: dschuff
 */

#ifndef REUSESTACKSTATS_H_
#define REUSESTACKSTATS_H_

#include <map>
#include <string>
#include <vector>
#include <tr1/unordered_map>
#include "reusestack-common.h"

class PCStats {
public:
  class DistanceStats {
  public:
    DistanceStats();
    void AddSample(acc_count_t distance);
    std::string GetStatsString() const;
  private:
    static const int kHistogramDensity = 2; ///< number of histogram buckets per power of 2
    static const int kInitialHistogramBuckets = 8; ///< initial number of buckets (dynamically resized)
    static const int kHistogramSize = kInitialHistogramBuckets * kHistogramDensity; ///< total size of the histogram
    static const double kDumpColdMissValue; // should be same as RueseStackStats counterpart
    static const double kDumpInvalMissValue; // should be same as RueseStackStats counterpart
    int64_t total_distance_;
    acc_count_t sample_count_;
    acc_count_t cold_miss_count_;
    acc_count_t inval_miss_count_;
    std::vector<acc_count_t> distance_histogram_;
    std::vector<float> bucket_avg_;
  };
  void AddSample(address_t PC, acc_count_t distance);
  std::string GetStatsString() const;
  ~PCStats();
private:
  std::tr1::unordered_map<address_t, DistanceStats *> stats_;
};

class ReuseStackStats {
public:
  typedef std::pair<double, acc_count_t> HistogramEntry;
  ReuseStackStats(int block_size);
  void AddSample(address_t address, acc_count_t distance);

  void SetRatioPredictionSizes(const std::vector<int> &sizes);
  std::vector<int> GetRatioPredictionSizes() const;
  void AddRatioPredictionSize(int size);

  void UpdateRatioPredictions();
  void ResetRatioPredictions();

  acc_count_t GetTotalSamples() { return sample_count_; }
  acc_count_t GetColdSamples() { return cold_miss_count_;}
  acc_count_t GetInvalSamples() { return inval_miss_count_;}
  acc_count_t GetTotalDistance() { return total_distance_;}

  std::string GetHistogramString() const;  // Return histogram as a dictionary
  std::string GetAttributes() const;  // return only dictionary elements, not a full dict
  std::string GetPredictions() const;  // Return dictionary
  std::vector<HistogramEntry> GetHistogram() const;
  acc_count_t GetTargetSize(double target_hit_rate);

private:
  static const int kHistogramDensity = 10; ///< number of histogram buckets per power of 2
  static const int kInitialHistogramBuckets = 20; ///< initial number of buckets (dynamically resized)
  static const int kHistogramSize = kInitialHistogramBuckets * kHistogramDensity; ///< total size of the histogram
  static const double kDumpColdMissValue;
  static const double kDumpInvalMissValue;
  const int kBlockSize;
  acc_count_t sample_count_;
  std::vector<acc_count_t> distance_histogram_;
  int32_t cold_miss_count_;
  int32_t inval_miss_count_;
  int64_t total_distance_;
  acc_count_t current_prediction_accesses_;
  acc_count_t total_prediction_accesses_;
  acc_count_t total_prediction_hits_;
  std::map<acc_count_t, acc_count_t> current_prediction_hits_;
  std::map<acc_count_t, std::vector<acc_count_t> > ratio_predictions_;
  std::vector<acc_count_t> prediction_accesses_;
  std::vector<double> target_hit_rates_;
};

#endif /* REUSESTACKSTATS_H_ */
