/**
 * seidel-2d.h: This file is part of the PolyBench/C 3.2 test suite.
 *
 *
 * Contact: Louis-Noel Pouchet <pouchet@cse.ohio-state.edu>
 * Web address: http://polybench.sourceforge.net
 */
#ifndef SEIDEL_2D_H
# define SEIDEL_2D_H

# define SMALL_DATASET
# define DATA_TYPE int

/* Default to STANDARD_DATASET. */
# if !defined(MINI_DATASET) && !defined(SMALL_DATASET) && !defined(LARGE_DATASET) && !defined(EXTRALARGE_DATASET)
#  define STANDARD_DATASET
# endif

/* Do not define anything if the user manually defines the size. */
# if !defined(TSTEPS) && ! defined(N)
/* Define the possible dataset sizes. */
#  ifdef MINI_DATASET
#   define TSTEPS 2
#   define N 32
#  endif

#  ifdef SMALL_DATASET
#   define TSTEPS 10
#   define N 100
#  endif

#  ifdef STANDARD_DATASET /* Default if unspecified. */
#   define TSTEPS 20
#   define N 1000
#  endif

#  ifdef LARGE_DATASET
#   define TSTEPS 20
#   define N 2000
#  endif

#  ifdef EXTRALARGE_DATASET
#   define TSTEPS 100
#   define N 4000
#  endif
# endif /* !N */

# define _PB_TSTEPS POLYBENCH_LOOP_BOUND(TSTEPS,tsteps)
# define _PB_N POLYBENCH_LOOP_BOUND(N,n)

# ifndef DATA_TYPE
#  define DATA_TYPE double
#  define DATA_PRINTF_MODIFIER "%0.2lf "
# endif


#endif /* !SEIDEL_2D */
