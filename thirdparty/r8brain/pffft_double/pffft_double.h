/* Copyright (c) 2013  Julien Pommier ( pommier@modartt.com ) 

   Based on original fortran 77 code from FFTPACKv4 from NETLIB,
   authored by Dr Paul Swarztrauber of NCAR, in 1985.

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
*/
/*
   NOTE: This file is adapted from Julien Pommier's original PFFFT,
   which works on 32 bit floating point precision using SSE instructions,
   to work with 64 bit floating point precision using AVX instructions.
   Author: Dario Mambro @ https://github.com/unevens/pffft
*/
/*
   PFFFT : a Pretty Fast FFT.

   This is basically an adaptation of the single precision fftpack
   (v4) as found on netlib taking advantage of SIMD instruction found
   on cpus such as intel x86 (SSE1), powerpc (Altivec), and arm (NEON).
   
   For architectures where no SIMD instruction is available, the code
   falls back to a scalar version.  

   Restrictions: 

   - 1D transforms only, with 64-bit double precision.

   - supports only transforms for inputs of length N of the form
   N=(2^a)*(3^b)*(5^c), a >= 5, b >=0, c >= 0 (32, 48, 64, 96, 128,
   144, 160, etc are all acceptable lengths). Performance is best for
   128<=N<=8192.

   - all (double*) pointers in the functions below are expected to
   have an "simd-compatible" alignment, that is 32 bytes on x86 and
   powerpc CPUs.
  
   You can allocate such buffers with the functions
   pffft_aligned_malloc / pffft_aligned_free (or with stuff like
   posix_memalign..)

*/

#ifndef PFFFT_DOUBLE_H
#define PFFFT_DOUBLE_H

#include <stddef.h> /* for size_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  /* opaque struct holding internal stuff (precomputed twiddle factors)
     this struct can be shared by many threads as it contains only
     read-only data.  
  */
  typedef struct PFFFTD_Setup PFFFTD_Setup;

#ifndef PFFFT_COMMON_ENUMS
#define PFFFT_COMMON_ENUMS

  /* direction of the transform */
  typedef enum { PFFFT_FORWARD, PFFFT_BACKWARD } pffft_direction_t;
  
  /* type of transform */
  typedef enum { PFFFT_REAL, PFFFT_COMPLEX } pffft_transform_t;

#endif

  /*
    prepare for performing transforms of size N -- the returned
    PFFFTD_Setup structure is read-only so it can safely be shared by
    multiple concurrent threads. 
  */
  PFFFTD_Setup *pffftd_new_setup(int N, pffft_transform_t transform);
  void pffftd_destroy_setup(PFFFTD_Setup *);
  /* 
     Perform a Fourier transform , The z-domain data is stored in the
     most efficient order for transforming it back, or using it for
     convolution. If you need to have its content sorted in the
     "usual" way, that is as an array of interleaved complex numbers,
     either use pffft_transform_ordered , or call pffft_zreorder after
     the forward fft, and before the backward fft.

     Transforms are not scaled: PFFFT_BACKWARD(PFFFT_FORWARD(x)) = N*x.
     Typically you will want to scale the backward transform by 1/N.
     
     The 'work' pointer should point to an area of N (2*N for complex
     fft) doubles, properly aligned. If 'work' is NULL, then stack will
     be used instead (this is probably the best strategy for small
     FFTs, say for N < 16384). Threads usually have a small stack, that
     there's no sufficient amount of memory, usually leading to a crash!
     Use the heap with pffft_aligned_malloc() in this case.

     input and output may alias.
  */
  void pffftd_transform(PFFFTD_Setup *setup, const double *input, double *output, double *work, pffft_direction_t direction);

  /* 
     Similar to pffft_transform, but makes sure that the output is
     ordered as expected (interleaved complex numbers).  This is
     similar to calling pffft_transform and then pffft_zreorder.
     
     input and output may alias.
  */
  void pffftd_transform_ordered(PFFFTD_Setup *setup, const double *input, double *output, double *work, pffft_direction_t direction);

  /* 
     call pffft_zreorder(.., PFFFT_FORWARD) after pffft_transform(...,
     PFFFT_FORWARD) if you want to have the frequency components in
     the correct "canonical" order, as interleaved complex numbers.
     
     (for real transforms, both 0-frequency and half frequency
     components, which are real, are assembled in the first entry as
     F(0)+i*F(n/2+1). Note that the original fftpack did place
     F(n/2+1) at the end of the arrays).
     
     input and output should not alias.
  */
  void pffftd_zreorder(PFFFTD_Setup *setup, const double *input, double *output, pffft_direction_t direction);

  /* 
     Perform a multiplication of the frequency components of dft_a and
     dft_b and accumulate them into dft_ab. The arrays should have
     been obtained with pffft_transform(.., PFFFT_FORWARD) and should
     *not* have been reordered with pffft_zreorder (otherwise just
     perform the operation yourself as the dft coefs are stored as
     interleaved complex numbers).
     
     the operation performed is: dft_ab += (dft_a * fdt_b)*scaling
     
     The dft_a, dft_b and dft_ab pointers may alias.
  */
  void pffftd_zconvolve_accumulate(PFFFTD_Setup *setup, const double *dft_a, const double *dft_b, double *dft_ab, double scaling);

  /* 
     Perform a multiplication of the frequency components of dft_a and
     dft_b and put result in dft_ab. The arrays should have
     been obtained with pffft_transform(.., PFFFT_FORWARD) and should
     *not* have been reordered with pffft_zreorder (otherwise just
     perform the operation yourself as the dft coefs are stored as
     interleaved complex numbers).

     the operation performed is: dft_ab = (dft_a * fdt_b)*scaling

     The dft_a, dft_b and dft_ab pointers may alias.
  */
  void pffftd_zconvolve_no_accu(PFFFTD_Setup *setup, const double *dft_a, const double *dft_b, double*dft_ab, double scaling);

  /* return 4 or 1 wether support AVX instructions was enabled when building pffft-double.c */
  int pffftd_simd_size();

  /* return string identifier of used architecture (AVX/..) */
  const char * pffftd_simd_arch();


  /* following functions are identical to the pffft_ functions */

  /* simple helper to get minimum possible fft size */
  int pffftd_min_fft_size(pffft_transform_t transform);

  /* simple helper to determine next power of 2
     - without inexact/rounding floating point operations
  */
  int pffftd_next_power_of_two(int N);

  /* simple helper to determine if power of 2 - returns bool */
  int pffftd_is_power_of_two(int N);

  /*
    the double buffers must have the correct alignment (32-byte boundary
    on intel and powerpc). This function may be used to obtain such
    correctly aligned buffers.  
  */
  void *pffftd_aligned_malloc(size_t nb_bytes);
  void pffftd_aligned_free(void *);

#ifdef __cplusplus
}
#endif

#endif /* PFFFT_DOUBLE_H */

