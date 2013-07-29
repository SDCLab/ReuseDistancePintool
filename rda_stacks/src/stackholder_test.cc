/*
 * stackholder_test.cc
 *
 *  Created on: Aug 31, 2009
 *      Author: dschuff
 */

#include <stdio.h>
#include <string>
#include <gtest/gtest.h>
#include "stackholder.h"

//from ???
#define REF_READ 1
#define REF_WRITE 2
#define REF_RMW 4
#define REF_PREFETCH 8
#define REF_USER 16
#define REF_SYSTEM 32
#define REF_NONCPU 64
#define REF_CHUNKDEC 128
#define REF_READ_BITS 0
#define REF_WRITE_BITS 1
#define REF_RMW_BITS 2
#define REF_PREFETCH_BITS 3

//from memstat.h and ???
//these are used in software-simics iface and simics-trace processor iface
#define CHUNK_NONE -1
#define CHUNK_PRIVATE 0
#define CHUNK_STACK -2
//this one is only used in the interface between Simics and trace processor
// but as a result -3 should never be used by software-simics iface
#define CHUNK_TIMEDEC -3
#define TIMEDEC_START 1
#define TIMEDEC_STOP 2

typedef struct reference {
    uint64_t timestamp;
    uint64_t PC;
    uint64_t logical;
    //uint64 physical;
    uint16_t type;
    uint8_t size;
    uint8_t cpu;
    uint32_t misc;//chunksize for chunkdec, 0 for timedec
} reference_t;

class StackHolderTest : public testing::Test
{
protected:
  static const std::string kTestInputFileName;
  static const std::string kOutputFileName;
  static const std::string kReferenceOutputFileName;
  static const int kRatioPredictionSizes[3];
  virtual void SetUp();  // use SetUp so it can use EXPECT
  virtual ~StackHolderTest();
  void CompareOutput();

  StackHolder *stacks_;
};
const std::string StackHolderTest::kTestInputFileName("stackholder-testTrace");
const std::string StackHolderTest::kReferenceOutputFileName("stackholder-testRefOutput");
const std::string StackHolderTest::kOutputFileName("reuseStackTest.tmp");
const int StackHolderTest::kRatioPredictionSizes[3] = {262144, 524288, 1048576};

void StackHolderTest::SetUp() {

  try {
    stacks_ = new StackHolder(StackHolder::kDefaultGranularity, kOutputFileName);
  } catch (std::invalid_argument ex) {
    FAIL() << "StackHolder constructor threw exception";
  }
  stacks_->set_do_inval(true);
  stacks_->set_do_shared(true);
  stacks_->set_do_sim_stacks(true);
  stacks_->set_do_single_stacks(true);
  stacks_->set_do_lazy_stacks(true);
  stacks_->set_do_oracular_stacks(true);

  for (int i = 0; i < 3; i++) {
    // this is only setup for 4 threads
    stacks_->AddRatioPredictionSize(kRatioPredictionSizes[i]);
    stacks_->AddPairPredictionSize(kRatioPredictionSizes[i] * 2);
    stacks_->AddSharedPredictionSize(kRatioPredictionSizes[i] * 4);
  }
  for (int i = 1; i <= 4; i++) {
    stacks_->Allocate(i);
  }
}

StackHolderTest::~StackHolderTest() {
  delete stacks_;
  if (!::testing::Test::HasFailure()) unlink(kOutputFileName.c_str());
}

void StackHolderTest::CompareOutput() {
  FILE *reference;
  FILE *actual;
  actual = fopen(stacks_->GetStatsFileName().c_str(), "r");
  ASSERT_TRUE(actual != NULL) << "Could not open actual output file for comparison";
  reference = fopen(kReferenceOutputFileName.c_str(), "r");
  ASSERT_TRUE(reference != NULL) << "Could not open reference output " + kReferenceOutputFileName;
  while (!feof(actual) && !feof(reference)) {
    ASSERT_EQ(fgetc(reference), fgetc(actual)) << "Output files differ";
  }
  EXPECT_NE(0, feof(actual)) << "actual output still has bytes";
  EXPECT_NE(0, feof(reference)) << "reference output still has bytes";
  fclose(actual);
  fclose(reference);
}

// for now only use one test so we only read the test input once.
TEST_F(StackHolderTest, TheTest) {
  //return;
  reference_t ref;
  int64_t access_count = 0;
  int64_t bytes_read = 0;
  uint64_t region_timestamp = 0;
  int64_t region_accesses = 0;
  int region_count = 0;
  FILE *input_file;
  std::vector<uint64_t> regionTimes;
  std::vector<uint64_t> regionAccesses;

  input_file = fopen(kTestInputFileName.c_str(), "rb");
  ASSERT_TRUE(input_file != NULL) << "Could not open test input file " + kTestInputFileName;

  while (fread(&ref, sizeof(ref), 1, input_file) == 1) {
    if (access_count == 0) region_timestamp = ref.timestamp;
    if ((ref.type & REF_USER)) {  // ignore system references
      //access_count++;
      bytes_read += sizeof(ref);

      // don't count noncpu and page table accesses
      if (!(ref.type & REF_CHUNKDEC) && ref.misc == 1){
        //table->Access(*get_memop(&ref));
        //printf("memtrace Access %p ", ref.logical);
        access_count++;
        int thread = ref.cpu;
        stacks_->Access(thread, ref.logical, ref.size, (ref.type & REF_WRITE) != 0);
      }
      else if ((ref.type & REF_CHUNKDEC) &&  (ref.misc == (uint32_t)CHUNK_TIMEDEC)) {
        //access_count--;
        stacks_->EndParallelRegion();
        stacks_->UpdateRatioPredictions();
        if (ref.size == (uint8_t)TIMEDEC_STOP){
          printf("period %d time %lu accesses %ld\n", region_count,
                 ref.timestamp - region_timestamp, access_count - region_accesses);
          regionTimes.push_back(ref.timestamp - region_timestamp);
          regionAccesses.push_back(access_count - region_accesses);
          region_count++;
          region_timestamp = ref.timestamp;
          region_accesses = access_count;
        }
      }
    }
  }
  stacks_->EndParallelRegion();
  if(region_count != (int)regionTimes.size()){
    printf("regionCount %d regionTimes %d\n", region_count, (int) regionTimes.size());
  }

  stacks_->DumpStatsPython();
  fprintf(stacks_->GetStatsFileHandle(), "regionTimes = [");
  for (int i = 0; i < region_count; i++) {
    fprintf(stacks_->GetStatsFileHandle(), "%lu, ", regionTimes[i]);
  }
  fprintf(stacks_->GetStatsFileHandle(), "]\n");
  fprintf(stacks_->GetStatsFileHandle(), "regionAccesses = [");
  for (int i = 0; i < region_count; i++) {
    fprintf(stacks_->GetStatsFileHandle(), "%lu, ", regionAccesses[i]);
  }
  fprintf(stacks_->GetStatsFileHandle(), "]\n");
  fclose(input_file);

  stacks_->FlushStats();
  CompareOutput();
}
