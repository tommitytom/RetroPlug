## AVX-optimized sin(), cos(), exp() and log() functions

### Introduction
Origin from [here](http://software-lisc.fbk.eu/avx_mathfun/). But fix several problem for it.

* The origin file is not compatible with GCC 4.9+.
    * because header immintrin.h is changed between [gcc 4.8](https://github.com/gcc-mirror/gcc/blob/gcc-4_8_5-release/gcc/config/i386/immintrin.h)
    and [gcc 4.9](https://github.com/gcc-mirror/gcc/blob/gcc-4_9_0-release/gcc/config/i386/immintrin.h)
    * In gcc 4.9, AVX2 function will always be defined whenever -mavx2 is set or not. So there are multi-define problems
    when use gcc 4.9 before.
    * In macos, the clang also use the header like gcc 4.9
    * I just add "avx_" prefix for each function in AVX2 to solve this problem.
* There is a warning in int constant variable define.
    * Just add a cast to fix it.
* AVX2 use intrinsics like '_mm256_and_si256', '_mm256_andnot_si256'
    * Change origin header to fix it. Make it work well in both avx and avx2.

### Test and Compile

Test programs use cmake. Just do

    # Install gtest before, and make sure your computer support avx2/avx.
    mkdir build
    cmake -D CMAKE_BUILD_TYPE=Release ..
    make -j 2
    ./test_avx2 ; ./test_avx

It will run some unittest.

### How to use this library

Just include "avx_mathfun.h" in your project.

### License

The origin [file](http://software-lisc.fbk.eu/avx_mathfun/) uses zlib license. It is not changed.