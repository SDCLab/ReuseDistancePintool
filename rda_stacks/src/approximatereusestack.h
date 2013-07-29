/*
 * File:   approximateReuseStack.h
 * Author: dschuff
 *
 * Created on December 12, 2008, 2:01 PM
 */

#ifndef _APPROXIMATEREUSESTACK_H
#define	_APPROXIMATEREUSESTACK_H

#include <set>
#include <stdint.h>
#include <tr1/unordered_map>
#include "reusestack-common.h"
#define ERROR_RATE 0.01

#define PERF


//typedef struct PACKED _atree_node {
class atree_node {
public:
  atree_node(acc_count_t tm, int wt, int cap, int sz, atree_node *lft, atree_node *rt,
             atree_node *prv)
      : left(lft), right(rt), prev(prv), time(tm), weight(wt), size(sz), capacity(cap) {/*addr=0;*/}
//      address_t addr;
  atree_node *left, *right, *prev;
  acc_count_t time;
  int weight;
  int size;
  int capacity;
};

class approximateReuseStack : public ReuseStackImplInterface {
public:
  approximateReuseStack(FILE * outfile=NULL, int granularity=DEFAULT_GRANULARITY);
//  TreeReuseStack(const TreeReuseStack &other);
  //void ReplaceTree(const TreeReuseStack &other);
  //bool CheckDuplicateNodes(TreeReuseStack *other);
  virtual ~approximateReuseStack();
  virtual acc_count_t SnoopInvalidate(address_t addr);
  virtual acc_count_t StackAccess(address_t addr);

  void print(void (*callback)(int, address_t)){doTreeCheck(); print_tree(root,0,0, callback);}

  acc_count_t getTotAddrs() { return tot_addrs;}
  virtual acc_count_t GetStackSize() { return stackSize;}

private:
  acc_count_t treeSearch(acc_count_t time, bool do_delete);
  void treeInsert(atree_node *newNode);
  void treeDelete(atree_node *node);
  void splay(acc_count_t key);
  void treeCompression(atree_node *n);
  acc_count_t hashLookup(address_t addr);
  void freeTree(atree_node *ptr);
  atree_node *findSuccessor(atree_node *n);
  int treeCheck(atree_node *ptr);
  void print_tree(atree_node *n, int depth, int dist, void (*callback)(int, address_t));
  void doTreeCheck();

  acc_count_t currentTime;	/* Count of addresses processed */
  atree_node *root;
  atree_node *newest;
  double errorRate;
  //const unsigned int size_limit;
  int treenodeCount;
  std::tr1::unordered_map<address_t, acc_count_t> last_access;
  std::set<acc_count_t> hole_set_;

  int blockBytes;
  acc_count_t tot_addrs; ///< total unique addresses ever seen
  int32_t stackSize; ///< current total size of the stack
  int getBlockBytes() { return blockBytes;}

#ifdef PERF
  acc_count_t tsdCalls;
  uint64_t totalDepth;
  uint64_t no_splay_steps;
#endif
};

#endif	/* _APPROXIMATEREUSESTACK_H */

