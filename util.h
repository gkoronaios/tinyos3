
#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <assert.h>
#include <time.h>

/**
	@file util.h

	@brief Tinyos utility code.

	This file defines the following:
	- macros for error checking and message reporting
	- a _resource list_ data structure
	- a _resource heap_ data structure


	@defgroup utilities Utility code
	@{
*/


/**
	@defgroup check_macros Macros for checking system calls.

	@brief Macros for checking system calls and reporting errors.

	With regard to reporting an error, there are, generally speaking, two kinds of functions in
	the Unix API:
	-# Functions that return >=0 on success and -1 on error, storing the error code
	  in the global variable @c errno, and
	-# Functions which return an error code directly.

	The macros in this section help with checking the error status of such calls,
	when an error should abort the program. Their use can be seen in bios.c

	@{
 */

/**
	@brief Print a message to stderr and abort.
 */
#define FATAL(msg) { fprintf(stderr, "FATAL %s(%d):%s: %s\n", \
__FILE__, __LINE__, __FUNCTION__, (msg)); abort(); }

/**
	@brief Print an error message to stderr and abort.

	This macro will print a readable message (using @c FATAL) 
	corresponding to a unix error code, usually stored in 
	@c errno, or returned from a function.

	@param errcode a unix error code
	@see FATAL
 */
#define FATALERR(errcode) FATAL(strerror(errcode))

/**
	@brief Wrap a unix call to check for errors.

	This macro can wrap a call whose (integer) return value 
	is 0 for success or an error code for error.

	For example:
	@code
	CHECKRC(pthread_mutex_init(&mutex, NULL));
	@endcode
	@see CHECK
*/
#define CHECKRC(cmd) { int rc = (cmd); if(rc) FATALERR(rc); }

/**
	@brief Wrap a unix call to check for errors.

	This macro can wrap a call whose (integer) return value 
	is 0 for success or -1 for failure, with an error code stored
	in errno.

	For example:
	@code
	CHECK(gettimeofday(t, NULL));
	@endcode
	@see CHECK
*/	
#define CHECK(cmd)  { if((cmd)==-1) FATALERR(errno); }

/**
	@brief Check a condition and abort if it fails.

	This macro will check a boolean condition and 
	abort with a message if it does not hold. It is used to
	check parameters of functions.

	This is similar to @c assert(...), but will not be turned off.
*/
#define CHECK_CONDITION(expr) { if(!(expr)) FATAL("Failed constraint: " # expr) }


/**
	@brief A wrapper for malloc checking for out-of-memory.

	If there is no memory to fulfill a request, FATAL is used to
	print an error message and abort.

	@param size the number of bytes allocated via malloc
	@returns the new memory block
  */
static inline void * xmalloc (size_t size)
{
  void *value = malloc (size);
  if (value == 0)
    FATAL("virtual memory exhausted");
  return value;
}


/** @}   check_macros  */


/**
	@defgroup pointer_marking Routines for marking pointers.
	@brief Some routines for marking pointers.

	In some situations, it is useful to store single-bit flags inside objects.
	This is often the case with data structures. In order to avoid wasting a
	whole byte (or more) of space in each object, one can often use the
	least-significant bits of pointers. 

	This works well, since with the exception of pointers to char, other types
	of pointers are already aligned at some power of 2 higher than 1, therefore
	the least significant bit of any pointer is actually always 0.

	Pointer marking is a strong optimization, and as such it is not recommended
	for simple problems.

	The routines in this group are used in the implementation of the pairing heap
	data structure.

	@{

 */


#define __mark_mask  ((uintptr_t)1)
#define __unmark_mask  (~ (uintptr_t)1)

/**
	@brief Return a marked pointer.

	This routine will set a mark bit into a (possibly already marked) pointer and return the new
	pointer. The returned pointer should not be dereferenced, as results will be undefined
	(and probably catastrophic).

	@see pointer_unmarked
	
	@param ptr the pointer to mark
	@return a marked pointer
 */
static inline void* pointer_marked(void* ptr) { return (void*) ((uintptr_t)ptr | __mark_mask); }

/**
	@brief Return an unmarked pointer.

	This routine will unset a mark bit into a (possibly already unmarked) pointer and return the new
	pointer. This pointer can be dereferenced without a problem.


	@param ptr the pointer to unmark
	@return the unmarked pointer
 */
static inline void* pointer_unmarked(void* ptr) { return (void*) ((uintptr_t)ptr & __unmark_mask); }

/**
	@brief Check for the existence of a mark in a pointer.

	@param ptr the pointer to check
	@return 1  if the pointer is marked and 0 if it is unmarked
 */
static inline int pointer_is_marked(void* ptr) { return (uintptr_t)ptr & __mark_mask;  }

#undef __mark_mask
#undef __unmark_mask

/** @}   pointer_marking  */



/*******************************************************
 *
 *
 *******************************************************/

/**
	@defgroup rlists  Resource lists
	@brief  A simple and fast list implementation.


	Overview
	--------

	This data structure is a doubly-linked circular list, whose implementation
	is based on the splicing operation. 

	In a circular list, the nodes form a ring. For example if a, b, and c
	are nodes, a ring may look like
	@verbatim
	+--> a --> b --> c -->+
	|                     |
	+<------<--------<----+
	@endverbatim
	where only the @c next pointer is drawn. The @c prev pointer of a node
	is always the opposite of @c next, i.e.,  @c p->next->prev==p->prev->next==p.
	In the following, we shall denote such a ring by [a,b,c]. Note that
	[b,c,a] and [c,a,b] are describing the same ring as [a,b,c]. A singleton ring
	has just one node, e.g., [a].

	The splicing operation between two rlnodes a and b means simply to
	swap their @c next pointers (also adjusting the 'prev' pointers appropriately).
	Splicing two nodes on different rings, joins the two rings. Splicing two
	nodes on the same ring, splits the ring. 
	For example, @c splice(a,c) on ring
	[a,b,c,d] would create two rings [a,d] and [b,c]. 
	A splice can be reversed by repeating it; continuing the previous example, 
	given rings [a,d] and [b,c], splice(a,c) will create ring [a,b,c,d] again.
	The precise definition of splice
	is the following:
	@code
	rlnode* splice(rlnode* a, rlnode* b) {
		swap(& a->next->prev, & b->next->prev);
		swap(& a->next, & b->next);
		return b;
	}
	@endcode
	In general, @c splice(a,b) applies the following transformation
	@verbatim
	[a, x...]  [b, y...]   ==>   [a, y..., b, x...]
	[a, x..., b, y...]     ==>   [a, y...]  [b, x...]
	@endverbatim

	To implement lists, an rlnode object is used 
	as _sentinel_,  that is, it holds no data and is not properly part of the 
	list. If L is the list node, then ring  [C, L, A, B]  represents the list {A,B,C}.
	The empty list is represented as [L].

	We now show some examples of list operations, implemented by splicing.
	Suppose that L is a pointer to the sentinel node of a list.
	Also, suppose that N is (pointer to) a node in a singleton ring [N]
	Then, the the following operation are implemented as shown (in pseudocode):
	@verbatim
	empty(L)              ::  return  L == L->next
	head(L)               ::  return  L->next
	tail(L)               ::  return  L->prev
	push_front(L, N)      ::  splice(L, N)
	push_back(L, N)       ::  splice(L->prev, N)
	pop_front(L)          ::  return splice(L, L->next)
	pop_back(L)           ::  return splice(L, L->prev) 
	remove(N)             ::  return splice(N->prev, N)
	insert_after(P, N)    ::  splice(P, N->prev)
	insert_before(P, N)   ::  splice(P->prev, N)
	@endverbatim

	These operations can be used to perform other operations. For example,
	if L1 and L2 are two lists, then we can append the nodes of L2 to L1
	(leaving L2 empty), by the following two operations:
	@verbatim
	push_back(L1, L2);
	remove(L2);
	@endverbatim

	For more details on the implementation, please read the code of @ref util.h.

	Usage
	-----

	Resource lists are mostly useful as storage for lists of resources. The main type
	is the list node, type @c rlnode. Each @c rlnode object must be initialized before use,
	by calling either @c rlnode_init or @c rlnode_new.
	@code
	TCB* mytcb =...;
	FCB* myfcb =...;

	rlnode n1, n2;

	// The following four lines are equivalent 
	rlnode_init(& n1, mytcb); 
	rlnode_new(& n1)->tcb = mytcb;
	rlnode_init(& n1, NULL);  n1->tcb = mytcb;
	rlnode_new(& n1);  n1->tcb = mytcb;


	n1->fcb = myfcb;
	myfcb = n1->fcb;
	@endcode


	###  Creating lists

	A list is defined by a sentinel node. For example,
	@code
	rlnode mylist;  
	rlnode_new(&mylist);
	@endcode
	Note that, although we did not store a value into the sentinel node, we actually 
	could do so if desired.
	
	Once a list is created, it needs to be filled with data.
	There are routines for adding nodes to the head and tail of a list, or in an intermediate
	location. Also, lists can be compared for equality, have their length taken, checked for
	emptiness, etc.
	@see rlist_push_front
	@see rlist_push_back

	### Intrusive lists

	In order to add nodes to a list, we must allocate @c rlnode objects somewhere in memory.
	It is absolutely legal to use `malloc()` for this purpose, but we must add code to free
	the allocated memory, which can be annoying.

	If we wish to store objects of a particular kind however, we can use a different technique:
	we can store an rlnode pointer inside the object itself. A list built by this trick is called
	an *intrusive list*.

	For example, suppose we want to
	maintain a list of TCBs with high priority.
	@code
	rlnode hi_pri_list;  rlnode_new(&hi_pri_list);

	struct thread_control_block {
	 .... // other stuff
	 rlnode hi_pri_node;
	};

	// initialize the node
	TCB* newtcb = ...;
	rlnode_init(& newtcb->hi_pri_node, newtcb);

	// then, we can just add the node to the list
	rlist_push_back(& hi_pri_list, & newtcb->hi_pri_node);
	@endcode

	Because node @c hi_pri_node is stored inside the object, it is always available. The node
	can be removed and re-added to this or another list, and memory allocation/deallocation
	is not an issue.
	The implementation of tinyos3 uses this idea very extensively, in TCB, PCB and FCB.

	@{
 */


typedef struct process_control_block PCB;	/**< @brief Forward declaration */
typedef struct thread_control_block TCB;	/**< @brief Forward declaration */
typedef struct core_control_block CCB;		/**< @brief Forward declaration */
typedef struct device_control_block DCB;	/**< @brief Forward declaration */
typedef struct file_control_block FCB;		/**< @brief Forward declaration */
typedef struct InodeHandle Inode;					/**< @brief Forward declaration */
typedef struct FsMount FsMount;				/**< @brief Forward declaration */

/** @brief A convenience typedef */
typedef struct resource_list_node * rlnode_ptr;

#define RLNODE_KEY \
    PCB* pcb; \
    TCB* tcb; \
    CCB* ccb; \
    DCB* dcb; \
    FCB* fcb; \
    Inode* inode; \
    FsMount* mnt; \
    void* obj; \
    const char* str; \
    rlnode_ptr node_ptr; \
    intptr_t num; \
    uintptr_t unum; \

/** @brief A convenience type for the key of an @c rlnode.
	
	An rlnode key is a union of pointer types and numeric types.
	There are two numeric types, one signed and one unsigned.
*/
typedef union  __attribute__((__transparent_union__))
{ 
	RLNODE_KEY 
} rlnode_key;

/**
	@brief List node
*/
typedef struct resource_list_node {
  
  /** @brief The list node's key.
     
     The key (data element) of a list node is 
     stored in a union of several pointer and integer types.
     This allows for easy access, without the need for casting. 
     For example,
     \code
     TCB* tcb = mynode->tcb;
     \endcode
     */
	union {
		RLNODE_KEY
		rlnode_key key;
	};

	/* list pointers */
	rlnode_ptr prev;  /**< @brief Pointer to previous node */
	rlnode_ptr next;	/**< @brief Pointer to next node */
} rlnode;


/**
	@brief Initialize a node as a singleton ring.

	This function will initialize the pointers of a node 
	to form a singleton ring. The node is returned, so that
	one can write code such as
	\code
	rlnode n;  rlnode_new(&n)->num = 3;
	\endcode
	@pre @c p!=NULL
	@param p the node to initialize into a singleton
	@returns the node itself
 */
static inline rlnode* rlnode_new(rlnode* p) 
{ 
	p->prev = p->next = p; 
	return p;
}

/**
	@brief Initialize a node as a singleton ring.

	This function will initialize the pointers of a node 
	to form a singleton ring, and store the . The node is returned, so that
	one can write code such as
	\code
	rlnode n;  rlist_push_front(&L, rlnode_init(&n, obj));
	\endcode

	@pre @c p!=NULL
	@param p the node to initialize into a singleton
	@param ptr the pointer to store as the node key
	@returns the node itself
 */
static inline rlnode* rlnode_init(rlnode* p, rlnode_key key)  
{
	rlnode_new(p)->key = key; 
	return p;
}


/**
	@brief Initializer for resource list nodes.

	This macro can be used as follows:
	\code
	rlnode foo = RLNODE(foo, .num=10);
	\endcode

	Another example, initializing an intrusive list node in some
	`struct Foo`:
	\code
	struct Foo { int x; rlnode n; };
	struct Foo foo = { .x=1, .n = RLNODE(foo.n, .obj=&foo) };
	\endcode

	This is handy for global variables.
	@see RLIST
  */
#define RLNODE(N, V) ((rlnode){ V, .prev=&(N), .next=&(N) })


/**
	@brief Initializer for resource lists.

	This macro can be used as follows:
	\code
	rlnode foo = RLIST(foo);
	\endcode
	This is especially handy for global variables.
  */
#define RLIST(L) ((rlnode){ .obj=NULL, .prev=&(L), .next=&(L) })


/** 
	@brief Swap two pointers to rlnode.
*/
static inline void rlnode_swap(rlnode_ptr *p, rlnode_ptr *q) 
{
  rlnode *temp;
  temp = *p;  *p = *q; *q = temp;  
}

/**
	@brief Splice two rlnodes.

	The splice operation swaps the @c next pointers of the two nodes,
	adjusting the @c prev pointers appropriately.

	@param a the first node
	@param b the second node
	@returns the second node, @c b
*/
static inline rlnode* rl_splice(rlnode *a, rlnode *b)
{
  rlnode_swap( &(a->next->prev), &(b->next->prev) );
  rlnode_swap( &(a->next), & (b->next) );
  return b;
}

/**
	@brief Remove node from a ring and turn it into singleton.

	This function will remove @c a from the ring that contains it.
	If @c a is a singleton ring, this function has no effect.
	@param a the node to remove from a ring
	@returns the removed node
*/
static inline rlnode* rlist_remove(rlnode* a) { rl_splice(a, a->prev); return a; }

/** @brief  Check a list for emptiness.

	@param a the list to check
	@returns true if the list is empty, else 0.
 */
static inline int is_rlist_empty(rlnode* a) { return a==a->next; }

/**
	@brief Insert at the head of a list.

	Assuming that @c node is not in the ring of @c list, 
	this function inserts the ring  of @c node (often a singleton) 
	at the head of @c list.

	This function is equivalent to @c splice(list,node). 
  */
static inline void rlist_push_front(rlnode* list, rlnode* node) { rl_splice(list, node); }

/**
	@brief Insert at the tail of a list.

	Assuming that @c node is not in the ring of @c list, 
	this function inserts the ring  of @c node (often a singleton) 
	at the tail of @c list.

	This function is equivalent to @c splice(list->prev,node). 
  */
static inline void rlist_push_back(rlnode* list, rlnode* node) { rl_splice(list->prev, node); }

/**
	@brief Remove and return the head of the list.

	This function, applied on a non-empty list, will remove the head of 
	the list and return in.

	When it is applied to an empty list, the function will return the
	list itself.
*/
static inline rlnode* rlist_pop_front(rlnode* list) { return rl_splice(list, list->next); }

/**
	@brief Remove and return the tail of the list.

	This function, applied on a non-empty list, will remove the tail of 
	the list and return in.
*/
static inline rlnode* rlist_pop_back(rlnode* list) { return rl_splice(list, list->prev); }

/**
	@brief Return the length of a list.

	This function returns the length of a list.
	@note the cost of this operation is @f$ O(n) @f$  
*/
static inline size_t rlist_len(rlnode* list) 
{
	unsigned int count = 0;
	rlnode* p = list->next;
	while(p!=list) {
		p = p->next;
		count++;
	}
	return count;
}

/**
	@brief Check two lists for equality.

	@param L1 the first list
	@param L2 the second list
	@returns true iff two lists are equal, else false. 
 */
static inline int rlist_equal(rlnode* L1, rlnode* L2)
{
	rlnode *i1 = L1->next;
	rlnode *i2 = L2->next;

	while(i1!=L1) {
		if(i2==L2 || i1->num != i2->num)
			return 0;
		i1 = i1->next; 
		i2 = i2->next;
	}

	return i2==L2;
}

/**
	@brief Append the nodes of a list to another.

	After the append, @c lsrc becomes empty. The operation is
	@verbatim
	[ldest, X...] [lsrc, Y...]  ==> [ldest, X..., Y...]  [lsrc]
	@endverbatim
*/
static inline void rlist_append(rlnode* ldest, rlnode* lsrc)
{
	rlist_push_back(ldest, lsrc);
	rlist_remove(lsrc);
}

/**
	@brief Prepend the nodes of a list to another.

	After the append, @c lsrc becomes empty. The operation is
	@verbatim
	[ldest, X...] [lsrc, Y...]  ==> [ldest, Y..., X...]  [lsrc]
	@endverbatim
*/
static inline void rlist_prepend(rlnode* ldest, rlnode* lsrc)
{
	rlist_push_front(ldest, lsrc);
	rlist_remove(lsrc);
}

/**
	@brief Reverse a ring or list.

	This function will reverse the direction of a ring. 
  */
static inline void rlist_reverse(rlnode* l)
{
	rlnode *p = l;

	do {
		rlnode_swap(& p->prev, & p->next);
		p = p->next;
	} while(p != l);
}

/**
	@brief Find a node by key.

	Search and return the first node whose key is equal to a
	given key, else return a given node (which may be NULL).

	@param List the list to search
	@param key the key to search for in the list
	@param fail the node pointer to return on failure
  */
static inline rlnode* rlist_find(rlnode* List, rlnode_key key, rlnode* fail)
{
	rlnode* i= List->next;
	while(i!=List) {
		if(i->obj == key.obj)
			return i;
		else
			i = i->next;
	}
	return fail;
}

/**
	@brief Move nodes 

	Append all nodes of Lsrc which satisfy pred (that is, pred(...) returns non-zero)
	to the end of Ldest.
*/
static inline void rlist_select(rlnode* Lsrc, rlnode* Ldest, int (*pred)(rlnode*))
{
	rlnode* I = Lsrc;
	while(I->next != Lsrc) {
		if(pred(I->next)) {
			rlnode* p = rlist_remove(I->next);
			rlist_push_back(Ldest, p);
		} else {
			I = I->next;
		}
	}
}

/* @} rlists */

/**
	@defgroup rheap Resource priority queues.
	
	@brief  Resource heaps/priority queues.

	## Overview ##

	In an operating system, it is sometimes useful to maintain a collection of resources ordered
	on some property. For example, we may wish to have a set of threads ordered by priority, or a set
	of timeouts ordered by the time they will be triggered. In such a case, we want to be able to
	find the element whose key is minimum, for example the highest-priority thread or the 
	timeout to expire the soonest. As we know from our data-structures course, a good data structure
	to use in such a scenario is a priority queue, also known as a heap.

	There are several heap algorithms proposed in the literature. In TinyOS we have implemented 
	pairing heaps [Fredman, Sedgewick, Sleator and Tarjan, Algorithmica (1986)] 
	(also, see the wikipedia page for this data structure).

	A pairing heap is represented as a (non-binary) tree, where the key stored at a node is less 
	than or equal to the keys stored in its descendants. The root of the tree is a node whose key is 
	minimum among all nodes in the tree.

	In order to define "minimum", we ask the user to provide a "less-than" comparator function, 
	which is passed as a parameter to most of the API functions of our heap implementation. Of course,
	the actual function my be a "greater-than" function, and in this case the heap will maintain
	a maximum-key element at the root.

	The performance of a pairing heap is very good in practice. The *insert* operation takes time
	\f$O(1)\f$, where as the *delete_minimum* or *delete* operation takes amortized time \f$O(\log n)\f$.

	## Implementation ##

	The implementation follows the logic resource lists. We are using @c rlnode as the basic tree node
	construct, by using the two pointers it contains. The children of a node N are part of a singly-linked
	list, the so-called child list. Node N has a pointer to its child list, and a pointer to its next sibling,
	if any. More precisely,
	- @c prev points to the singly-connected child list of node N, and
	- @c next points to the next sibling, if N has a right sibling, or to the parent, if N is the rightmost
	  sibling of its parent.  We use pointer marking (as returned by @ref pointer_marked()) 
	  on the least-significant bit of the sibling pointer to denote "pointer-to-parent".

	The whole heap is represented as a pointer to the root rlnode (the root of the tree). An empty heap is denoted 
	by the @c NULL pointer. The root node's @c next pointer is equal to @c NULL, and is unmarked. This is the only
	node in a heap whose @c next pointer is @c NULL.

	## Usage ##

	In order to use the heaps, we need to have a comparator function on @c rlnode objects, such as
	\code
	int lessf(rlnode *a, rlnode* b) { return a->num < b->num; }
	\endcode

	An empty heap is simply a NULL @ref rlnode pointer.
	\code
	rlnode* heap = NULL;
	\endcode

	In order to add nodes to a heap we have two options. First, we can initialize a node as a singleton
	heap, and then __meld__ it to another heap
	\code
	rlnode a, b;
	rheap_init(&a)->num = 4;
	rheap_init(&b)->num = 7;
	heap = rheap_meld(h, &a, lessf);
	heap = rheap_meld(h, &b, lessf);
	\endcode

	Alternatively, we may skip the heap initialization step:
	\code
	rlnode a, b;
	a.>num = 4;
	b.num = 7;
	heap = rheap_insert(h, &a, lessf);
	heap = rheap_insert(h, &b, lessf);
	\endcode

	Another alternative is to first construct a _resource list_ in the usual way, and then take all the list nodes
	and turn them into a heap:
	\code
	rlnode L = RLIST(L);
	//.  populate L with any number of elements ,,, 
	assert(! is_rlist_empty(&L));
	rlnode* ring = rl_splice(&L, L.prev);  // Take all nodes out of L 
	heap = rheap_from_ring(ring,  lessf);
	\endcode

	Finally, two heaps can be merged into a new heap:
	\code
	heap = pheap_meld(heap, other_heap, lessf);
	\endcode

	To remove items from a heap, there are two options. One is to remove the heap root, that is, the minimum element.
	Note that sometimes, we may need to save the identity of the minimum element before we remove it.
	\code
	rlnode* minelem = heap;
	if(heap!=NULL) heap = rheap_delmin(heap, lessf);
	// After this line, minelem is either NULL or it contains a legal singleton heap
	\endcode

	Alternatively, we may want to remove a specific element from a heap
	\code
	heap = rheap_delete(heap, node, lessf);
	\endcode

	Finally, we can collect all the elements of a heap into a ring
	\code
	rlnode* ring = rheap_to_ring(heap);
	heap = NULL;
	\endcode

	When the value of an object stored in a heap is changed so that if becomes "less" or "more" in the heap
	ordering, we can adjust its position in the heap in two ways. First, we can **delete** and then
	**insert** the node. This will incur an overhead of approximately \f$O(\log n)\f$. Alternatively, in the
	case where we know that the item became "less" or stayed equal, we can simply call @ref rheap_decrease(). 

	@{
*/


/**
	@brief Less-than comparator function type for @c rlnode.

	A requirement for such a function @c F is that, if a and b are two
	rlnode objects, then it should not be that both @c F(a,b) and @c F(b,a)
	return 1. 

	@returns 1 if @c a is strictly less than @c b, and 0 otherwise.
 */
typedef int (*rlnode_less_func)(rlnode* a, rlnode* b);



/**
	@brief Initialize a heap node.

	A heap node is initialized a follows:
	\code
	node->prev = pointer_marked(node);  // To denote empty child list 
	node->next = NULL;  // To denote no siblings
	\endcode

	The returned node is a legal singleton heap.

	@param node the node to initialize
	@returns the passed node initialized as a singleton heap
  */
rlnode* rheap_init(rlnode* node);

/**
	@brief Return the size of a heap.

	Returns the number of nodes in the heap tree. Note that the complexity of this
	call is linear.

	@param heap a heap
	@returns the number of nodes in the heap 
*/
size_t rheap_size(rlnode* heap);

/**
	@brief Return the parent of a node.

	Note that the root node of a heap returns NULL as the parent.

	@param node the node whose parent is returned
	@return the parent of @c node
 */
static inline rlnode* rheap_parent(rlnode* node)
{
	assert(node != NULL);
	rlnode* p = node->next;
	if(p==NULL) return NULL; /* we are the root of the heap */
	while( ! pointer_is_marked(p)) p = p->next;
	return pointer_unmarked(p);
}


/**
	@brief Delete the minimum node from the heap.

	The min-node is reset to a singleton heap, ready to be melded into another heap.
	Note that this node __is not returned__. Instead, the new heap remaining after the removal
	of the min element is returned.

	@param heap a heap from which the minimum node will be removed
	@pre heap must not be NULL
	@returns the new heap obtained after removal (which may be null)
 */
rlnode* rheap_delmin(rlnode* heap, rlnode_less_func lessf);

/**
	@brief Extract a subtree from the heap

	This call will remove @c node from the heap and return the new heap.
	@param heap a heap that must contain @c node
	@param node the node to be removed from the heap
	@param lessf the comparator function
	@return the new heap after removal of node
 */
rlnode* rheap_delete(rlnode* heap, rlnode* node, rlnode_less_func lessf);

/**
	@brief Restore heap after a decrease of the key in some node/

	This call restores the heap invariant after the order of @c node is changed.

	@param heap a heap that must contain @c node
	@param node the node whose key decreased
	@param lessf the comparator function
	@return the new heap after adjustment
 */
rlnode* rheap_decrease(rlnode* heap, rlnode* node, rlnode_less_func lessf);


/**
	@brief Take the union of two heaps.

	This function returns a new heap containing all elements from the
	two operands.

	@param heap1 the first heap
	@param heap2 the second heap
	@param lessf the comparator function
	@returns a new heap constructed as the union of the two operands
 */
rlnode* rheap_meld(rlnode* heap1, rlnode* heap2, rlnode_less_func lessf);


/**
	@brief Insert node into the heap.

	This convenience function adds a new node into a heap. Note that
	the node's prev and next pointers are not assumed to be initialized and are destroyed.

	@param heap the heap into which a new node is added
	@param node the new node to be added
	@param lessf the comparator function
	@returns the new heap after the insertion
*/
static inline rlnode* rheap_insert(rlnode* heap, rlnode* node, rlnode_less_func lessf)
{
	return rheap_meld(heap, rheap_init(node), lessf);
}


/**
	@brief Make a heap out of a ring of nodes.

	Given a resource list of nodes, return a heap made out of
	these nodes according to a comparator.

	@param ring the ring of nodes that will turn into a heap
	@param lessf the comparator function
	@returns a new heap
 */
rlnode* rheap_from_ring(rlnode* ring, rlnode_less_func lessf);


/**
	@brief Make a ring of nodes out of heap nodes.

	Given a heap of nodes, return a ring (cyclic list) made out of
	these nodes. Note that the order of the nodes is not sorted.

	@param ring the heap of nodes that will turn into a ring
	@returns a new heap
 */
rlnode* rheap_to_ring(rlnode* heap);

/* @} rheap */



/**
	@defgroup rhashtable  Resource hash table.
	
	@brief  Resource hash tables.

	## Overview ##

	A hash table provides very fast, typically \f$O(1)\f$ insertion, deletion and lookup, based
	on equality. This is sometimes called "dictionary access".

	@{
 */


typedef unsigned long hash_value;
typedef int (*rdict_equal)(rlnode*, rlnode_key key);
typedef rlnode* rdict_bucket;


typedef struct rdict
{
	unsigned long size;		/**< @brief Number of elements in the table */
	unsigned long bucketno;	/**< @brief Number of buckets in the table */
	rdict_bucket* buckets;		/**< @brief Array of rings */
} rdict;


void rdict_init(rdict* dict, unsigned long buckno);

void rdict_destroy(rdict* dict);

void rdict_clear(rdict* dict);

static inline rdict_bucket* rdict_get_bucket(rdict* dict, hash_value h)
{
	return & dict->buckets[h % dict->bucketno];
}

static inline rlnode* rdict_bucket_lookup(rdict_bucket* bucket, rlnode_key key, rlnode* defval, rdict_equal equalf)
{
	if(*bucket == NULL) return defval;
	rlnode* current = *bucket;
	do {
		if(equalf(current, key))
			return current;
		current = current->next;
	} while(current!=*bucket);
	return defval;
}

static inline rlnode* rdict_lookup(rdict* dict, hash_value h, rlnode_key key, rlnode* defval, rdict_equal equalf)
{
	return rdict_bucket_lookup(rdict_get_bucket(dict, h), key, defval, equalf);
}

static inline void rdict_bucket_insert(rdict_bucket* bucket, rlnode* elem)
{
	if(*bucket == NULL)
		*bucket = elem;
	else
		rl_splice(*bucket, elem);
}

static inline int rdict_equal_nodes(rlnode* p, rlnode* q) { return p==q; }

static inline int rdict_bucket_remove(rdict_bucket* bucket, rlnode* elem)
{
	elem = rdict_bucket_lookup(bucket, elem, NULL, rdict_equal_nodes);
	if(elem==NULL) return 0;

	if(*bucket == elem) {
		if(elem == elem->next)
			*bucket = NULL;
		else {
			*bucket = elem->next;
			rl_splice(elem->prev, elem);
		}
	}
	else {
		rl_splice(elem->prev, elem);
	}
	return 1;
}


static inline void rdict_insert(rdict* dict, rlnode* elem, hash_value h)
{
	rdict_bucket_insert(rdict_get_bucket(dict, h), elem);
	dict->size ++;
}


static inline void rdict_remove(rdict* dict, rlnode* elem, hash_value h)
{
	if(rdict_bucket_remove(rdict_get_bucket(dict, h), elem))
		dict->size --;
}


static inline hash_value hash_combine(hash_value lhs, hash_value rhs)
{
	lhs ^= rhs + 0x9e3779b97f4a7c16 + (lhs << 6) + (lhs >> 2);
  	return lhs;	
}


static inline hash_value hash_string(const char* str)
{
	/* This is from the Python string implementation */
	hash_value h = (*str) << 7;
	while(*str) {
		h *= 1000003;
		h ^= *str++;
	}
	return h;
}


/* @} rhashtable */



/*
	Some helpers for packing and unpacking vectors of strings into
	(argl, args)
*/

/**
	@brief Return the total length of a string array.

	@param argc the length of the string array
	@param argv the string array
	@returns the total number of bytes in all the strings, including the
	    terminating zeros.
*/
static inline size_t argvlen(size_t argc, const char** argv)
{
	size_t l=0;
	for(size_t i=0; i<argc; i++) {
		l+= strlen(argv[i])+1;
	}	
	return l;
}


/**
	@brief Pack a string array into an argument buffer.

	Pack a string array into an argument buffer, which must
	be at least @c argvlen(argc,argv) bytes big.

	@param args the output argument buffer, which must be large enough
	@param argc the length of the input string array
	@param argv the input string array
	@returns the length of the output argument buffer
	@see argvlen
*/
static inline size_t argvpack(void* args, size_t argc, const char** argv)
{
	int argl=0;

	char* pos = args;
	for(size_t i=0; i<argc; i++) {
		const char *s = argv[i];
		while(( *pos++ = *s++ )) argl++;
	}
	return argl+argc;
}

/**
	@brief Return the number of strings packed in an argument buffer.

	This is equal to the number of zero bytes in the buffer.

	@param argl the length of the argument buffer
	@param args the argument buffer
	@returns the number of strings packed in @c args
*/
static inline size_t argscount(int argl, void* args)
{
	int n=0;
	char* a = args;

	for(int i=0; i<argl; i++)
		if(a[i]=='\0') n++;
	return n;	
}

/**
	@brief Unpack a string array from an argument buffer.

	The string array's length must be less than or equal to
	the number of zero bytes in @c args.

	@param argc the length of the output string array
	@param argv the output string array
	@param argl the length of the input argument buffer
	@param args the input argument buffer
	@returns the first location not unpacked

	@see argscount
*/
static inline void* argvunpack(size_t argc, const char** argv, int argl, void* args)
{
	char* a = args;
	for(int i=0;i<argc;i++) {
		argv[i] = a;
		while(*a++); /* skip non-0 */
	}
	return a;
}


/**
	@brief Return the difference in time between two timespecs

	The value of @c struct timespec can be set by using @c clock_gettime().

	@return The number of nanoseconds between the two timespecs.
  */
static inline double timespec_diff(struct timespec* t1, struct timespec* t2)
{
    double T1 = t1->tv_nsec + t1->tv_sec*1E9;
    double T2 = t2->tv_nsec + t2->tv_sec*1E9;
    return T2-T1;
}



/**
	@defgroup exceptions  An execption-like library.

	@brief An exception-like mechanism for C

	Exceptions are supported by many object-oriented languages,
	such as Java, C++ and Python, but not by C. This makes programming
	certain kinds of tasks somewhat complicated. These are
	tasks that can cause a thread to exit back through several layers of
	calls.  For example, a system call may lead to a stack of nested
	calls to execute the code of a driver. If processing is to be
	rolled back to the point of entry to the kernel, all calls in that
	stack need to propagate the error. This makes coding tedious and
	error-prone.

	In C, there is a standard-library facility that can be used to 
	implement such functionality, available by including `<setjmp.h>`.
	The help is in the form of functions @c setjmp() and @c longjmp 
	(and their vatiatons). In this 	library, these standard calls, 
	wrapped by some suitable macros, and using some GNU GCC extensions
	(nested functions),	provide some easy-to-use exception-like programming 
	structures.

	## Examples

	Before we describe the details, we show some examples of the library's
	use.

	A try-block is declared as follows:
	@code
	TRY_WITH(context) {

		ON_ERROR {
			printf("Error in what I was doing\n");
			// After this, execute the finally 
		}

		FINALLY(e) {
			if(e) 
				printf("Continuing after error\n");			
			else
				printf("Finished without error\n");
		}

		// do something here 
		if(error_happens)
			raise_exception(context);

		// or call a function that may call raise_exception()
		do_something_else();

		// If we leave here, FINALLY will be executed 
	}
	@endcode

	For example, one could do the following, to construct a
	composite resource:
	@code
	Resource r1, r2, r3;
	TRY_WITH(context) {
		lock_monitor();
		FINALLY(e) {
			unlock_monitor();
		}

		// This may raise_exception(...)
		r1 = acquire_resource1();
		ON_ERROR {
			release_resource1(r1);
		}

		// This may raise_exception(...)
		r2 = acquire_resource2(r2);

		ON_ERROR {
			release_resource2(r2);
		}
		
		// This may raise_exception(...)
		r3 = acquire_resrouce3(r1, r2);
	}
	return r3;
	@endcode

	## How it works

	The workings are based on the idea of an **exception stack**. The elements
	of this stack are called __exception stack frames__ (ESFs). Each thread should
	have its own exception stack. 
	When a TRY_WITH(...) block begins, a new ESF is pushed to the stack, and the
	block starts to execute. 
	
	Each ESF has two lists of functions of type @ exception_handler, which is defined 
	as  `void (*)(int)`. The nodes for these lists are `struct exception_handler_frame`
	objects.  Initially, the new ESF has empty lists.  The first list is the list
	of **catchers** and the second is the list of **finalizers**.

	As the TRY-block executes, execution reaches `FINALLY()` and `ON_ERROR` blocks.
	When a `FINALLY()` block is reached, a new handler is added to the finalizers list.
	When a `ON_ERROR` block is reached, a new handler is added to the catchers list.

	If execution arrives at the end of the TRY-block, the list of catchers is thrown
	away and the finalizers are executed (in reverse order, that is, last-in-first-out).

	If at some point the function `raise_exception()` is called, execution jumps
	back at the TRY-block at the top of the exception stack. There, each catcher is
	first executed, followed by all the finalizers. At the end, the ESF is popped 
	from the exception stack. Then, if at least one catcher 
	did execute, the exception is considered handled, and execution continues after
	the TRY-block. If however there was no catcher executed, and the exception stack
	is non-empty, then `raise_exception()` is called again, to repeat the process.

	An exception stack is defined simply as a pointer to `struct exception_stack_frame`.
	An __exception context__is a pointer to such a pointer, that is,
	@code
	typedef struct exception_stack_frame** exception_context;
	@endcode
	A context needs to be available to our code in two places: when a 
	`TRY_WITH(context)` block is defined, and when `raise_exception(context)` is
	called.
	
	One can simply define a context as a global variable:
	@code
	// at top level
	struct execution_stack_frame* exception_stack = NULL;
	#define TRY  TRY_WITH(&exception_stack)
	#define RAISE  raise_exception(&exception_stack)

	TRY {
		...
		RAISE;
		...
	}
	@endcode

	In a multi-threaded case, it is necessary to declare one context for each
	thread. This can be done at the TCB, for example.

	@code
	struct TCB {
		....
		struct execution_stack_frame* exception_stack = NULL;	
	}

	#define TRY  TRY_WITH(& CURTHREAD->exception_stack)
	#define RAISE  raise_exception(& CURTHREAD->exception_stack)
	@endcode

	## Performance

	Although setting up a TRY-block is relatively cheap (basically, a call to
	`setjmp` is done), it is better to avoid calling exception handlers.
	So, for very critical pieces of code, one could do
	@code
	TRY {
		lock_mutex();
		ON_ERROR {
			unlock_mutex();
		}

		... // stuff
	
		unlock_mutex();
	}
	@endcode
	instead of the more convenient
	@code
	TRY {
		lock_mutex();
		FINALLY(e) {
			unlock_mutex();
		}

		... // stuff	
	}
	@endcode

	The first case is faster, because it avoids a function call, when there is
	no exception, whereas the second will make a call to the `FINALLY` block, even
	without an exception raised. But, remember: __premature optimization is the
	source of all evil__.

	## Raising from inside an exception handler

	It is perfecly legal and supported to have exceptions raised from inside
	`ON_ERROR` or `FINALLY` blocks (or the functions they call).

	What happens in this case is that the exception handler execution is aborted
	and the processing continues with the next exception handler of the ESF.

	@{
 */


typedef void (*exception_handler)(int);


struct exception_handler_frame {
	exception_handler handler;
	struct exception_handler_frame* next;
};


struct exception_stack_frame
{
	struct exception_stack_frame * next;
	struct exception_handler_frame * catchers;
	struct exception_handler_frame * finalizers;
	jmp_buf jbuf;
};

typedef struct exception_stack_frame** exception_context;

void raise_exception(exception_context context);

void exception_unwind(exception_context context, int errcode);


/**
	@defgroup helpers Helpers for exceptions
	@brief  These are some internal helpers, not part of the public API.
	@internal
	@{
*/
static inline void __exc_push_frame(exception_context context, 
	struct exception_stack_frame* frame)
{
	frame->next = *context;
	*context = frame;
}

static inline struct exception_stack_frame* __exc_try(exception_context context, int errcode)
{
	if(errcode==0) 
		return *context;
	else {
		exception_unwind(context, errcode);
		return NULL;
	}
}

static inline struct exception_stack_frame* __exc_exit_try(exception_context context)
{
	(*context)->catchers = NULL;
	exception_unwind(context, 0);
	return NULL;
}

#define __concatenate_tokens(x,y) x ## y
#define __conc(z,w) __concatenate_tokens(z,w)

/** @} */

#define TRY_WITH(context) \
	struct exception_stack_frame __conc(__try_,__LINE__) = \
		{ .catchers=NULL, .finalizers=NULL };\
	__exc_push_frame((context), & __conc(__try_ , __LINE__) ); \
	int __conc(__exception_,__LINE__) = setjmp( __conc(__try_,__LINE__).jbuf); \
	__atomic_signal_fence(__ATOMIC_SEQ_CST);\
	for(struct exception_stack_frame* __frame = \
		__exc_try((context), __conc(__exception_, __LINE__) );\
		__frame != NULL ; \
		__frame = __exc_exit_try(context)) \

#define FINALLY(excname)  \
	struct exception_handler_frame __conc(__xframe_,__LINE__);\
	__conc(__xframe_, __LINE__).next = __frame->finalizers; \
	__frame->finalizers = & __conc(__xframe_, __LINE__) ;\
	auto void __conc(__action_, __LINE__) (int); \
	__conc(__xframe_,__LINE__).handler = __conc(__action_,__LINE__);\
	__atomic_signal_fence(__ATOMIC_SEQ_CST);\
	void __conc(__action_,__LINE__)(int excname) \


#define ON_ERROR  \
	struct exception_handler_frame __conc(__xframe_,__LINE__);\
	__conc(__xframe_, __LINE__).next = __frame->catchers; \
	__frame->catchers = & __conc(__xframe_, __LINE__) ;\
	auto void __conc(__action_, __LINE__) (int); \
	__conc(__xframe_,__LINE__).handler = __conc(__action_,__LINE__);\
	__atomic_signal_fence(__ATOMIC_SEQ_CST);\
	void __conc(__action_,__LINE__)(int __dummy) \


/** @}  exceptions */


/** @} utilities */


#endif