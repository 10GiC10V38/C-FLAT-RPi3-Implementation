/*
 * This benchmark simulates the search in a TAR archive
 * for a set of filenames
 *
 * Created by Julian Kunkel for Embench-iot
 * Licensed under MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

//#include "support.h"

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

#define LOCAL_SCALE_FACTOR 47

// number of files in the archive
#define ARCHIVE_FILES 35

#define N_SEARCHES 5

/* BEEBS heap is just an array */
/* 8995 = sizeof(tar_header_t) * ARCHIVE_FILES */
#define roundup(d, u) ((((d)+(u))/(u))*(u))
#define HEAP_SIZE roundup(8995, sizeof(void *))
static char heap[HEAP_SIZE];

void
initialise_benchmark (void)
{
}


static int benchmark_body (int rpt);

void
warm_caches (int  heat)
{
  benchmark_body (heat);
  return;
}


int
benchmark (void)
{
  return benchmark_body (LOCAL_SCALE_FACTOR * CPU_MHZ);
}

// this is the basic TAR header format which is in ASCII
typedef struct {
  char filename[100];
  char mode[8];  // file mode
  char uID[8];   // user id
  char gID[8];   // group id
  char size[12]; // in bytes octal base
  char mtime[12]; // numeric Unix time format (octal)
  char checksum[8]; // for the header, ignored herew2
  char isLink;
  char linkedFile[100];
} tar_header_t;


static int __attribute__ ((noinline))
benchmark_body (int rpt)
{
  int i, j, p;
  tar_header_t * hdr;
  int found;

  for (j = 0; j < rpt; j++) {
    init_heap_beebs ((void *) heap, HEAP_SIZE);

    // always create ARCHIVE_FILES files in the archive
    int files = ARCHIVE_FILES;
    hdr = malloc_beebs(sizeof(tar_header_t) * files);
    for (i = 0; i < files; i++){
      // create record
      tar_header_t * c = & hdr[i];
      // initialize here for cache efficiency reasons
      memset(c, 0, sizeof(tar_header_t));
      int flen = 5 + i % 94; // vary file lengths
      c->isLink = '0';
      for(p = 0; p < flen; p++){
        c->filename[p] = rand_beebs() % 26 + 65;
      }
      c->size[0] = '0';
    }

    found = 0; // number of times a file was found
    // actual benchmark, strcmp with a set of N_SEARCHES files
    // the memory access here is chosen inefficiently on purpose
    for (p = 0; p < N_SEARCHES; p++){
      // chose the position of the file to search for from the mid of the list
      char * search = hdr[(p + ARCHIVE_FILES/2) % ARCHIVE_FILES].filename;

      // for each filename iterate through all files until found
      for (i = 0; i < files; i++){
        tar_header_t * cur = & hdr[i];
        // implementation of strcmp
        char *c1;
        char *c2;
        for (c1 = hdr[i].filename, c2 = search; (*c1 != '\0' && *c2 != '\0' && *c1 == *c2) ; c1++, c2++);
        // complete match?
        if(*c1 == '\0' && *c2 == '\0'){
          found++;
          break;
        }
      }
    }
    free_beebs(hdr);
  }

  return found == N_SEARCHES;
}


int
verify_benchmark (int r)
{
  return r == 1;
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

}				/* main () */

