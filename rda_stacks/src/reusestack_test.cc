/*
 * reuseStack_test.cc
 *
 *  Created on: Aug 25, 2009
 *      Author: dschuff
 */

#include <gtest/gtest.h>
#include "treereusestack.h"
#include "reusestack.h"

class TreeReuseStackTest: public testing::Test
{
protected:
  static const int kAccessSequenceLength = 5;
  TreeReuseStackTest() {
    tree_ = new TreeReuseStack(NULL, 8);
  }
  virtual ~TreeReuseStackTest() {
    delete tree_;
  }

  // Access the stack 'count' times sequentially starting from 'initial count' and expect that
  // StackAccess returns 'ret' each time
  void AccessSequentially(int count, int initial_count, acc_count_t ret) {
    acc_count_t access_address = initial_count + 1;
    for (int i = 0; i < count; i++) {
      SCOPED_TRACE("Access iter " + i);
      EXPECT_EQ(ret, tree_->StackAccess(access_address));
      access_address++;
    }
  }
  // Access each of the values in the 'addresses' and expect the corresponding value in
  // 'expected_depths'. Both arrays must have at least 'count' elements
  void AccessList(int count, int addresses[], acc_count_t expected_depths[]) {
    SCOPED_TRACE("AccessList, address = ");
    for (int i = 0; i < count; i++) {
      SCOPED_TRACE(addresses[i]);
      EXPECT_EQ(expected_depths[i], tree_->StackAccess(addresses[i]));
    }
  }
  // Verify that each address in 'addresses' has the corresponding depth specified in 'depths'
  void ExpectList(int count, int addresses[], int depths[]) {
    SCOPED_TRACE("ExpectList, address = ");
    for (int i = 0; i < count; i++) {
      SCOPED_TRACE(addresses[i]);
      EXPECT_EQ(depths[i], tree_->GetDepth(addresses[i]));
    }
  }

  TreeReuseStack *tree_;
};

const int TreeReuseStackTest::kAccessSequenceLength;

// Tests that repeated accesses have distance of 0
TEST_F(TreeReuseStackTest, RepeatedAccess) {
  EXPECT_EQ(kStackNotFound, tree_->StackAccess(0x1234));
  EXPECT_EQ(0, tree_->StackAccess(0x1234));
  EXPECT_EQ(1, tree_->getTotAddrs());
  EXPECT_EQ(1, tree_->GetStackSize());
}

// Tests that a sequence of new addresses always have infinite distance
TEST_F(TreeReuseStackTest, SequentialAccess) {
  AccessSequentially(kAccessSequenceLength, 0, kStackNotFound);
  EXPECT_EQ(kAccessSequenceLength, tree_->getTotAddrs());
  EXPECT_EQ(kAccessSequenceLength, tree_->GetStackSize());
}

// Tests that the sequence has inf distance on the first run and distance of length - 1 on the 2nd
TEST_F(TreeReuseStackTest, RepeatedSequence) {
  AccessSequentially(kAccessSequenceLength, 0, kStackNotFound);
  AccessSequentially(kAccessSequenceLength, 0, kAccessSequenceLength - 1);
  EXPECT_EQ(kAccessSequenceLength, tree_->getTotAddrs());
  EXPECT_EQ(kAccessSequenceLength, tree_->GetStackSize());
}


TEST_F(TreeReuseStackTest, InvalidationRedux) {
  // 5 sequential accesses, so the stack is 1|2|3|4|5
  AccessSequentially(5, 0, kStackNotFound);
  // invalidate addr 3, which should have inum 3
  // so stack is now 1|2|h|4|5
  EXPECT_EQ(3, tree_->SnoopInvalidate(3));
  // expect invalidated addr to be gone
  EXPECT_EQ(kStackNotFound, tree_->GetDepth(3));
  // expect depth of addr 1 (below hole) to be 4, just as if there were no inval (due to hole)
  // expect depth addr 4 (above hole) to be 1, same as if no inval
  int addresses[] = {5,4,2,1};
  int expected_depths[] = {0,1,3,4};
  ExpectList(4, addresses, expected_depths);

  // pull addr 2 to top of stack, its depth was 3
  // stack is now 1|h|4|5|2
  EXPECT_EQ(3, tree_->StackAccess(2));
  int addresses2[] = {1,4,5,2};
  int expected_depths2[] = {4,2,1,0};
  ExpectList(4, addresses2, expected_depths2);
}

TEST(ReuseStackTest, Prefetch) {
  FILE *outfile = fopen("reusestack-test-output", "r");
  ReuseStack stack(outfile, 1, ReuseStack::kTreeStack);
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(kStackNotFound, stack.Access(i, 1, ReuseStackBase::kRead));
  }
  EXPECT_EQ(100 - 4, stack.Prefetch(3));
  EXPECT_EQ(100 - 45, stack.Prefetch(45));
}
