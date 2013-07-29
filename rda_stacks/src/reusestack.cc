
#include "reusestack.h"


ReuseStack::ReuseStack(FILE *outf, int granularity, StackImplementationTypes stack_type)
    : ReuseStackBase(outf, granularity),
      blockBytes(granularity), kStackType(stack_type),
      maxAddr(kMaxAddress - blockBytes),
      accessCount(0), blockAccessCount(0),
      invalCount(0), coldCount(0), invalidateCalls(0), coherenceMisses(0), /*doCheckRace(false),*/
      writeCount(0), prefetchCount(0), prefetchCoherenceMisses(0), prefetchColdCount(0),
      outfile(outf), totalSize(0), stats_(blockBytes), read_stats_(blockBytes), 
      write_stats_(blockBytes), fetch_stats_(blockBytes), prefetch_stats_(blockBytes)
{
    stackImpl.reset(GetStackImpl(outf, blockBytes));
}

//void ReuseStack::setBlockBytes(int granularity)
//{
//    blockBytes = granularity;
//    maxAddr = kMaxAddress - blockBytes;
//}

//void ReuseStack::setOutfile(FILE *outf, int granularity)
//{
//    outfile = outf;
//    setBlockBytes(granularity);
//}

acc_count_t ReuseStack::Access(address_t address, int size, AccessType type)
{
  accessCount++;
  address_t block, addr = address;
  totalSize += size;
  if (size < 1 || size > 8) {
    //printf("Access size %d\n", size);
  }
  acc_count_t dist;
  do {
    blockAccessCount++;
    writeCount += (type == kWrite);
    fetchCount += (type == kFetch);
    block = addr / blockBytes;

    try {
      dist = stackImpl->StackAccess(block);
    } catch (std::bad_alloc exc) {
      printf("failed allocation in stackAccess: stackSize %"PRIacc" what:%s\n", stackImpl->GetStackSize(),
             exc.what());
      throw exc;
    }

    if (dist == kStackNotFound) {
      // for now, keep track of invalidations here and not in the stats module
      if (invalidatedAddrs.count(block) > 0) {
        coherenceMisses++;
        invalidatedAddrs.erase(block);
        dist = kInvalidationMiss;
      } else {
        coldCount++;
        dist = kColdMiss;
      }
    } else {
      //moved to stats
    }
    stats_.AddSample(block, dist);
    switch(type) {
    case kRead:
      read_stats_.AddSample(block, dist);
      break;
    case kWrite:
      //write_stats_.AddSample(block, dist);
      break;
    case kFetch:
      //fetch_stats_.AddSample(block, dist);
      break;
    }

    if (addr > maxAddr) break;
    addr += blockBytes;
  } while (address + size > addr);
  return dist;
}

acc_count_t ReuseStack::Prefetch(address_t address)
{
  prefetchCount++;
  address_t block = address / blockBytes;
  acc_count_t dist;

  try {
    dist = stackImpl->StackAccess(block);
  } catch (std::bad_alloc exc) {
    printf("failed allocation in stackAccess: stackSize %"PRIacc" what:%s\n", stackImpl->GetStackSize(),
           exc.what());
    throw exc;
  }

  if (dist == kStackNotFound) {
    // for now, keep track of invalidations here and not in the stats module
    if (invalidatedAddrs.count(block) > 0) {
      prefetchCoherenceMisses++;
      invalidatedAddrs.erase(block);
      dist = kInvalidationMiss;
    } else {
      prefetchColdCount++;
      dist = kColdMiss;
    }
  } else {
    //moved to stats
  }
  prefetch_stats_.AddSample(block, dist);

  return dist;
}

void ReuseStack::Snoop(address_t addr, int size)
{//FIXME: deal with size here
    address_t block = addr/blockBytes;
    try {
        invalidateCalls++;
        if(stackImpl->SnoopInvalidate(block) != kStackNotFound) {
            invalCount++;
            if(true) invalidatedAddrs[block] += 1;
        }
    } catch (std::bad_alloc exc) {
        printf("failed allocation in snoopInvalidate: stackSize %"PRIacc" what:%s\n",
               stackImpl->GetStackSize(), exc.what());
        throw exc;
    }
}


void ReuseStack::DumpStatistics() const
{
  fprintf(outfile, "{");  // begin rddata dict
  fprintf(outfile, "'histogram':%s,", stats_.GetHistogramString().c_str());  // print histogram dict
  // begin attribute dict
  fprintf(outfile, "'attributes':{'accessCount':%"PRIacc", 'blockAccessCount':%"PRIacc", 'avgSize':%.2f, "
          "'coldCount':%"PRIacc", 'invalCount': %"PRIacc", 'stackSize': %"PRIacc", "
          "'coherenceMisses':%"PRIacc", 'writeCount':%"PRIacc", "
	  "'fetchCount':%"PRIacc", 'invalidateCalls':%"PRIacc", ",
          accessCount, blockAccessCount, (float)totalSize / accessCount,
          coldCount, invalCount, stackImpl->GetStackSize(),
          coherenceMisses, writeCount, fetchCount, invalidateCalls);
  fprintf(outfile, "'prefetchCount':%"PRIacc", 'prefetchColdCount':%"PRIacc", 'prefetchCoherenceMisses':%"
	  PRIacc", ", prefetchCount, prefetchColdCount, prefetchCoherenceMisses);
  fprintf(outfile, "%s", stats_.GetAttributes().c_str());  // print stats attributes
  fprintf(outfile, "},");  // end attribute dict
  //read/write/fetch histos go here
  fprintf(outfile, "'read_histo':{'histogram':%s, 'attributes':{%s}}, ",
	  read_stats_.GetHistogramString().c_str(), read_stats_.GetAttributes().c_str());
  fprintf(outfile, "'write_histo':{'histogram':%s, 'attributes':{%s}}, ",
	  write_stats_.GetHistogramString().c_str(), write_stats_.GetAttributes().c_str());
  fprintf(outfile, "'fetch_histo':{'histogram':%s, 'attributes':{%s}}, ",
	  fetch_stats_.GetHistogramString().c_str(), fetch_stats_.GetAttributes().c_str());
  fprintf(outfile, "'prefetch_histo':{'histogram':%s, 'attributes':{%s}}, ",
    prefetch_stats_.GetHistogramString().c_str(), prefetch_stats_.GetAttributes().c_str());
  fprintf(outfile, "}\n"); // end rddata dict
  fprintf(outfile, "#preds %s", stats_.GetPredictions().c_str());
}

void ReuseStack::SetRatioPredictionSizes(std::vector<int> &sizes)
{
  stats_.SetRatioPredictionSizes(sizes);
  lastCoherence = lastCold = 0;
}

std::vector<int> ReuseStack::GetRatioPredictionSizes()
{
  return stats_.GetRatioPredictionSizes();
}

void ReuseStack::AddRatioPredictionSize(int size)
{
  stats_.AddRatioPredictionSize(size);
}

void ReuseStack::ResetRatioPredictions()
{
  stats_.ResetRatioPredictions();
}

void ReuseStack::UpdateRatioPredictions()
{
  stats_.UpdateRatioPredictions();
  if (coherenceMisses == lastCoherence && coldCount == lastCold){
    //printf("no misses %d\n", period);
  }
  lastCoherence =  coherenceMisses;
  lastCold = coldCount;
}

ReuseStackImplInterface *ReuseStack::GetStackImpl(FILE *outf, int granularity) {
  switch(kStackType) {
    case kTreeStack:
      return new TreeReuseStack(outf, granularity);
    case kApproximateStack:
      return new approximateReuseStack(outf, granularity);
    default:
      return NULL;
  }
}

