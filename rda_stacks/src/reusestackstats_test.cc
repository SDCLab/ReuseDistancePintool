/*
 * reusestackstats_test.cc
 *
 *  Created on: Oct 26, 2009
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "reusestackstats.h"

using std::string;

class ReuseStackStatsTest : public testing::Test {
protected:
  static const int kDefaultBlockSize = 8;
  ReuseStackStatsTest() : stats_(kDefaultBlockSize) {};
  ReuseStackStats stats_;
};

void ParseIntAttribute(string attribute_dict, string attribute_name, int *value) {
  size_t index = attribute_dict.find(attribute_name);
  int ret;
  EXPECT_EQ(1, sscanf(attribute_dict.c_str() + attribute_name.length() + index, "':%d", &ret))
    << "Couldn't match attribute " + attribute_name + " at index " << index << " in str " <<
    attribute_dict.c_str();
  *value = ret;
}

void ParseFloatAttribute(string attribute_dict, string attribute_name, float *value) {
  size_t index = attribute_dict.find(attribute_name);
  float ret;
  EXPECT_EQ(1, sscanf(attribute_dict.c_str() + attribute_name.length() + index, "':%f", &ret))
    << "Couldn't match attribute " + attribute_name + " at index " << index << " in str " <<
    attribute_dict.c_str();
  *value = ret;
}

// Test that sample count, total dist, median dist and average dist are calculated correctly
TEST_F(ReuseStackStatsTest, BasicStats) {
  stats_.AddSample(0, 1);
  stats_.AddSample(0, 2);
  stats_.AddSample(0, 32);
  stats_.AddSample(0, 65);
  int value = 0;
  string attributes(stats_.GetAttributes());
  ParseIntAttribute(attributes, "sampleCount", &value);
  EXPECT_EQ(4, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "totalDist", &value);
  EXPECT_EQ(100, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "medianDist", &value);
  EXPECT_EQ(2, value) << "attributes " + attributes;
  float fvalue = 0.0;
  ParseFloatAttribute(attributes, "avgDist", &fvalue);
  EXPECT_FLOAT_EQ(25.0, fvalue) << "attributes " + attributes;
}

// Test that the same basic stats are correct when some samples are kStackNotFound
// (and therefore should not be included in the average)
TEST_F(ReuseStackStatsTest, NotfoundStats) {
  stats_.AddSample(0, kStackNotFound);
  stats_.AddSample(0, 1);
  stats_.AddSample(0, 2);
  stats_.AddSample(0, 32);
  stats_.AddSample(0, 65);
  stats_.AddSample(0, kStackNotFound);
  int value = 0;
  string attributes(stats_.GetAttributes());
  ParseIntAttribute(attributes, "sampleCount", &value);
  EXPECT_EQ(6, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "totalDist", &value);
  EXPECT_EQ(100, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "medianDist", &value);
  EXPECT_EQ(32, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "coldStatCount", &value);
  EXPECT_EQ(2, value) << "attributes " + attributes;
  float fvalue = 0.0;
  ParseFloatAttribute(attributes, "avgDist", &fvalue);
  EXPECT_FLOAT_EQ(25.0, fvalue) << "attributes " + attributes;
  printf("%s\n", stats_.GetHistogramString().c_str());
}

// Test that the stats are correct when some entries are 0s
TEST_F(ReuseStackStatsTest, ZeroStats) {
  stats_.AddSample(0, 1);
  stats_.AddSample(0, 0);
  stats_.AddSample(0, 32);
  stats_.AddSample(0, 65);
  int value = 0;
  string attributes(stats_.GetAttributes());
  ParseIntAttribute(attributes, "sampleCount", &value);
  EXPECT_EQ(4, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "totalDist", &value);
  EXPECT_EQ(98, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "medianDist", &value);
  EXPECT_EQ(1, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "coldStatCount", &value);
  EXPECT_EQ(0, value) << "attributes " + attributes;
  float fvalue = 0.0;
  ParseFloatAttribute(attributes, "avgDist", &fvalue);
  EXPECT_FLOAT_EQ(24.5, fvalue) << "attributes " + attributes;
  printf("%s\n", stats_.GetHistogramString().c_str());
}

// Test that the total distance reflected in the histogram is correct
TEST_F(ReuseStackStatsTest, HistogramSize) {
  stats_.AddSample(0, kStackNotFound);
  stats_.AddSample(0, 1);
  stats_.AddSample(0, 2);
  stats_.AddSample(0, 32);
  stats_.AddSample(0, 65);
  stats_.AddSample(0, 0);
  int value = 0;
  string attributes(stats_.GetAttributes());
  ParseIntAttribute(attributes, "sampleCount", &value);
  EXPECT_EQ(6, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "totalDist", &value);
  EXPECT_EQ(100, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "medianDist", &value);
  EXPECT_EQ(2, value) << "attributes " + attributes;
  ParseIntAttribute(attributes, "coldStatCount", &value);
  EXPECT_EQ(1, value) << "attributes " + attributes;
  float fvalue = 0.0;
  ParseFloatAttribute(attributes, "avgDist", &fvalue);
  EXPECT_FLOAT_EQ(20.0, fvalue) << "attributes " + attributes;
  std::vector<ReuseStackStats::HistogramEntry> histogram(stats_.GetHistogram());
  double total_dist = 0;
  for (unsigned int i = 0; i < histogram.size() - 1; i++) {
    total_dist += histogram[i].first * histogram[i].second;
  }
  EXPECT_FLOAT_EQ(99.0, total_dist);
  printf("%s\n", stats_.GetHistogramString().c_str());
}
