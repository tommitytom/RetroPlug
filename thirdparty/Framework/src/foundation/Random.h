#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "foundation/Types.h"

namespace fw {
	const uint32 SEED_MAX = UINT32_MAX / 2;
	const uint32 MID_POINT = SEED_MAX / 2;
	const std::string_view RANDOM_STRING_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	// Fixed point random that generates the same numbers on any platform/language
	class Random {
	private:
		uint32 _seed;

	public:
		Random(uint32 seed = 1234) : _seed(seed) {}
		~Random() {}

		void setSeed(uint32 seed) {
			if (seed == 0) {
				seed = SEED_MAX;
			}

			_seed = seed % SEED_MAX;
		}

		uint32 getSeed() const {
			return _seed;
		}

		uint32 nextInt() {
			_seed = _seed * 16807 % SEED_MAX;
			return _seed;
		}

		f64 nextDouble() {
			return (f64)(nextInt() - 1) / (f64)(SEED_MAX - 1);
		}

		f32 nextFloat() {
			return (f32)(nextInt() - 1) / (f32)(SEED_MAX - 1);
		}

		bool nextBool() {
			return nextInt() > MID_POINT;
		}

		f64 nextDoubleRange(f64 min, f64 max) {
			f64 range = max - min;
			return nextDouble() * range + min;
		}

		f32 nextFloatRange(f32 min, f32 max) {
			f32 range = max - min;
			return nextFloat() * range + min;
		}

		uint32 nextIntRange(uint32 min, uint32 max) {
			uint32 range = max - min;
			if (range != 0) {
				return nextInt() % range + min;
			}

			return 0;
		}

		int32 nextIntRange(int32 min, int32 max) {
			int32 range = max - min;
			if (range != 0) {
				return nextInt() % range + min;
			}

			return 0;
		}

		std::string nextString(size_t length) {
			std::string out;
			out.resize(length);

			for (size_t i = 0; i < length; ++i) {
				out[i] = RANDOM_STRING_CHARS[nextIntRange(0, (int32)RANDOM_STRING_CHARS.size())];
			}

			return out;
		}
	};
}
