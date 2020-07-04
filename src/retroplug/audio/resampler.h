/* Copyright  (C) 2010-2017 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (rthreads.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __RESAMPLER_H__
#define __RESAMPLER_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <xmmintrin.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327
#endif


struct resampler_data
   {
	  const float *data_in;
	  float *data_out;

	  size_t input_frames;
	  size_t output_frames;

	  double ratio;
   };

void *resampler_sinc_init();
void resampler_sinc_process(void *re_, struct resampler_data *data);
void resampler_sinc_free(void *re_);

#endif

#ifdef RESAMPLER_IMPLEMENTATION
#undef RESAMPLER_IMPLEMENTATION

#define SINC_WINDOW_KAISER
#define SINC_WINDOW_KAISER_BETA 5.5
#define CUTOFF 0.825
#define PHASE_BITS 8
#define SUBPHASE_BITS 16
#define SINC_COEFF_LERP 1
#define SIDELOBES 8
#define ENABLE_AVX 0

#if SINC_COEFF_LERP
#define TAPS_MULT 2
#else
#define TAPS_MULT 1
#endif

#define window_function(idx)  (kaiser_window_function(idx, SINC_WINDOW_KAISER_BETA))

/* For the little amount of taps we're using,
* SSE1 is faster than AVX for some reason.
* AVX code is kept here though as by increasing number
* of sinc taps, the AVX code is clearly faster than SSE1.
*/

#define PHASES (1 << (PHASE_BITS + SUBPHASE_BITS))

#define TAPS (SIDELOBES * 2)
#define SUBPHASE_MASK ((1 << SUBPHASE_BITS) - 1)
#define SUBPHASE_MOD (1.0f / (1 << SUBPHASE_BITS))


typedef struct rarch_sinc_resampler
{
	float *phase_table;
	float *buffer_l;
	float *buffer_r;
	unsigned taps;
	unsigned ptr;
	uint32_t time;
	/* A buffer for phase_table, buffer_l and buffer_r
	* are created in a single calloc().
	* Ensure that we get as good cache locality as we can hope for. */
	float *main_buffer;
} rarch_sinc_resampler_t;


/* Modified Bessel function of first order.
* Check Wiki for mathematical definition ... */
static __forceinline double besseli0(double x)
{
	unsigned i;
	double sum = 0.0;
	double factorial = 1.0;
	double factorial_mult = 0.0;
	double x_pow = 1.0;
	double two_div_pow = 1.0;
	double x_sqr = x * x;
	/* Approximate. This is an infinite sum.
	* Luckily, it converges rather fast. */
	for (i = 0; i < 18; i++)
	{
		sum += x_pow * two_div_pow / (factorial * factorial);

		factorial_mult += 1.0;
		x_pow *= x_sqr;
		two_div_pow *= 0.25;
		factorial *= factorial_mult;
	}
	return sum;
}

static __forceinline double kaiser_window_function(double index, double beta)
{
	return besseli0(beta * sqrtf(1 - index * index));
}

static __forceinline double sinc(double val)
{
	if (fabs(val) < 0.00001)
		return 1.0;
	return sin(val) / val;
}

void *memalign_alloc(size_t boundary, size_t size)
{
	void **place = NULL;
	uintptr_t addr = 0;
	void *ptr = (void*)malloc(boundary + size + sizeof(uintptr_t));
	if (!ptr)
		return NULL;
	addr = ((uintptr_t)ptr + sizeof(uintptr_t) + boundary)
		& ~(boundary - 1);
	place = (void**)addr;
	place[-1] = ptr;
	return (void*)addr;
}

void memalign_free(void *ptr)
{
	void **p = NULL;
	if (!ptr)
		return;
	p = (void**)ptr;
	free(p[-1]);
}

void resampler_sinc_process(void *re_, struct resampler_data *data)
{
	size_t out_frames = 0;
	rarch_sinc_resampler_t *resamp = (rarch_sinc_resampler_t*)re_;
	uint32_t ratio = PHASES / data->ratio;
	const float *input = data->data_in;
	float *output = data->data_out;
	size_t frames = data->input_frames;

	while (frames)
	{
		while (frames && resamp->time >= PHASES)
		{
			/* Push in reverse to make filter more obvious. */
			if (!resamp->ptr)
				resamp->ptr = resamp->taps;
			resamp->ptr--;

			resamp->buffer_l[resamp->ptr + resamp->taps] =
				resamp->buffer_l[resamp->ptr] = *input++;

			resamp->buffer_r[resamp->ptr + resamp->taps] =
				resamp->buffer_r[resamp->ptr] = *input++;

			resamp->time -= PHASES;
			frames--;
		}

		while (resamp->time < PHASES)
		{
			unsigned i;
			__m128 sum;
			const float *buffer_l = resamp->buffer_l + resamp->ptr;
			const float *buffer_r = resamp->buffer_r + resamp->ptr;
			unsigned taps = resamp->taps;
			unsigned phase = resamp->time >> SUBPHASE_BITS;
			const float *phase_table = resamp->phase_table + phase * taps * TAPS_MULT;
			const float *delta_table = phase_table + taps;
			__m128 delta = _mm_set1_ps((float)
				(resamp->time & SUBPHASE_MASK) * SUBPHASE_MOD);

			__m128 sum_l = _mm_setzero_ps();
			__m128 sum_r = _mm_setzero_ps();

			for (i = 0; i < taps; i += 4)
			{
				__m128 buf_l = _mm_loadu_ps(buffer_l + i);
				__m128 buf_r = _mm_loadu_ps(buffer_r + i);
				__m128 deltas = _mm_load_ps(delta_table + i);
				__m128 _sinc = _mm_add_ps(_mm_load_ps(phase_table + i),
					_mm_mul_ps(deltas, delta));
				sum_l = _mm_add_ps(sum_l, _mm_mul_ps(buf_l, _sinc));
				sum_r = _mm_add_ps(sum_r, _mm_mul_ps(buf_r, _sinc));
			}

			/* Them annoying shuffles.
			* sum_l = { l3, l2, l1, l0 }
			* sum_r = { r3, r2, r1, r0 }
			*/

			sum = _mm_add_ps(_mm_shuffle_ps(sum_l, sum_r,
				_MM_SHUFFLE(1, 0, 1, 0)),
				_mm_shuffle_ps(sum_l, sum_r, _MM_SHUFFLE(3, 2, 3, 2)));

			/* sum   = { r1, r0, l1, l0 } + { r3, r2, l3, l2 }
			* sum   = { R1, R0, L1, L0 }
			*/

			sum = _mm_add_ps(_mm_shuffle_ps(sum, sum, _MM_SHUFFLE(3, 3, 1, 1)), sum);

			/* sum   = {R1, R1, L1, L1 } + { R1, R0, L1, L0 }
			* sum   = { X,  R,  X,  L }
			*/

			/* Store L */
			_mm_store_ss(output + 0, sum);

			/* movehl { X, R, X, L } == { X, R, X, R } */
			_mm_store_ss(output + 1, _mm_movehl_ps(sum, sum));

			output += 2;
			out_frames++;
			resamp->time += ratio;
		}
	}

	data->output_frames = out_frames;
}


static void sinc_init_table(rarch_sinc_resampler_t *resamp, double cutoff,
	float *phase_table, int phases, int taps, bool calculate_delta)
{
	int i, j;
	double    window_mod = window_function(0.0); /* Need to normalize w(0) to 1.0. */
	int           stride = calculate_delta ? 2 : 1;
	double     sidelobes = taps / 2.0;

	for (i = 0; i < phases; i++)
	{
		for (j = 0; j < taps; j++)
		{
			double sinc_phase;
			float val;
			int               n = j * phases + i;
			double window_phase = (double)n / (phases * taps); /* [0, 1). */
			window_phase = 2.0 * window_phase - 1.0; /* [-1, 1) */
			sinc_phase = sidelobes * window_phase;
			val = cutoff * sinc(M_PI * sinc_phase * cutoff) *
				window_function(window_phase) / window_mod;
			phase_table[i * stride * taps + j] = val;
		}
	}

	if (calculate_delta)
	{
		int phase;
		int p;

		for (p = 0; p < phases - 1; p++)
		{
			for (j = 0; j < taps; j++)
			{
				float delta = phase_table[(p + 1) * stride * taps + j] -
					phase_table[p * stride * taps + j];
				phase_table[(p * stride + 1) * taps + j] = delta;
			}
		}

		phase = phases - 1;
		for (j = 0; j < taps; j++)
		{
			float val, delta;
			double sinc_phase;
			int n = j * phases + (phase + 1);
			double window_phase = (double)n / (phases * taps); /* (0, 1]. */
			window_phase = 2.0 * window_phase - 1.0; /* (-1, 1] */
			sinc_phase = sidelobes * window_phase;

			val = cutoff * sinc(M_PI * sinc_phase * cutoff) *
				window_function(window_phase) / window_mod;
			delta = (val - phase_table[phase * stride * taps + j]);
			phase_table[(phase * stride + 1) * taps + j] = delta;
		}
	}
}

void resampler_sinc_free(void *data)
{
	rarch_sinc_resampler_t *resamp = (rarch_sinc_resampler_t*)data;
	if (resamp)
		memalign_free(resamp->main_buffer);
	free(resamp);
}

void *resampler_sinc_init()
{
	double cutoff;
	size_t phase_elems, elems;
	rarch_sinc_resampler_t *re = (rarch_sinc_resampler_t*)
		calloc(1, sizeof(*re));

	if (!re)
		return NULL;

	re->taps = TAPS;
	cutoff = CUTOFF;
	double bandwidth_mod = 1.0;
	/* Downsampling, must lower cutoff, and extend number of
	* taps accordingly to keep same stopband attenuation. */
	if (bandwidth_mod < 1.0)
	{
		cutoff *= bandwidth_mod;
		re->taps = (unsigned)ceil(re->taps / bandwidth_mod);
	}

	re->taps = (re->taps + 3) & ~3;

	phase_elems = ((1 << PHASE_BITS) * re->taps) * TAPS_MULT;
	elems = phase_elems + 4 * re->taps;

	re->main_buffer = (float*)memalign_alloc(128, sizeof(float) * elems);
	if (!re->main_buffer)
		goto error;

	re->phase_table = re->main_buffer;
	re->buffer_l = re->main_buffer + phase_elems;
	re->buffer_r = re->buffer_l + 2 * re->taps;

	sinc_init_table(re, cutoff, re->phase_table,
		1 << PHASE_BITS, re->taps, SINC_COEFF_LERP);
	return re;

error:
	resampler_sinc_free(re);
	return NULL;
}

#endif /* INI_IMPLEMENTATION */


