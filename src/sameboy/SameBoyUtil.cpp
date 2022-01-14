#include "SameBoyUtil.h"

extern "C" {
#include <gb.h>
}

using namespace rp;

void SameBoyUtil::spinMs(GB_gameboy_t* gb, f32 ms) {
	spinNs(gb, ms * 1000000.0f);
}

void SameBoyUtil::spinNs(GB_gameboy_t* gb, f32 ns) {
	f32 clockRate = (f32)GB_get_clock_rate(gb);

	while (ns > 0) {
		uint8 cycles = GB_run(gb);
		ns -= (f32)cycles * 1000000000.0f / 2.0f / clockRate;
	}
}

f32 SameBoyUtil::cyclesToNs(GB_gameboy_t* gb, uint64 cycles) {
	return (f32)cycles * 1000000000.0f / 2.0f / (f32)GB_get_clock_rate(gb);
}

f32 SameBoyUtil::cyclesToMs(GB_gameboy_t* gb, uint64 cycles) {
	return cyclesToNs(gb, cycles) / 1000000.0f;
}
