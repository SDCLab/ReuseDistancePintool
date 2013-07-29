/*
 * File:   TreeReuseStack.h
 * Author: dschuff
 *
 * Created on October 26, 2008, 3:28 PM
 */

#ifndef _TREEREUSESTACK_H
#define	_TREEREUSESTACK_H

#include <stdint.h>
#include <set>
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#include <vector>
#include "reusestack-common.h"

#define PACKED __attribute__ ((__packed__))

#define MAX_STACKS 4
#define PERF

typedef struct PACKED _tree_node {
    address_t addr;
    struct _tree_node *lft, *rt;
    acc_count_t inum;
    int rtwt;
} tree_node;

class TreeReuseStack : public ReuseStackImplInterface {
public:
  TreeReuseStack(FILE * outfile, int granularity);
  virtual ~TreeReuseStack();

//Main API functions as of now
  virtual acc_count_t SnoopInvalidate(address_t addr);
  virtual acc_count_t StackAccess(address_t addr);
  virtual acc_count_t GetStackSize() { return stackSize;}

  void ReplaceTree(const TreeReuseStack &other);
  bool CheckDuplicateNodes(const TreeReuseStack &other) const;
  void Print(void (*callback)(int, address_t)){treeCheck(root); print_tree(root,0,0, callback);}
  bool IsInCache(address_t block);
  void SetCapacityActions(void * obj, int blockCapacity, int index,
                          void (*capacityCallback)(void *, address_t, int, int));

  acc_count_t getTotAddrs() { return tot_addrs;}

  acc_count_t GetDepth(address_t addr);

private:
  typedef std::tr1::unordered_map<address_t, acc_count_t> AddressCount;
  typedef std::tr1::unordered_set<address_t> AddressSet;
  acc_count_t HashLookup(address_t addr);
  acc_count_t RefTree(acc_count_t i_inum, address_t addr);
  acc_count_t GetDepthAndNode(acc_count_t i_inum, tree_node **node) const;
  void InsertNewNode(address_t addr, tree_node *nnode);
  /* splay the input entry to the top of the stack */
  void splay(int at);
  void rotate_left(int y);
  void rotate_right(int y);
  tree_node * delete_oldest_node(void);
  tree_node * delete_inum(acc_count_t i_inum, address_t addr);

  tree_node *root;		/* Root of splay tree */
  std::vector<tree_node *> p_stack; /* Stack used for tree operations */
  AddressCount last_access;
  //unsigned int tot_addrs;	/* Count of distinct addresses */
  acc_count_t reference_count_;	/* Count of addresses processed */

  static const address_t kHoleAddress = static_cast<address_t>(-1);
  //std::vector<acc_count_t> holeHeap;
//    struct heapCompare : public std::binary_function<tree_node *&, tree_node *&, bool> {
//        bool operator()(tree_node*& lhs, tree_node*& rhs) const {
//            return lhs->inum < rhs->inum;
//        }
//    };
//    heapCompare heapComp;
  acc_count_t DoHoleAccess(acc_count_t inum, address_t addr, tree_node **ret_node);
  std::set<acc_count_t> hole_set_;

  int blockBytes;
  acc_count_t tot_addrs; ///< total unique addresses ever seen
  int32_t stackSize; ///< current total size of the stack
  int getBlockBytes() { return blockBytes;}

  int32_t blockCapacity;
  int bcIndex;
  void *bcObject;
  void (*capacityCallback)(void *, address_t, int, int);
  AddressSet blocksWithinCapacity;

  bool dupNodeCheck(const tree_node *thingOne, const tree_node *thingTwo) const;

  //stuff for node deletion
  std::vector<tree_node *> delete_stack;
  int delete_top;
  short delete_first_time;
  acc_count_t delete_last_inum;
  int treeCheck(tree_node *ptr);
  void print_tree(tree_node *n, int depth, int dist, void (*callback)(int, address_t));
  void freeTree(tree_node *ptr);
  tree_node *copyTree(tree_node *root);
#ifdef PERF
  acc_count_t deleteInumCalls;
  acc_count_t treeRefCalls;
  uint64_t totalDepth;
  uint64_t no_splay_steps;
  acc_count_t hole_jumps_;
#endif
  DISALLOW_COPY_AND_ASSIGN(TreeReuseStack);
};

#endif	/* _TREEREUSESTACK_H */

