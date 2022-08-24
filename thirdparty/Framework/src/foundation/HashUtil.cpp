#include "HashUtil.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

using namespace fw;

uint64 HashUtil::hash(const Uint8Buffer& buffer, uint64 seed) {
	return XXH64(buffer.data(), buffer.size(), seed);
}
