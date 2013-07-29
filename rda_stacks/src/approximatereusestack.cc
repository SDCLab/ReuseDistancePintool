#include <cmath>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include "approximatereusestack.h"

static inline int node_weight(atree_node *x){
    return x == NULL ? 0 : x->weight;
}

approximateReuseStack::approximateReuseStack(FILE * outfile, int granularity)
: currentTime(0), errorRate(ERROR_RATE), treenodeCount(0), /*size_limit(512*1024*1024), */
blockBytes(granularity), tot_addrs(0), stackSize(0) {
  root = NULL;// new atree_node(currentTime, 0, 0, 0, NULL, NULL, NULL);
  newest = root;
#ifdef PERF
  tsdCalls = totalDepth = no_splay_steps = 0;
#endif
    //setBlockBytes(granularity);
//    root->time = currentTime;
//    //root->addr = addr;
//    root->weight = 1;
//    root->capacity = 1;
//    root->size = 1;
//    root->left = root->right = root->prev = NULL;
}
int getCount(atree_node *ptr) {
  if (ptr == NULL) return 0;
  return 1 + getCount(ptr->left) + getCount(ptr->right);
}

void approximateReuseStack::doTreeCheck() {
  treeCheck(root);
  int tcount = getCount(root);
  assert(treenodeCount == tcount);
  int pcount = 0;
  atree_node *n = newest;
  while (n != NULL){
    pcount++;
    if (n->prev) assert(n->time > n->prev->time);
    n=n->prev;
  }
  assert(pcount == treenodeCount);
}

acc_count_t approximateReuseStack::StackAccess(address_t addr) {
  acc_count_t lastAccessTime;
  atree_node *newNode;
  acc_count_t distance = kStackNotFound;

  currentTime++;
  lastAccessTime = hashLookup(addr);
  acc_count_t hole_top = *hole_set_.begin();  // technically undefined if size() == 0?
  if (hole_set_.size() > 0 && hole_top > lastAccessTime) {
    hole_set_.erase(hole_top);
    if (lastAccessTime != 0) {
      distance = treeSearch(lastAccessTime, false);
      hole_set_.insert(lastAccessTime);
    }
    treeSearch(hole_top, true); // check the deleted node?
  }
  else if (lastAccessTime != 0){
    distance = treeSearch(lastAccessTime, true);
  }
  newNode = new atree_node(currentTime, 1, 1, 1, NULL, NULL, newest);
//   newNode->addr = addr;
  //printf("time %d inserting %p addr %p last %d\n", currentTime, newNode, addr, lastAccessTime);
  newest = newNode;
  treeInsert(newNode);
   if (treenodeCount >= 4 * log(root->weight)/log(1+errorRate) + 4){
       treeCompression(newNode);
       //doTreeCheck();
   }
  return distance;
}

acc_count_t approximateReuseStack::SnoopInvalidate(address_t addr) {
  if(last_access.count(addr) == 0) return kStackNotFound;
  acc_count_t time = last_access[addr];
  //treeSearch(time, false);
  hole_set_.insert(time);
  last_access.erase(addr);
  --stackSize;
  //doTreeCheck();
  return time;
}

approximateReuseStack::~approximateReuseStack() {
#ifdef PERF
    printf("treeSearchDelete calls %"PRIacc", avg depth %f, splay steps %lu\n", tsdCalls, (float)totalDepth / (float)tsdCalls, no_splay_steps);
    printf("treeNodeCount %d, %ld bytes\n", treenodeCount, treenodeCount * sizeof(atree_node));
#endif
    freeTree(newest);
}

acc_count_t approximateReuseStack::treeSearch(acc_count_t time, bool do_delete) {
  atree_node *node = root, *parent = NULL;
  acc_count_t distance = 0;
#ifdef PERF
  tsdCalls++;
#endif
  while (true) {
#ifdef PERF
  totalDepth++;
#endif
    node->weight -= static_cast<int>(do_delete);
    if (time < node->time && node->prev != NULL && time <= node->prev->time) {
      distance += node_weight(node->right);
      if (node->left == NULL) break;
      distance += node->size;
      parent = node;
      node = node->left;
    }
    else if (time > node->time) {
      if (node->right == NULL) break;
      parent = node;
      node = node->right;
    }
    else break;
  }
  distance += node_weight(node->right);//omitted by Ding paper

  if (do_delete) {
    double cap = distance * errorRate / (1 - errorRate);
    node->capacity = cap > 1.0 ? (int)cap : 1;
    node->size -= 1;

    if (node->size == 0) {
      //treeCheck(root);
      //printf("calling delete %p root %p time %d\n", node, root, root->time);
      //if(node == root)
          //printf("deleting root\n");
      treeDelete(node);
      //treeCheck(root);
    }
  }

  return distance;
}

/* Insert a new node into the tree. Updates left, right, and weight of the node
  (and rest of tree as necessary). Does not touch times or prev ptrs.
*/
void approximateReuseStack::treeInsert(atree_node *newNode) {
  newNode->left = newNode->right = NULL;
  if (root == NULL) {
    assert(treenodeCount == 0);
    treenodeCount = 1;
    root = newNode;
    root->weight = root->size;
    return;
  }
  splay(newNode->time);
  if (newNode->time < root->time) {
    newNode->right = root;
    newNode->left = root->left;
    newNode->weight = root->weight + newNode->size;
    if(root->left != NULL) root->weight -= root->left->size;
    root->left = NULL;
  } else if(newNode->time > root->time) {
    newNode->left = root;
    newNode->right = root->right;
    newNode->weight = root->weight + newNode->size;
    if(root->right != NULL) root->weight -= root->right->size;
    root->right = NULL;
  } else {
    throw std::invalid_argument("trying to insert node with identical time to existing node\n");
  }
  treenodeCount++;
  root = newNode;
}

void approximateReuseStack::treeDelete(atree_node *node) {
  atree_node *x, *succ = NULL;
  acc_count_t key = node->time;
  //int w = root->weight;
  splay(key);
  assert(root == node);
  if (root->left == NULL) {
    root = root->right;
  }
  else {
    x = root->right;
    root = root->left;
    splay(key);
    root->right = x;
    if(x != NULL) root->weight += x->weight;
  }
//    if(root) assert(root->weight == w);
  succ = findSuccessor(node);
  if(succ == NULL) {
    assert(newest == node);
    newest = node->prev;
  }
  else {
    assert(succ->prev == node);
    succ->prev = node->prev;
  }
  //if(NULL == root)
      //printf("?\n");
  //printf("deleting %p root %p, time %d\n", node, root, root->time);
  delete node;
  treenodeCount--;
}

atree_node * approximateReuseStack::findSuccessor(atree_node *n) {
  acc_count_t key = n->time;
  atree_node *p = root, *last = NULL;
  while (p) {
    if (key > p->time) p = p->right;
    else if (p->left == NULL) {
        return p;
        //break;
    }
    else if (key > p->left->time) {
        last = p;
        p = p->left;
    }
    else p = p->left;
  }
  return last;
}


void approximateReuseStack::splay(acc_count_t key) {
  atree_node *l, *r, *t, *y, header(0, 0, 0, 0, NULL, NULL, NULL);
  //atree_node headl(0, 0, 0, 0, NULL, NULL, NULL);
  //atree_node headr(0, 0, 0, 0, NULL, NULL, NULL);
  int l_weight, r_weight;
#ifdef PERF
  no_splay_steps++;
#endif
  l = r = &header;
  t = root;
  l_weight = r_weight = 0;
  //header->left = header->right = null;
  for (;;) {
    if (key < t->time) {
      if (t->left == NULL) break;
      if (key < t->left->time) {
        y = t->left;                            /* rotate right */
//             t->weight -= y->size;
//             if(y->left != NULL) t->weight -= y->left->weight;
//             y->weight += t->size;
//             if(t->right != NULL) y->weight += t->right->weight;
        t->left = y->right;
        y->right = t;
        t->weight = node_weight(t->left) + node_weight(t->right) + t->size;
        t = y;
        if (t->left == NULL) break;
      }
      r->left = t;                                 /* link right */
      //r->weight += t->weight;
      r = t;
      t = t->left;
      r_weight += node_weight(r->right) + r->size;
      //r->weight -= t->weight; /* break link */
    } else if (key > t->time) {
      if (t->right == NULL) break;
      if (key > t->right->time) {
        y = t->right;                            /* rotate left */
//                 t->weight -= y->size;
//                 if(y->right != NULL) t->weight -= y->right->weight;
//                 y->weight += t->size;
//                 if(t->left != NULL) y->weight += t->left->weight;
        t->right = y->left;
        y->left = t;
        t->weight = node_weight(t->left) + node_weight(t->right) + t->size;
        t = y;
        if (t->right == NULL) break;
      }
      l->right = t;                                /* link left */
      //l->weight += t->weight;
      l = t;
      t = t->right;
      l_weight += node_weight(l->left) + l->size;
      //l->weight -= t->weight;
    } else {
      break;
    }
  }
  l_weight += node_weight(t->left);  /* Now l_weight and r_weight are the sizes of */
  r_weight += node_weight(t->right); /* the left and right trees we just built.*/
  t->weight = l_weight + r_weight + t->size;

  l->right = r->left = NULL;

  /* The following two loops correct the size fields of the right path  */
  /* from the left child of the root and the right path from the left   */
  /* child of the root.                                                 */
  for (y = header.right; y != NULL; y = y->right) {
    y->weight = l_weight;
    l_weight -= y->size+node_weight(y->left);
  }
  assert(header.weight == 0);
  for (y = header.left; y != NULL; y = y->left) {
    y->weight = r_weight;
    r_weight -= y->size+node_weight(y->right);
  }
  assert(header.weight == 0);
  l->right = t->left;                                /* assemble */
  r->left = t->right;
  t->left = header.right;
  t->right = header.left;

  root = t;
  //treeCheck(root);
}

void approximateReuseStack::treeCompression(atree_node *n) {
  int distance = 0;
  atree_node *tmp;
  n->capacity = 1;
  std::vector<atree_node *> nodes;
  int pos, size;

  while (n->prev != NULL) {
    if (n->prev->size + n->size <= n->capacity) {
      //merge n->prev into n
      n->size += n->prev->size;
      tmp = n->prev;
      n->prev = n->prev->prev;
      delete tmp;
      treenodeCount--;
    }
    else {
      distance += n->size;
      n->left = NULL;
      n->right = NULL;
      nodes.push_back(n);
      n = n->prev;
      double cap = distance * errorRate / (1 - errorRate);
      n->capacity = cap > 1.0 ? (int)cap : 1;
    }
  }
  n->left = n->right = NULL;
  nodes.push_back(n);
  //build tree from list
  root = NULL;
//     t=t->prev;
//     while(t != NULL){
//         //t->left = NULL;
//         //t->right = NULL;
//         //treeInsert(t);
//         //nodes.push_back(t);
//         t = t->prev;
//     }
  //randomly permute the nodes to get a balanced (enough) tree
  size = nodes.size();
  assert(size == treenodeCount);
  //srandom(1);
  for (int i = 0; i < size; i++) {
    pos = random() % (size - i) + i;
    tmp = nodes[i];
    nodes[i] = nodes[pos];
    nodes[pos] = tmp;
  }
  atree_node *cur, *last = NULL;
  root = nodes.back();
  root->weight = root->size;
  nodes.pop_back();
  treenodeCount = 1;
  while (nodes.size() > 0) {
    //treeInsert(nodes.back());
    tmp = nodes.back();
    tmp->weight = tmp->size;
    cur = root;
    while (cur) {
      last = cur;
      cur->weight += tmp->size;
      if(tmp->time == cur->time)
        printf("error\n");
      if(tmp->time < cur->time) {
        cur = cur->left;
      }
      else {
        cur = cur->right;
      }
    }
    if (tmp->time < last->time) last->left = tmp;
    else last->right = tmp;
    nodes.pop_back();
    treenodeCount++;
  }
}

/*
 * Lookup 'addr' in the hash table. Adds 'addr' to the hash table if it is
 * not found.
 *
 * Input: Address to be looked up
 * Output: Previous arrival time of address if found, zero if not.
 * Side effects: Adds the address to the hash table if it is not found.
 * Updates the previous time of arrival of address.
 */
acc_count_t approximateReuseStack::hashLookup(address_t addr) {
  acc_count_t old_inum;		/* Scratch variables */

  if (last_access.count(addr) == 0) {
    ++tot_addrs;
    ++stackSize;
    try {
      last_access[addr] = currentTime;
    } catch (std::bad_alloc exc) {
      printf("failed allocation adding to hash: tot_addrs %"
             PRIacc " what:%s\n", tot_addrs, exc.what());
    }
    /*if ((last_access.size() * getBlockBytes()) % (5*1024*1024) == 0){
      printf("%luMB target program use\n", last_access.size()*getBlockBytes()/1024/1024);
      printf("last_access bucket %zu, tot_addrs %"PRIacc" stackSize %d\n",
             last_access.bucket_count(), tot_addrs, stackSize);
    }*/
    return 0;
  }
  else {
    old_inum = last_access[addr];
    last_access[addr] = currentTime;
    return old_inum;
  }
}

void approximateReuseStack::freeTree(atree_node *ptr)
{
  atree_node *t;
  while (ptr != NULL){
    t = ptr;
    ptr = ptr->prev;
    delete t;
  }
}

/*
    Perform a consistency check on the tree. returns weight of ptr. no side effects.
*/
int approximateReuseStack::treeCheck(atree_node *ptr) {
  int errors=0;
  int weight=0;
  if (ptr == NULL) return 0;
  if (ptr->left == ptr) {
    printf("Loop left at %p-> %p, %"PRIacc"\n", ptr, ptr->left, ptr->time);
    errors++;
  }
  else if (ptr->left) {
    if (ptr->left->time >= ptr->time) {
      printf("tree invariant error %p->%p, %"PRIacc",%"PRIacc" ",
             ptr, ptr->left, ptr->time, ptr->left->time);
      //printf("addrs %p, %p\n", (void *)ptr->addr, (void *)ptr->lft->addr);
    }
    weight += treeCheck(ptr->left);
  }
  if (ptr->right == ptr) {
    printf("Loop right at %p-> %p, %"PRIacc"\n", ptr, ptr->right, ptr->time);
    errors++;
  }
  else if (ptr->right) {
    if (ptr->right->time <= ptr->time) {
      printf("tree invariant error %p->%p, %"PRIacc",%"PRIacc"\n",
             ptr, ptr->right, ptr->time, ptr->right->time);
      //printf("addrs %p, %p\n", (void *)ptr->addr, (void *)ptr->rt->addr);
      errors++;
    }
    //if(ptr->rtwt != getweight(ptr->rt))
    //    printf("weight error %p->%p, %d,%d", ptr, ptr->lft, ptr->rtwt, ptr->rt->rtwt);
    weight += treeCheck(ptr->right);
  }
  if (!ptr->left && !ptr->right) {
    if (ptr->weight != ptr->size) {
      printf("weight error %p,%d\n", ptr, ptr->weight);
      errors++;
    }
  }
  if (ptr->weight != weight + ptr->size)
    printf("weight error %p,%d\n", ptr, ptr->weight);
  return weight + ptr->size;
}

void approximateReuseStack::print_tree(atree_node *n, int depth, int dist,
                                       void (*callback)(int, address_t) ) {
  if (n == NULL) return;
  print_tree(n->right, depth+1, dist, callback);
  dist += node_weight(n->right);
  for (int i=0;i<depth;i++) printf(" ");
  printf ("%"PRIacc"(%d)->addr? sz %d cap %d dist %d\n",
          n->time, n->weight, /*n->addr * blockBytes,*/ n->size, n->capacity, dist);
  if (callback) callback(dist, 0 /*n->addr*/);
  print_tree(n->left, depth+1, dist + n->size, callback);
}


