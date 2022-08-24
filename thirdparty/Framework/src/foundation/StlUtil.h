#pragma once

#include <vector>

#include "foundation/Types.h"

namespace fw::StlUtil {
	template <typename T>
	static bool vectorContains(const std::vector<T>& vec, const T& item) {
		for (size_t i = 0; i < vec.size(); ++i) {
			if (vec[i] == item) {
				return true;
			}
		}

		return false;
	}

	template <typename T>
	static int32 getVectorIndex(const std::vector<T>& vec, const T& item) {
		for (size_t i = 0; i < vec.size(); ++i) {
			if (&vec[i] == &item) {
				return (int32)i;
			}
		}

		return -1;
	}
}
