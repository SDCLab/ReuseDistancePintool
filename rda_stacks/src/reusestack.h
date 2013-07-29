/*
 * File:   ReuseStack.h
 * Author: dschuff
 *
 * Created on October 23, 2008, 5:39 PM
 */

#ifndef _REUSESTACK_H
#define	_REUSESTACK_H

#include <climits>
#include <cstdio>
#include <list>
#include <map>
#include <tr1/unordered_map>
#include <vector>
#include <boost/scoped_ptr.hpp>
#include "reusestack-common.h"
#include "reusestackstats.h"
#include "treereusestack.h"
#include "approximatereusestack.h"

static const address_t kMaxAddress = std::numeric_limits<int64_t>::max();

/**
*   Base class for reuse distance stacks.
*   Implements a "null" stack that can be instantiated and does nothing
*   (while still producing valid-ish output)
*/
class ReuseStackBase {
public:
  enum AccessType {
    kRead,
    kWrite,
    kFetch,
  };
  ReuseStackBase(FILE * outf, int granularity){outfile = outf;}
  virtual void setOutfile(FILE * outf, int granularity){outfile = outf;}
  virtual acc_count_t Access(address_t addr, int size, AccessType type) {return 0;}
  virtual acc_count_t Prefetch(address_t addr) {return 0;}
  virtual void Snoop(address_t addr, int size) {}

  virtual int GetGranularity(){return 0;}
  virtual void DumpStatistics() const {fprintf(outfile, "[{},{},{}]\n");}
  virtual void SetRatioPredictionSizes(std::vector<int> &sizes){}
  virtual std::vector<int> GetRatioPredictionSizes() {std::vector<int> sizes; return sizes;}
  virtual void AddRatioPredictionSize(int size) {}
  virtual void UpdateRatioPredictions() {}
  virtual void ResetRatioPredictions() {}
  virtual ~ReuseStackBase() {}
private:
  FILE *outfile;
  DISALLOW_COPY_AND_ASSIGN(ReuseStackBase);
};


/**
*   Abstract base class for usable reuse distance stacks. Handles all the stats and histograms,
*   and calls into the derived class to get the reuse distance of each reference.
*/
class ReuseStack : public ReuseStackBase {
public:
  enum StackImplementationTypes {
    kTreeStack,
    kApproximateStack,
  };
  ReuseStack(FILE * outfile, int granularity,
             StackImplementationTypes stack_type);
  virtual ~ReuseStack() {}
  //void setOutfile(FILE * outf, int granularity=DEFAULT_GRANULARITY);
  acc_count_t Access(address_t addr, int size, AccessType type);
  acc_count_t Prefetch(address_t addr);
  void Snoop(address_t addr, int size);
  int GetGranularity() { return blockBytes;}
  void DumpStatistics() const;

  void SetRatioPredictionSizes(std::vector<int> &sizes);
  std::vector<int> GetRatioPredictionSizes();
  void AddRatioPredictionSize(int size);
  void UpdateRatioPredictions();
  void ResetRatioPredictions();

protected:
  //virtual acc_count_t SnoopInvalidate(address_t addr, int size) = 0;
  //virtual acc_count_t StackAccess(address_t addr, int size) = 0;
  void setBlockBytes(int granularity);
  //int getBlockBytes() { return blockBytes;}
private:
  typedef std::tr1::unordered_map<address_t, int> AddressCount;
  ReuseStackImplInterface *GetStackImpl(FILE * outfile, int granularity);

  int blockBytes; ///< Bytes per tracked block (aka the tracking granularity)
  const StackImplementationTypes kStackType;
  address_t maxAddr;

  acc_count_t accessCount;
  acc_count_t blockAccessCount;
  acc_count_t invalCount;
  acc_count_t coldCount;
  acc_count_t invalidateCalls;
  acc_count_t coherenceMisses;
  acc_count_t writeCount;
  acc_count_t fetchCount;

  acc_count_t prefetchCount;
  acc_count_t prefetchCoherenceMisses;
  acc_count_t prefetchColdCount;

  /// addresses invalidated by snoops (to differentiate cold from coherence misses)
  AddressCount invalidatedAddrs;

  acc_count_t lastCoherence, lastCold;

  FILE * outfile;
  acc_count_t totalSize;

  boost::scoped_ptr<ReuseStackImplInterface> stackImpl;
  ReuseStackStats stats_;
  ReuseStackStats read_stats_;
  ReuseStackStats write_stats_;
  ReuseStackStats fetch_stats_;
  ReuseStackStats prefetch_stats_;
  DISALLOW_COPY_AND_ASSIGN(ReuseStack);
};

typedef struct PACKED {
  address_t address;
  //uint8_t cpu;
  uint8_t is_write;
  uint8_t size;
} BufferedRef;

#define PAR_REF_READ 0
#define PAR_REF_WRITE 1
#define PAR_REF_INVAL 2

#endif	/* _REUSESTACK_H */

