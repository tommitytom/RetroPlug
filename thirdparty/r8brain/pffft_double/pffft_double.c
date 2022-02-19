/* Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )
   Copyright (c) 2020  Hayati Ayguen ( h_ayguen@web.de )
   Copyright (c) 2020  Dario Mambro ( dario.mambro@gmail.com )

   Based on original fortran 77 code from FFTPACKv4 from NETLIB
   (http://www.netlib.org/fftpack), authored by Dr Paul Swarztrauber
   of NCAR, in 1985.

   As confirmed by the NCAR fftpack software curators, the following
   FFTPACKv5 license applies to FFTPACKv4 sources. My changes are
   released under the same terms.

   FFTPACK license:

   http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html

   Copyright (c) 2004 the University Corporation for Atmospheric
   Research ("UCAR"). All rights reserved. Developed by NCAR's
   Computational and Information Systems Laboratory, UCAR,
   www.cisl.ucar.edu.

   Redistribution and use of the Software in source and binary forms,
   with or without modification, is permitted provided that the
   following conditions are met:

   - Neither the names of NCAR's Computational and Information Systems
   Laboratory, the University Corporation for Atmospheric Research,
   nor the names of its sponsors or contributors may be used to
   endorse or promote products derived from this Software without
   specific prior written permission.  

   - Redistributions of source code must retain the above copyright
   notices, this list of conditions, and the disclaimer below.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the disclaimer below in the
   documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
   SOFTWARE.


   PFFFT : a Pretty Fast FFT.

   This file is largerly based on the original FFTPACK implementation, modified in
   order to take advantage of SIMD instructions of modern CPUs.
*/

/*
   NOTE: This file is adapted from Julien Pommier's original PFFFT,
   which works on 32 bit floating point precision using SSE instructions,
   to work with 64 bit floating point precision using AVX instructions.
   Author: Dario Mambro @ https://github.com/unevens/pffft
*/

#include "pffft_double.h"

/* detect compiler flavour */
#if defined(_MSC_VER)
#  define COMPILER_MSVC
#elif defined(__GNUC__)
#  define COMPILER_GCC
#endif

#ifdef COMPILER_MSVC
#  ifndef _USE_MATH_DEFINES
#    define _USE_MATH_DEFINES
#  endif
#  include <malloc.h>
#else
#  include <alloca.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#if defined(COMPILER_GCC)
#  define ALWAYS_INLINE(return_type) inline return_type __attribute__ ((always_inline))
#  define NEVER_INLINE(return_type) return_type __attribute__ ((noinline))
#  define RESTRICT __restrict
#  define VLA_ARRAY_ON_STACK(type__, varname__, size__) type__ varname__[size__];
#elif defined(COMPILER_MSVC)
#  define ALWAYS_INLINE(return_type) __forceinline return_type
#  define NEVER_INLINE(return_type) __declspec(noinline) return_type
#  define RESTRICT __restrict
#  define VLA_ARRAY_ON_STACK(type__, varname__, size__) type__ *varname__ = (type__*)_alloca(size__ * sizeof(type__))
#endif


#ifdef COMPILER_MSVC
#pragma warning( disable : 4244 4305 4204 4456 )
#endif

/* 
   vector support macros: the rest of the code is independant of
   AVX -- adding support for other platforms with 4-element
   vectors should be limited to these macros 
*/
#include "simd/pf_double.h"

/* have code comparable with this definition */
#define float double
#define SETUP_STRUCT               PFFFTD_Setup
#define FUNC_NEW_SETUP             pffftd_new_setup
#define FUNC_DESTROY               pffftd_destroy_setup
#define FUNC_TRANSFORM_UNORDRD     pffftd_transform
#define FUNC_TRANSFORM_ORDERED     pffftd_transform_ordered
#define FUNC_ZREORDER              pffftd_zreorder
#define FUNC_ZCONVOLVE_ACCUMULATE  pffftd_zconvolve_accumulate
#define FUNC_ZCONVOLVE_NO_ACCU     pffftd_zconvolve_no_accu

#define FUNC_ALIGNED_MALLOC        pffftd_aligned_malloc
#define FUNC_ALIGNED_FREE          pffftd_aligned_free
#define FUNC_SIMD_SIZE             pffftd_simd_size
#define FUNC_SIMD_ARCH             pffftd_simd_arch
#define FUNC_VALIDATE_SIMD_A       validate_pffftd_simd
#define FUNC_VALIDATE_SIMD_EX      validate_pffftd_simd_ex

#define FUNC_CPLX_FINALIZE         pffftd_cplx_finalize
#define FUNC_CPLX_PREPROCESS       pffftd_cplx_preprocess
#define FUNC_REAL_PREPROCESS_4X4   pffftd_real_preprocess_4x4
#define FUNC_REAL_PREPROCESS       pffftd_real_preprocess
#define FUNC_REAL_FINALIZE_4X4     pffftd_real_finalize_4x4
#define FUNC_REAL_FINALIZE         pffftd_real_finalize
#define FUNC_TRANSFORM_INTERNAL    pffftd_transform_internal

#define FUNC_COS  cos
#define FUNC_SIN  sin

#include "pffft_priv_impl.h"

/* moved from "pffft_common.c" */

/* SSE and co like 16-bytes aligned pointers
 * with a 64-byte alignment, we are even aligned on L2 cache lines... */
#define MALLOC_V4SF_ALIGNMENT 64
#define MALLOC_V4SF_SIZE (MALLOC_V4SF_ALIGNMENT+sizeof(void*))

static void * Valigned_malloc(size_t nb_bytes) {
  void *p, *p0 = malloc(nb_bytes + MALLOC_V4SF_SIZE);
  if (!p0) return (void *) 0;
  p = (void *) (((uintptr_t) p0 + MALLOC_V4SF_SIZE) &
    ~(uintptr_t) (MALLOC_V4SF_ALIGNMENT-1));
  *((void **) p - 1) = p0;
  return p;
}

static void Valigned_free(void *p) {
  if (p) free(*((void **) p - 1));
}


static int next_power_of_two(int N) {
  /* https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2 */
  /* compute the next highest power of 2 of 32-bit v */
  unsigned v = N;
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

static int is_power_of_two(int N) {
  /* https://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2 */
  int f = N && !(N & (N - 1));
  return f;
}

static int min_fft_size(pffft_transform_t transform) {
  /* unfortunately, the fft size must be a multiple of 16 for complex FFTs
     and 32 for real FFTs -- a lot of stuff would need to be rewritten to
     handle other cases (or maybe just switch to a scalar fft, I don't know..) */
  int simdSz = pffftd_simd_size();
  if (transform == PFFFT_REAL)
    return ( 2 * simdSz * simdSz );
  else if (transform == PFFFT_COMPLEX)
    return ( simdSz * simdSz );
  else
    return 1;
}

void *pffftd_aligned_malloc(size_t nb_bytes) { return Valigned_malloc(nb_bytes); }
void pffftd_aligned_free(void *p) { Valigned_free(p); }
int pffftd_next_power_of_two(int N) { return next_power_of_two(N); }
int pffftd_is_power_of_two(int N) { return is_power_of_two(N); }
int pffftd_min_fft_size(pffft_transform_t transform) { return min_fft_size(transform); }
