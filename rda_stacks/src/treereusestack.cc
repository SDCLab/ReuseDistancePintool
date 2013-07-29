
#include <algorithm>
#include <new>
#include <string.h>
#include <stdexcept>

#include "reusestack.h"
#include "treereusestack.h"

const address_t TreeReuseStack::kHoleAddress;// = static_cast<address_t>(-1);

static tree_node * getNth(int index, tree_node *n) {
  if (index == n->rtwt) return n;
  if (index < n->rtwt) return getNth(index, n->rt);
  else /*if(index > n->rtwt)*/ return getNth(index - n->rtwt - 1, n->lft);
}



TreeReuseStack::TreeReuseStack(FILE * outfile, int granularity)
    : reference_count_(0), blockBytes(granularity), tot_addrs(0), stackSize(0),
      blockCapacity(0), bcIndex(0), bcObject(NULL), capacityCallback(NULL), delete_first_time(1) {

  address_t addr = 1;
  root = new tree_node;
  root->inum = reference_count_;
  root->addr = addr;
  root->rtwt = 0;
  root->lft = root->rt = NULL;
  p_stack.push_back(NULL);
#ifdef PERF
  treeRefCalls = deleteInumCalls = totalDepth = no_splay_steps = hole_jumps_ = 0;
#endif
}

void TreeReuseStack::ReplaceTree(const TreeReuseStack &other) {
  //last_access.clear();
  last_access = other.last_access;
  reference_count_ = other.reference_count_;
  delete_first_time = other.delete_first_time;
  delete_last_inum = other.delete_last_inum;
  freeTree(root);
  root = copyTree(other.root);
}

void TreeReuseStack::freeTree(tree_node *ptr) {
  if (ptr->rt) freeTree(ptr->rt);
  if (ptr->lft) freeTree(ptr->lft);
  delete ptr;
}


tree_node * TreeReuseStack::copyTree(tree_node *root) {
  if (root == NULL) return NULL;
  tree_node *ptr = new tree_node;
  ptr->inum = root->inum;
  ptr->addr = root->addr;
  ptr->rtwt = root->rtwt;
  ptr->lft = copyTree(root->lft);
  ptr->rt = copyTree(root->rt);
  return ptr;
}

TreeReuseStack::~TreeReuseStack() {
#ifdef PERF
  printf("treeRef calls %"PRIacc", avg depth %f, delete calls %"PRIacc", splay steps %"PRId64
         ", hole jumps %"PRIacc"\n",
         treeRefCalls, (float)totalDepth / (float)treeRefCalls, deleteInumCalls, no_splay_steps,
         hole_jumps_);
#endif
  freeTree(root);
}

static int getweight(tree_node *ptr) {
  if (ptr == NULL) return 0;
  return 1 + getweight(ptr->lft) + getweight(ptr->rt);
}

/*
  Utility to check the consistency of the tree. returns weight of the tree at ptr. no side effects.
*/
int TreeReuseStack::treeCheck(tree_node *ptr) {
  int rtweight, weight = 0;
  if (ptr->lft == ptr) {
    printf("Loop left at %p-> %p, %"PRIacc"\n", ptr, ptr->lft, ptr->inum);
  }
  else if (ptr->lft) {
    if (ptr->lft->inum >= ptr->inum) {
      printf("tree invariant error %p->%p, %"PRIacc",%"PRIacc" ", ptr, ptr->lft, ptr->inum, ptr->lft->inum);
      printf("addrs %p, %p\n", (void *)ptr->addr, (void *)ptr->lft->addr);
    }
    weight += treeCheck(ptr->lft);
  }
  if (ptr->rt == ptr) {
    printf("Loop right at %p-> %p, %"PRIacc"\n", ptr, ptr->rt, ptr->inum);
  }
  else if (ptr->rt) {
    if (ptr->rt->inum <= ptr->inum) {
      printf("tree invariant error %p->%p, %"PRIacc",%"PRIacc"\n", ptr, ptr->rt, ptr->inum, ptr->rt->inum);
      printf("addrs %p, %p\n", (void *)ptr->addr, (void *)ptr->rt->addr);
    }
    //if(ptr->rtwt != getweight(ptr->rt))
    //    printf("weight error %p->%p, %d,%d", ptr, ptr->lft, ptr->rtwt, ptr->rt->rtwt);
    rtweight = treeCheck(ptr->rt);
    weight += rtweight;
    if (rtweight != ptr->rtwt)
      printf("rtweight error %p->%p, %d,%d\n", ptr, ptr->lft, ptr->rtwt, ptr->rt->rtwt);
  } else {
    if (ptr->rtwt != 0){
      printf("weight error %p,%d\n", ptr, ptr->rtwt);
    }
  }
  return weight + 1;
}

/* thank you, thank you, Sam I Am! */
bool TreeReuseStack::dupNodeCheck(const tree_node *thingOne, const tree_node *thingTwo) const {
  if (thingOne == NULL && thingTwo == NULL) return true;
  if (thingOne == NULL || thingTwo == NULL) return false;
  if (thingOne->addr != thingTwo->addr) return false;
  if (dupNodeCheck(thingOne->lft, thingTwo->lft) == false) return false;
  return dupNodeCheck(thingOne->rt, thingTwo->rt);
}

bool TreeReuseStack::CheckDuplicateNodes(const TreeReuseStack &other) const {
  return dupNodeCheck(root, other.root);
}

void TreeReuseStack::InsertNewNode(address_t addr, tree_node *nnode) {
  try {
    if (nnode == NULL) {
      nnode = new tree_node;
    }
    memset(nnode, 0, sizeof(tree_node));

    nnode->inum = reference_count_;
    nnode->addr = addr;
    nnode->lft = root;
    nnode->rt = NULL;
    nnode->rtwt = 0;
    root = nnode;
  } catch (std::bad_alloc exc) {
    printf("failed allocating new tree node: %s\n", exc.what());
    throw;
  }
}

/*accesses address addr, pulling it to top of stack
 * addr should be a multiple of blockBytes
 *
 */
acc_count_t TreeReuseStack::StackAccess(address_t addr) {
  acc_count_t i_inum;		/* Key lookup */
  acc_count_t ret = kStackNotFound;

  if (++reference_count_ >= kAccessCountMax){
    throw std::overflow_error("Overflow in number of references!");
  }

  if ((i_inum = HashLookup(addr)) != 0) {
    tree_node *node;
    ret = DoHoleAccess(i_inum, addr, &node);
    if (ret != kStackNotFound) {
      InsertNewNode(addr, node);
    } else {
      ret = RefTree(i_inum, addr);
    }
    //treeCheck(root);
    if (ret == kStackNotFound) {
      printf("ref_tree notfound but hash found\n");
      treeCheck(root);
    }
  } else {
    tree_node *node;
    DoHoleAccess(0, addr, &node);
    InsertNewNode(addr, node);
  }
  //treeCheck(root);

  bool miss = capacityCallback != NULL && (ret == kStackNotFound ||
                                           ret >= static_cast<acc_count_t>(blockCapacity));
  if (miss) {
    blocksWithinCapacity.insert(root->addr);
    //if(bcIndex == 1) printf("inserted %lx BWC %lu\n", root->addr, blocksWithinCapacity.size());
  }
  tree_node *nnode; /* New tree node */
  if (miss && blocksWithinCapacity.size() > static_cast<size_t>(blockCapacity)) {
    //blocksWithinCapacity.insert(root->addr);
    nnode = getNth(blockCapacity, root);
    //treeCheck(root);
    //if(bcIndex == 1) printf("evicted %lx BWC %lu\n", nnode->addr, blocksWithinCapacity.size());
    //if(refNoModify(root, nnode->inum) != blockCapacity)
    //printf("getNth returned %d instead of %d\n", refNoModify(root, nnode->inum), blockCapacity);

    if (blocksWithinCapacity.erase(nnode->addr) != 1) {
      //nnode = getNth(blockCapacity, root);
      printf("tried to erase nonexistent %"PRIaddr"\n", nnode->addr);
    }
    capacityCallback(bcObject, nnode->addr, blockCapacity, bcIndex);
    if (blocksWithinCapacity.size() > (unsigned)blockCapacity) {
      throw std::runtime_error("too many blocks");
    }
  }
  return ret;
}

/**
 * Returns the depth in the stack of the address
 */
acc_count_t TreeReuseStack::GetDepth(address_t addr) {
  if (last_access.count(addr) != 0) return GetDepthAndNode(last_access[addr], NULL);
  return kStackNotFound;
}

#if defined(NOHOLES)
acc_count_t TreeReuseStack::SnoopInvalidate(address_t addr)
{
    if(last_access.count(addr) == 0) return kStackNotFound;
    acc_count_t inum = last_access[addr];
    tree_node *del = delete_inum(inum, addr);
    //treeCheck(root);
    if(del == NULL) {
        printf("error: NULL node returned for inum %u from addr %p\n", inum, (void *)addr);
        treeCheck(root);
    }
    else if(del->inum != inum || del->addr != addr){
        printf("error: wrong inum/addr returned for deletion %d:%p ",del->inum, (void *)del->addr);
        printf("instead of %d:%p", inum, (void *)addr);
    }
    delete del;
    last_access.erase(addr);
    if(capacityCallback) {
        //if(bcIndex == 1)printf("invalidated %lx, BWC %lu LA %lu ",
                                 //addr, blocksWithinCapacity.size(), last_access.size());
        if(blocksWithinCapacity.erase(addr) == 1 && last_access.size() > blockCapacity){
        //if(last_access.size() > blocksWithinCapacity.size() )
            blocksWithinCapacity.insert(getNth(blockCapacity-1, root)->addr);
            //if(bcIndex == 1)printf("invalidated %lx, inserted %lx\n",
                                     //addr, getNth(blockCapacity, root)->addr);
        }
        //else if (bcIndex == 1)printf("\n");
    }
    --stackSize;
    return inum;
}
acc_count_t TreeReuseStack::DoHoleAccess(acc_count_t inum, address_t addr, tree_node **ret_node) {
  *ret_node = NULL;
  return kStackNotFound;
}
#else
acc_count_t TreeReuseStack::SnoopInvalidate(address_t addr) {
  if (last_access.count(addr) == 0) return kStackNotFound;
  acc_count_t inum = last_access[addr];
  tree_node *inval;
  //if(GetDepthAndNode(inum, &inval) != refNoModify(root, inum))
  //    printf("ERROR: GetDepthAndNode didnt match refNoModify\n");
  GetDepthAndNode(inum, &inval);
  if (inval != NULL) {
    //holeHeap.push_back(inval->inum);
    //std::push_heap(holeHeap.begin(), holeHeap.end());
    hole_set_.insert(inval->inum);
    inval->addr = kHoleAddress;
  } else {
    //if(invalidated.count(inum)) printf("double invalidation\n");
    printf(" lookup returned NULL addr 0x%"PRIaddr" entries %"PRIacc"\n",
           addr, reference_count_);
  }
  last_access.erase(addr);
  --stackSize;
  return inum;
}

// returns the inum of the hole to be filled by the Access to inum
//acc_count_t TreeReuseStack::holeAccess(acc_count_t inum) {
//  if (hole_set_.size() == 0) return kStackNotFound;
//  acc_count_t holeTop = *hole_set_.begin();
//  if (inum < holeTop) {
//    hole_set_.erase(holeTop);
//    if (inum != 0) hole_set_.insert(inum);
//    return holeTop;
//  }
//  return kStackNotFound;
//}

// returns the depth of the accessed node if one was accessed (only happens if a hole is
// leapfrogged). if a node was removed from the tree, returns a pointer to that node in ret_node
acc_count_t TreeReuseStack::DoHoleAccess(acc_count_t inum, address_t addr, tree_node **ret_node) {
  //find the accessed node, make it the hole, delete the hole, add node at top
  *ret_node = NULL;
  if (hole_set_.size() == 0) return kStackNotFound;
  acc_count_t hole_top = *hole_set_.begin();
  if (inum < hole_top) { // a hole is leapfrogged
#ifdef PERF
    hole_jumps_++;
#endif
    acc_count_t depth = kStackNotFound;
    hole_set_.erase(hole_top);  // fill it in
    tree_node *node;
    if (inum != 0) { // Access is to an existing node rather than a new one
      // find the accessed node and make it the hole
      depth = GetDepthAndNode(inum, &node);
      node->addr = kHoleAddress;
      hole_set_.insert(inum);
    }
    // delete the hole from its current location
    *ret_node = delete_inum(hole_top, kHoleAddress);
    if (*ret_node == NULL) {
      printf("error: NULL node returned for inum %"PRIacc" from addr %p\n",
             hole_top, (void *)addr);
      treeCheck(root);
    }
    else if ((*ret_node)->inum != hole_top) {
      printf("error: wrong inum/addr returned for deletion %"PRIacc":%p ",
             (*ret_node)->inum, (void *)(*ret_node)->addr);
      printf("instead of %"PRIacc":%p\n", hole_top, (void *)addr);
    }
    //InsertNewNode(addr, node);
    return depth;
  }
  return kStackNotFound;
}
#endif

/*
 * Lookup 'addr' in the hash table. Adds 'addr' to the hash table if it is
 * not found.
 *
 * Input: Address to be looked up
 * Output: Previous arrival time of address if found, zero if not.
 * Side effects: Adds the address to the hash table if it is not found.
 * Updates the previous time of arrival of address.
 *
 * If this method changes, GetDepth() should also be updated.
 */
acc_count_t TreeReuseStack::HashLookup(address_t addr) {
  acc_count_t old_inum;		/* Scratch variables */

  if (last_access.count(addr) == 0) {
    ++tot_addrs;
    if (++stackSize > std::numeric_limits<int32_t>::max()) {
      throw std::overflow_error("Stack size overflow (2G entries fits in memory? really?)");
    }
    try{
      last_access[addr] = reference_count_;
    } catch (std::bad_alloc exc) {
      printf("failed allocation adding to hash: tot_addrs %"PRIacc" what:%s\n",
             tot_addrs, exc.what());
    }
    /*if ((last_access.size() * getBlockBytes()) % (5*1024*1024) == 0){
      printf("%luMB program use\n", last_access.size()*getBlockBytes()/1024/1024);
      printf("last_access bucket %zu, tot_addrs %"PRIacc" stackSize %d\n",
             last_access.bucket_count(), tot_addrs, stackSize);
    }*/
    return 0;
  } else {
    old_inum = last_access[addr];
    last_access[addr] = reference_count_;
    return old_inum;
  }
}

void TreeReuseStack::print_tree(tree_node *n, int depth,
                                int dist, void (*callback)(int, address_t)) {
  if (n == NULL) return;
  print_tree(n->rt, depth+1, dist, callback);
  dist += n->rtwt;
  for (int i=0;i<depth;i++) printf(" ");
  printf("%"PRIacc"(%d)->%"PRIaddr" dist %d\n", n->inum, n->rtwt, n->addr * getBlockBytes(), dist);
  if (callback) callback(dist, n->addr);
  print_tree(n->lft, depth+1, dist+1, callback);
}

bool TreeReuseStack::IsInCache(address_t block) {
  return blocksWithinCapacity.count(block) > 0;
}

void TreeReuseStack::SetCapacityActions(void * obj, int cap, int index,
                                        void (*func)(void *, address_t, int, int)) {
  blockCapacity = cap;
  bcIndex = index;
  bcObject = obj;
  capacityCallback = func;
}



/*
 * Looks up the key i_inum in the splay tree, deletes the node
 * and reinserts it at the top. Also splays the previous entry in the
 * stack to the root.
 *
 * Input: Key to be looked up (i_inum) and current address.
 * Output: stack depth of addr in tree
 * Side effects: Updates tree as described above.
 */
acc_count_t TreeReuseStack::RefTree(acc_count_t i_inum, address_t addr) {
  tree_node *ptr;
  int top, addr_above, pos = 0, lstlft, at;
  acc_count_t ret = kStackNotFound;

#ifdef PERF
  treeRefCalls++;
#endif

  if (root->inum == i_inum && root->rtwt == 0) {
    ret = 0;//0;//++out_stack[1];
    root->inum = reference_count_;
  } else {
    top = addr_above = lstlft = 0;
    ptr = root;
    while (ptr) {
#ifdef PERF
      totalDepth++;
#endif
      ++top;
      if (top >= (int) p_stack.size()) p_stack.push_back(ptr);
      p_stack[top] = ptr;
      if (ptr->inum > i_inum) {
        addr_above += ptr->rtwt + 1;
        lstlft = top;
        ptr = ptr->lft;
      } else {
        if (ptr->inum == i_inum) {
          addr_above += ptr->rtwt;
          ret = addr_above;//++out_stack[addr_above + 1];
          pos = top;
          if (ptr->addr != addr) fprintf(stderr, "libcheetah: inconsistency w/ inum & addr'\n");
          ptr->rtwt -= 1;
          ptr = ptr->rt;
          while (ptr) {
            ++top;
            if (top >= (int) p_stack.size()) p_stack.push_back(ptr);
            p_stack[top] = ptr;
            ptr = ptr->lft;
          }
          break;  // stack top is successor, no left child
        }
        ptr->rtwt -= 1;
        ptr = ptr->rt;
      }
    }

    if (pos == top) {
      if (p_stack[top - 1]->lft == p_stack[top]) {
        p_stack[top - 1]->lft = p_stack[top]->lft;
      } else {
        p_stack[top - 1]->rt = p_stack[top]->lft;
      }
      at = lstlft;
    } else {
      if (p_stack[top - 1]->lft == p_stack[top]) {
        p_stack[top - 1]->lft = p_stack[top]->rt;
      } else {
        p_stack[top - 1]->rt = p_stack[top]->rt;
      }
      p_stack[pos]->addr = p_stack[top]->addr;
      p_stack[pos]->inum = p_stack[top]->inum;
      at = top - 1;
    }
    while (at > 1) {
#ifdef PERF
      no_splay_steps += 1; /* Counts the number of basic operations */
#endif
      splay(at);
      at = at - 2;
    }
    root = p_stack[1];

    p_stack[top]->lft = root;
    p_stack[top]->rt = NULL;
    p_stack[top]->inum = reference_count_;
    p_stack[top]->addr = addr;
    p_stack[top]->rtwt = 0;
    root = p_stack[top];
    /* traverse(root); */
  }
  return ret;
}


/*
 * Looks up a key and finds number of elements above it in the stack,
 * but does not update the tree
 */
acc_count_t TreeReuseStack::GetDepthAndNode(acc_count_t i_inum, tree_node **node) const {
//    int top, addr_above, pos = 0, lstlft, at;
  int addr_above = 0;
  tree_node *ptr;
  acc_count_t ret = kStackNotFound;
  if (root->inum == i_inum && root->rtwt == 0) {
    ret = 0;  // ++out_stack[1];
    ptr = root;
  } else {
    ptr = root;
    while (ptr) {
      if (ptr->inum > i_inum) {
        addr_above += ptr->rtwt + 1;
        ptr = ptr->lft;
      } else {
        if (ptr->inum == i_inum) {
          addr_above += ptr->rtwt;
          ret = addr_above;//++out_stack[addr_above + 1];
          break;//stack top is successor, no left child
        }
        ptr = ptr->rt;
      }
    }
  }
  if (ptr == NULL || (node != NULL && ptr->inum != i_inum)) {
    printf("ERROR: node called for and inum %"PRIacc" not found or ptr null: ptr %p ptr->inum %"
           PRIacc"\n",
           i_inum, ptr, ptr != NULL ? ptr->inum : 0);
    *node = NULL;
  } else if (node) {
    *node = ptr;
  }
  return ret;
}

/*
 * Deletes the last node in the stack from the tree.
 *
 * Input: None
 * Output: Deleted node
 * Side effects: Deletes last node from tree.
 */
tree_node * TreeReuseStack::delete_oldest_node(void) {
  tree_node *ptr, *free_ptr;
  //static struct tree_node **delete_stack;
  //static int top;
  //static short first_time=1;
  //static unsigned last_inum;

  if (delete_first_time
      || delete_top < 5
      || delete_stack[delete_top]->inum != delete_last_inum
      || delete_stack[delete_top-1]->lft != delete_stack[delete_top]) {
    if (delete_first_time) {
      delete_first_time = 0;
      //delete_stack = calloc(MAX_LINES, sizeof (struct tree_node *));
    }
    delete_top = -1;
    ptr = root;
    while (ptr->lft != NULL){//traverse the left pointers to the smallest entry, pushing as you go
      ++delete_top;
      if ((int)delete_stack.size() <= delete_top) delete_stack.push_back(ptr);
      delete_stack [delete_top] = ptr;
      ptr = ptr->lft;
    } //now ptr is oldest node, top is its parent
    delete_stack[delete_top]->lft = ptr->rt;//link left of its parent to its right
    free_ptr = ptr;

    ptr = delete_stack[delete_top]->lft;//now ptr is delete node's right child
    while (ptr) { //traverse down/left from right child, pushing
      ++delete_top;
      if ((int)delete_stack.size() <= delete_top) delete_stack.push_back(ptr);
      delete_stack [delete_top] = ptr;
      ptr = ptr->lft;
    }
    delete_last_inum = delete_stack[delete_top]->inum;//last_inum is new oldest node
  } else {
    if (delete_stack[delete_top]->lft != NULL) {
      fprintf(stderr, "libcheetah: lft ptr of last entry not NULL\n");
    }

    free_ptr = delete_stack[delete_top];
    delete_stack[delete_top-1]->lft = delete_stack[delete_top]->rt;
    --delete_top;
    ptr = delete_stack[delete_top]->lft;
    while (ptr) {
      ++delete_top;
      if ((int)delete_stack.size() <= delete_top) delete_stack.push_back(ptr);
      delete_stack [delete_top] = ptr;
      ptr = ptr->lft;
    }
    delete_last_inum = delete_stack[delete_top]->inum;
  }
  return free_ptr;
}

//deletes the node with inum i_inum from tree, if it is found
//no rebalancing.
tree_node * TreeReuseStack::delete_inum(acc_count_t i_inum, address_t addr) {
  tree_node *ptr = root, *parent = NULL, *ret = NULL;
  int top = -1;
  bool delete_again = false;

#ifdef PERF
  deleteInumCalls++;
#endif
  if (root == NULL)
    printf("null first");
  //find the inum
  while(ptr) {
    ++top;
    if (top >= (int)p_stack.size()) p_stack.push_back(ptr);
    p_stack[top] = ptr;
    if( i_inum < ptr->inum) {
      ptr = ptr->lft;
    } else if (i_inum > ptr->inum) {
      ptr->rtwt -= 1;
      ptr = ptr->rt;
    } else {
      //this is important when the deleted node is swapped with its successor below
      ptr->rtwt -= 1;
      break;
    }
  } //top of stack is now inum
  ptr = p_stack[top];
  if (ptr->inum != i_inum) {
    printf("error: inum %"PRIacc" not found in delete_inum\n", i_inum);
    return NULL;
  }
  if (ptr->addr != addr && addr != kHoleAddress) {
        printf("addr mismatch for inum %"PRIacc", addr %p in delete_inum\n",
               i_inum, (void *)ptr->addr);
        return NULL;
  }
  if (ptr != root) parent = p_stack[top-1];

  //classical tree deletion algorithm
  do {
    if (ptr->lft == NULL) {
      //replace ptr with its right child
      if (parent != NULL) {
        if (parent->lft == ptr) {
          parent->lft = ptr->rt;
        }
        else {
          parent->rt = ptr->rt;
        }
      } else {
        root = ptr->rt;
      }
      ret = ptr;
      delete_again = false;
    } else if (ptr->rt == NULL) {
      //replace ptr with its left child
      if (parent != NULL) {
        if (parent->lft == ptr) {
          parent->lft = ptr->lft;
        } else {
          parent->rt = ptr->lft;
        }
      } else {
        root = ptr->lft;
      }
      ret = ptr;
      delete_again = false;
    } else {
      //swap with in-order successor (has no left child) and delete
      tree_node *succ = ptr->rt, *succparent = ptr;
      address_t addtemp;
      acc_count_t itmp;
      while (succ->lft) {
        succparent = succ;
        succ = succ->lft;
      }
//            tmp = succ->rt;
//            succ->lft = ptr->lft;
//            succ->rt = ptr->rt;
//            succ->rtwt = ptr->rtwt - 1;
//            if(parent != NULL){
//                if(parent->lft == ptr) parent->lft = succ;
//                else parent->rt = succ;
//            }
//            ptr->rt = tmp;
//            ptr->lft = NULL;
      addtemp = ptr->addr;
      itmp = ptr->inum;
      ptr->addr = succ->addr;
      ptr->inum = succ->inum;
      succ->addr = addtemp;
      succ->inum = itmp;
      ptr = succ;
      parent = succparent;
      delete_again = true;
      //treeCheck(root);
    }
  } while (delete_again);
  //fix wieghts of higher nodes
  //treeCheck(root);
  return ret;
}

/*
 * A left rotation. Adapted from the Sleator and Tarjan paper on Splay trees
 * Makes use of p_stack, setup during the lookup.
 *
 * Input: Index to entry at which rotation is to be done in p_stack.
 * Output: None
 * Side effects: Does a left rotation at the entry
 */
void TreeReuseStack::rotate_left(int y) {
  int x,z;

  z = y-1;
  x = y+1;
  if (z > 0) {
    if (p_stack[z]->lft == p_stack[y]) {
      p_stack[z]->lft = p_stack[x];
    } else {
      p_stack[z]->rt = p_stack[x];
    }
  }
  p_stack[y]->rt = p_stack[x]->lft;
  p_stack[y]->rtwt -= p_stack[x]->rtwt + 1;
  p_stack[x]->lft = p_stack[y];
  p_stack[y] = p_stack[x];
  p_stack[x] = p_stack[x+1];
}


/*
 * A right rotation. Adapted from the Sleator and Tarjan paper on Splay trees
 * Makes use of p_stack, setup during the lookup.
 *
 * Input: Index to entry at which rotation is to be done in p_stack.
 * Output: None
 * Side effects: Does a right rotation at the entry
 */
void TreeReuseStack::rotate_right(int y) {
  int x,z;
  register tree_node *t1, *t2, *t3;

  z = y-1;
  x = y+1;
  t1 = p_stack[x];
  t2 = p_stack[y];
  t3 = p_stack[z];
  if (z>0) {
    if (t3->lft == t2) {
      t3->lft = t1;
    } else {
      t3->rt = t1;
    }
  }
  t2->lft = t1->rt;
  t1->rt = t2;
  t1->rtwt += t2->rtwt + 1;
  p_stack[y] = t1;
  p_stack[x] = p_stack[x+1];
}


/*
 * Adapted from the Sleator and Tarjan paper on Splay trees.
 * Splay the input entry to the top of the stack.
 * Makes use of p_stack setup during the lookup.
 *
 * Input: Index to entry to be splayed to root in p_stack.
 * Output: None
 * Side effects: Does a spay on the tree
 */
void TreeReuseStack::splay(int at) {
  int x, px, gx;

  x = at;
  px = at-1;
  gx = at-2;

  /* 'at' is a left child */
  if (p_stack[x] == p_stack[px]->lft) {
    if (gx == 0) {  /* zig */
      rotate_right(1);
    }
    else if (p_stack[px] == p_stack[gx]->lft) {   /* zig-zig */
      rotate_right(gx);
      rotate_right(gx);
    } else {                               /* zig-zag */
      rotate_right(px);
      rotate_left(gx);
    }
  }
  /* 'at' is a right child */
  else if (gx == 0) {                             /* zig */
    rotate_left(1);
  } else if (p_stack[px] == p_stack[gx]->rt) {         /* zig-zig */
    rotate_left(gx);
    rotate_left(gx);
  } else {                                   /* zig-zag */
    rotate_left(px);
    rotate_right(gx);
  }
}
