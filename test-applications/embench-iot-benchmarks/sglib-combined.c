/* BEEBS arraysort benchmark

   This version, copyright (C) 2014-2019 Embecosm Limited and University of
   Bristol

   Contributor James Pallister <james.pallister@bristol.ac.uk>
   Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>

   This file is part of Embench and was formerly part of the Bristol/Embecosm
   Embedded Benchmark Suite.

   SPDX-License-Identifier: GPL-3.0-or-later */

#include <string.h>
//#include "support.h"
#include "sglib.h"
#include <stdio.h> 

/*************** beebsc.c ********************/

/* Seed for the random number generator */

static long int seed = 0;

/* Heap records and sane initial values */

static void *heap_ptr = NULL;
static void *heap_end = NULL;
static size_t heap_requested = 0;


/* Yield a sequence of random numbers in the range [0, 2^15-1].

   long int is guaranteed to be at least 32 bits. The seed only ever uses 31
   bits (so is positive).

   For BEEBS this gets round different operating systems using different
   multipliers and offsets and RAND_MAX variations. */

int
rand_beebs (void)
{
  seed = (seed * 1103515245L + 12345) & ((1UL << 31) - 1);
  return (int) (seed >> 16);
}


/* Initialize the random number generator */

void
srand_beebs (unsigned int new_seed)
{
  seed = (long int) new_seed;
}


/* Initialize the BEEBS heap pointers. Note that the actual memory block is
   in the caller code. */

void
init_heap_beebs (void *heap, size_t heap_size)
{
  heap_ptr = (void *) heap;
  heap_end = (void *) ((char *) heap_ptr + heap_size);
  heap_requested = 0;
}


/* Report if malloc ever failed.

   Return non-zero (TRUE) if malloc did not reqest more than was available
   since the last call to init_heap_beebs, zero (FALSE) otherwise. */

int
check_heap_beebs (void *heap)
{
  return ((void *) ((char *) heap + heap_requested) <= heap_end);
}


/* BEEBS version of malloc.

   This is primarily to reduce library and OS dependencies. Malloc is
   generally not used in embedded code, or if it is, only in well defined
   contexts to pre-allocate a fixed amount of memory. So this simplistic
   implementation is just fine.

   Note in particular the assumption that memory will never be freed! */

void *
malloc_beebs (size_t size)
{
  void *new_ptr = heap_ptr;

  heap_requested += size;

  if (((void *) ((char *) heap_ptr + size) > heap_end) || (0 == size))
    return NULL;
  else
    {
      heap_ptr = (void *) ((char *) heap_ptr + size);
      return new_ptr;
    }
}


/* BEEBS version of calloc.

   Implement as wrapper for malloc */

void *
calloc_beebs (size_t nmemb, size_t size)
{
  void *new_ptr = malloc_beebs (nmemb * size);

  /* Calloc is defined to zero the memory. OK to use a function here, because
     it will be handled specially by the compiler anyway. */

  if (NULL != new_ptr)
    memset (new_ptr, 0, nmemb * size);

  return new_ptr;
}


/* BEEBS version of realloc.

   This is primarily to reduce library and OS dependencies. We just have to
   allocate new memory and copy stuff across. */

void *
realloc_beebs (void *ptr, size_t size)
{
  void *new_ptr = heap_ptr;

  heap_requested += size;

  if (((void *) ((char *) heap_ptr + size) > heap_end) || (0 == size))
    return NULL;
  else
    {
      heap_ptr = (void *) ((char *) heap_ptr + size);

      /* This is clunky, since we don't know the size of the original
         pointer. However it is a read only action and we know it must
         be big enough if we right off the end, or we couldn't have
         allocated here. If the size is smaller, it doesn't matter. */

      if (NULL != ptr)
	{
	  size_t i;

	  for (i = 0; i < size; i++)
	    ((char *) new_ptr)[i] = ((char *) ptr)[i];
	}

      return new_ptr;
    }
}


/* BEEBS version of free.

   For our simplified version of memory handling, free can just do nothing. */

void
free_beebs (void *ptr __attribute__ ((unused)))
{
}


/*
   Local Variables:
   mode: C
   c-file-style: "gnu"
   End:
*/

/******************************************************/

/* This scale factor will be changed to equalise the runtime of the
   benchmarks. */
#define LOCAL_SCALE_FACTOR 29

/* BEEBS heap is just an array */

#define HEAP_SIZE 8192
static char heap[HEAP_SIZE] __attribute__((aligned));

/* General array to sort for all ops */

static const int array[100] = {
  14, 66, 12, 41, 86, 69, 19, 77, 68, 38,
  26, 42, 37, 23, 17, 29, 55, 13, 90, 92,
  76, 99, 10, 54, 57, 83, 40, 44, 75, 33,
  24, 28, 80, 18, 78, 32, 93, 89, 52, 11,
  21, 96, 50, 15, 48, 63, 87, 20,  8, 85,
  43, 16, 94, 88, 53, 84, 74, 91, 67, 36,
  95, 61, 64,  5, 30, 82, 72, 46, 59,  9,
   7,  3, 39, 31,  4, 73, 70, 60, 58, 81,
  56, 51, 45,  1,  6, 49, 27, 47, 34, 35,
  62, 97,  2, 79, 98, 25, 22, 65, 71,  0
};

/* Array quicksort declarations */

int array2[100];

/* Doubly linked list declarations */

typedef struct dllist
{
  int i;
  struct dllist *ptr_to_next;
  struct dllist *ptr_to_previous;
} dllist;

#define DLLIST_COMPARATOR(e1, e2) (e1->i - e2->i)

SGLIB_DEFINE_DL_LIST_PROTOTYPES (dllist, DLLIST_COMPARATOR, ptr_to_previous,
				 ptr_to_next)
SGLIB_DEFINE_DL_LIST_FUNCTIONS (dllist, DLLIST_COMPARATOR, ptr_to_previous,
				ptr_to_next)

dllist *the_list;

/* Hash table declarations */

#define HASH_TAB_SIZE  20

typedef struct ilist
{
  int i;
  struct ilist *next;
} ilist;

ilist *htab[HASH_TAB_SIZE];

#define ILIST_COMPARATOR(e1, e2)    (e1->i - e2->i)

unsigned int
ilist_hash_function (ilist * e)
{
  return (e->i);
}

SGLIB_DEFINE_LIST_PROTOTYPES (ilist, ILIST_COMPARATOR, next)
SGLIB_DEFINE_LIST_FUNCTIONS (ilist, ILIST_COMPARATOR, next)
SGLIB_DEFINE_HASHED_CONTAINER_PROTOTYPES (ilist, HASH_TAB_SIZE,
					  ilist_hash_function)
SGLIB_DEFINE_HASHED_CONTAINER_FUNCTIONS (ilist, HASH_TAB_SIZE,
					 ilist_hash_function)

/* Queue declarations */

#define MAX_PARAMS 101

typedef struct iq
{
  int a[MAX_PARAMS];
  int i, j;
} iq;

SGLIB_DEFINE_QUEUE_FUNCTIONS (iq, int, a, i, j, MAX_PARAMS)

/* RB Tree declarations */

typedef struct rbtree
{
  int n;
  char color_field;
  struct rbtree *left;
  struct rbtree *right;
} rbtree;

#define CMPARATOR(x,y) ((x->n)-(y->n))

SGLIB_DEFINE_RBTREE_PROTOTYPES (rbtree, left, right, color_field, CMPARATOR)
SGLIB_DEFINE_RBTREE_FUNCTIONS (rbtree, left, right, color_field, CMPARATOR)


int
verify_benchmark (int res)
{
  static const int array_exp[100] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99
  };

  int i = 0;
  dllist *l;
  struct ilist ii, *nn;

  /* Doubly linked list check */

  for (l = sglib_dllist_get_first (the_list); l != NULL; l = l->ptr_to_next)
    {
      if (l->i != i)
	return 0;
      i++;
    }

  /* Hashtable check */

  for (i = 0; i < 100; i++)
    {
      ii.i = array[i];
      nn = sglib_hashed_ilist_find_member (htab, &ii);

      if ((nn == NULL) || (nn->i != array[i]))
	return 0;
    }

  return (15050 == res) && check_heap_beebs ((void *) heap)
    && (0 == memcmp (array2, array_exp, 100 * sizeof (array[0])));
}


void
initialise_benchmark (void)
{
}



static int benchmark_body (int  rpt);

void
warm_caches (int  heat)
{
  int  res = benchmark_body (heat);

  return;
}


int
benchmark (void)
{
  return benchmark_body (LOCAL_SCALE_FACTOR * CPU_MHZ);
}


static int __attribute__ ((noinline))
benchmark_body (int rpt)
{
  volatile int cnt;
  int i;

  for (i = 0; i < rpt; i++)
    {
      int i;
      dllist *l;
      struct ilist ii, *nn, *ll;
      struct sglib_hashed_ilist_iterator it;
      int ai, aj, n;
      int a[MAX_PARAMS];
      struct rbtree e, *t, *the_tree, *te;
      struct sglib_rbtree_iterator it2;

      /* Array quicksort */

      memcpy (array2, array, 100 * sizeof (array[0]));
      SGLIB_ARRAY_SINGLE_QUICK_SORT (int, array2, 100,
				     SGLIB_NUMERIC_COMPARATOR);

      /* Doubly linked list */

      init_heap_beebs ((void *) heap, HEAP_SIZE);
      the_list = NULL;

      for (i = 0; i < 100; ++i)
	{
	  l = malloc_beebs (sizeof (dllist));
	  l->i = array[i];
	  sglib_dllist_add (&the_list, l);
	}

      sglib_dllist_sort (&the_list);

      cnt = 0;

      for (l = sglib_dllist_get_first (the_list); l != NULL;
	   l = l->ptr_to_next)
	cnt++;

      /* Hash table */

      sglib_hashed_ilist_init (htab);

      for (i = 0; i < 100; i++)
	{
	  ii.i = array[i];
	  if (sglib_hashed_ilist_find_member (htab, &ii) == NULL)
	    {
	      nn = malloc_beebs (sizeof (struct ilist));
	      nn->i = array[i];
	      sglib_hashed_ilist_add (htab, nn);
	    }
	}

      for (ll = sglib_hashed_ilist_it_init (&it, htab);
	   ll != NULL; ll = sglib_hashed_ilist_it_next (&it))
	{
	  cnt++;
	}

      /* Queue */

      // echo parameters using a queue
      SGLIB_QUEUE_INIT (int, a, ai, aj);
      for (i = 0; i < 100; i++)
	{
	  n = array[i];
	  SGLIB_QUEUE_ADD (int, a, n, ai, aj, MAX_PARAMS);
	}
      while (!SGLIB_QUEUE_IS_EMPTY (int, a, ai, aj))
	{
	  cnt += SGLIB_QUEUE_FIRST_ELEMENT (int, a, ai, aj);
	  SGLIB_QUEUE_DELETE (int, a, ai, aj, MAX_PARAMS);
	}

      // print parameters in descending order
      SGLIB_HEAP_INIT (int, a, ai);
      for (i = 0; i < 100; i++)
	{
	  n = array[i];
	  SGLIB_HEAP_ADD (int, a, n, ai, MAX_PARAMS,
			  SGLIB_NUMERIC_COMPARATOR);
	}
      while (!SGLIB_HEAP_IS_EMPTY (int, a, ai))
	{
	  cnt += SGLIB_HEAP_FIRST_ELEMENT (int, a, ai);
	  SGLIB_HEAP_DELETE (int, a, ai, MAX_PARAMS,
			     SGLIB_NUMERIC_COMPARATOR);
	}

      /* RB Tree */

      the_tree = NULL;
      for (i = 0; i < 100; i++)
	{
	  e.n = array[i];
	  if (sglib_rbtree_find_member (the_tree, &e) == NULL)
	    {
	      t = malloc_beebs (sizeof (struct rbtree));
	      t->n = array[i];
	      sglib_rbtree_add (&the_tree, t);
	    }
	}

      for (te = sglib_rbtree_it_init_inorder (&it2, the_tree);
	   te != NULL; te = sglib_rbtree_it_next (&it2))
	{
	  cnt += te->n;
	}
    }

  return cnt;
}

int __attribute__ ((used))
main (int argc __attribute__ ((unused)),
      char *argv[] __attribute__ ((unused)))
{
  int i;
  volatile int result;
  int correct;

  //initialise_board ();
  initialise_benchmark ();
  warm_caches (0);

  //start_trigger ();
  result = benchmark ();
  //stop_trigger ();

  /* bmarks that use arrays will check a global array rather than int result */

  correct = verify_benchmark (result);

  printf("%d\n", correct);

}	

/*
   Local Variables:
   mode: C
   c-file-style: "gnu"
   End:
*/
