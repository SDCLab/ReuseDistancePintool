#ifndef REUSE_STACK_COMMON_H
#define REUSE_STACK_COMMON_H

#include <limits>
#include <tr1/cinttypes>
#define DEFAULT_GRANULARITY 8
typedef uint64_t address_t;
#define PRIaddr PRIx64
static const address_t kAddressMax = std::numeric_limits<uint64_t>::max();

#if defined(ACCCOUNTSIZE) && ACCCOUNTSIZE == 32
// no longer the default.
#warning 32 bit acc_count_t size not tested since stats updates, not guaranteed to work
typedef uint32_t acc_count_t;
static const acc_count_t kAccCountTypeMax = std::numeric_limits<int32_t>::max();
#define PRIacc PRIu32
#else
typedef int64_t acc_count_t;
static const acc_count_t kAccCountTypeMax = std::numeric_limits<int64_t>::max();
#define PRIacc PRId64
#endif //ACCOUNT_64

// these 2 constants should stay the same for compatibility
static const acc_count_t kStackNotFound = kAccCountTypeMax;
static const acc_count_t kColdMiss = kStackNotFound;
// inval miss stats differentiation added later
static const acc_count_t kInvalidationMiss = kColdMiss - 1;
// kAccessCountMax is the largest legitimate access count value
//   = max for the type minus the number of constants used
static const acc_count_t kAccessCountMax = kAccCountTypeMax - 2;

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

class ReuseStackImplInterface {
public:
  virtual acc_count_t SnoopInvalidate(address_t addr) = 0;
  virtual acc_count_t StackAccess(address_t addr) = 0;
  virtual acc_count_t GetStackSize() = 0;
  virtual ~ReuseStackImplInterface()  {}
};

#endif
