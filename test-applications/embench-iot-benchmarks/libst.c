/* BEEBS st benchmark

   This version, copyright (C) 2014-2019 Embecosm Limited and University of
   Bristol

   Contributor James Pallister <james.pallister@bristol.ac.uk>
   Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>

   This file is part of Embench and was formerly part of the Bristol/Embecosm
   Embedded Benchmark Suite.

   SPDX-License-Identifier: GPL-3.0-or-later */

/* stats.c */

/* 2012/09/28, Jan Gustafsson <jan.gustafsson@mdh.se>
 * Changes:
 *  - time is only enabled if the POUT flag is set
 *  - st.c:30:1:  main () warning: type specifier missing, defaults to 'int':
 *    fixed
 */


/* 2011/10/18, Benedikt Huber <benedikt@vmars.tuwien.ac.at>
 * Changes:
 *  - Measurement and Printing the Results is only enabled if the POUT flag is
 *    set
 *  - Added Prototypes for InitSeed and RandomInteger
 *  - Changed return type of InitSeed from 'missing (default int)' to 'void'
 */

#include <stdio.h>
#include <math.h>
//#include "support.h"

/* BEEBS local library variants header

   Copyright (C) 2019 Embecosm Limited.

   Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>

   This file is part of Embench and was formerly part of the Bristol/Embecosm
   Embedded Benchmark Suite.

   SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef BEEBSC_H
#define BEEBSC_H

#include <stddef.h>

/* BEEBS fixes RAND_MAX to its lowest permitted value, 2^15-1 */

#ifdef RAND_MAX
#undef RAND_MAX
#endif
#define RAND_MAX ((1U << 15) - 1)

/* Common understanding of a "small value" (epsilon) for floating point
   comparisons. */

#define VERIFY_DOUBLE_EPS 1.0e-13
#define VERIFY_FLOAT_EPS 1.0e-5

/* Simplified assert.

   The full complexity of assert is not needed for a benchmark. See the
   discussion at:

   https://lists.librecores.org/pipermail/embench/2019-August/000007.html 

   This function just*/

#define assert_beebs(expr) { if (!(expr)) exit (1); }

#define float_eq_beebs(exp, actual) (fabsf(exp - actual) < VERIFY_FLOAT_EPS)
#define float_neq_beebs(exp, actual) !float_eq_beebs(exp, actual)
#define double_eq_beebs(exp, actual) (fabs(exp - actual) < VERIFY_DOUBLE_EPS)
#define double_neq_beebs(exp, actual) !double_eq_beebs(exp, actual)

/* Local simplified versions of library functions */

int rand_beebs (void);
void srand_beebs (unsigned int new_seed);

void init_heap_beebs (void *heap, const size_t heap_size);
int check_heap_beebs (void *heap);
void *malloc_beebs (size_t size);
void *calloc_beebs (size_t nmemb, size_t size);
void *realloc_beebs (void *ptr, size_t size);
void free_beebs (void *ptr);
#endif /* BEEBSC_H */


/*
   Local Variables:
   mode: C
   c-file-style: "gnu"
   End:
*/

/* This scale factor will be changed to equalise the runtime of the
   benchmarks. */
#define LOCAL_SCALE_FACTOR 13

#define MAX 100

void InitSeed (void);
int RandomInteger ();
void Initialize (double[]);
void Calc_Sum_Mean (double[], double *, double *);
void Calc_Var_Stddev (double[], double, double *, double *);
void Calc_LinCorrCoef (double[], double[], double, double);


/* Statistics Program:
 * This program computes for two arrays of numbers the sum, the
 * mean, the variance, and standard deviation.  It then determines the
 * correlation coefficient between the two arrays.
 */

int Seed;
double ArrayA[MAX], ArrayB[MAX];
double SumA, SumB;
double Coef;


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
  int i;

  for (i = 0; i < rpt; i++)
    {
      double MeanA, MeanB, VarA, VarB, StddevA, StddevB /*, Coef */ ;

      InitSeed ();

      Initialize (ArrayA);
      Calc_Sum_Mean (ArrayA, &SumA, &MeanA);
      Calc_Var_Stddev (ArrayA, MeanA, &VarA, &StddevA);

      Initialize (ArrayB);
      Calc_Sum_Mean (ArrayB, &SumB, &MeanB);
      Calc_Var_Stddev (ArrayB, MeanB, &VarB, &StddevB);

      /* Coef will have to be used globally in Calc_LinCorrCoef since it would
         be beyond the 6 registers used for passing parameters
       */
      Calc_LinCorrCoef (ArrayA, ArrayB, MeanA, MeanB /*, &Coef */ );
    }

  return 0;
}


void
InitSeed ()
/*
 * Initializes the seed used in the random number generator.
 */
{
  Seed = 0;
}


void
Calc_Sum_Mean (double Array[], double *Sum, double *Mean)
{
  int i;

  *Sum = 0;
  for (i = 0; i < MAX; i++)
    *Sum += Array[i];
  *Mean = *Sum / MAX;
}


double
Square (double x)
{
  return x * x;
}


void
Calc_Var_Stddev (double Array[], double Mean, double *Var, double *Stddev)
{
  int i;
  double diffs;

  diffs = 0.0;
  for (i = 0; i < MAX; i++)
    diffs += Square (Array[i] - Mean);
  *Var = diffs / MAX;
  *Stddev = sqrt (*Var);
}


void
Calc_LinCorrCoef (double ArrayA[], double ArrayB[], double MeanA,
		  double MeanB /*, Coef */ )
{
  int i;
  double numerator, Aterm, Bterm;

  numerator = 0.0;
  Aterm = Bterm = 0.0;
  for (i = 0; i < MAX; i++)
    {
      numerator += (ArrayA[i] - MeanA) * (ArrayB[i] - MeanB);
      Aterm += Square (ArrayA[i] - MeanA);
      Bterm += Square (ArrayB[i] - MeanB);
    }

  /* Coef used globally */
  Coef = numerator / (sqrt (Aterm) * sqrt (Bterm));
}



void
Initialize (double Array[])
/*
 * Intializes the given array with random integers.
 */
{
  register int i;

  for (i = 0; i < MAX; i++)
    Array[i] = i + RandomInteger () / 8095.0;
}


int
RandomInteger ()
/*
 * Generates random integers between 0 and 8095
 */
{
  Seed = ((Seed * 133) + 81) % 8095;
  return (Seed);
}

int
verify_benchmark (int unused)
{
  double expSumA = 4999.00247066090196;
  double expSumB = 4996.84311303273534;
  double expCoef = 0.999900054853619324;

  return double_eq_beebs(expSumA, SumA)
    && double_eq_beebs(expSumB, SumB)
    && double_eq_beebs(expCoef, Coef);
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


/*
   Local Variables:
   mode: C
   c-file-style: "gnu"
   End:
*/
