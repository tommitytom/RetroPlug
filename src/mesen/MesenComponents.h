#pragma once

#include "Core/Shared/Emulator.h"

namespace rp {
	static constexpr double	CPU_CLOCK_RATE = 1789773.0;
	static constexpr int	PPU_DIVIDER = 3;		// PPU runs at 3x CPU clock (NTSC)

	// The FIFO registers the ROM polls.
	// These match the N8 Pro's FPGA register layout but can be
	// changed freely — just update your ROM accordingly.
	static constexpr uint16_t	FIFO_STATUS_ADDR = 0x4150;
	static constexpr uint16_t	FIFO_DATA_ADDR = 0x4151;

	struct MesenComponent { int dummy = 0; };
	struct MesenStateComponent {
		std::unique_ptr<Emulator> emulator;

		// Cycle count at the start of the current audio block.
		// Used to convert per-block sample offsets to absolute cycles.
		uint64 blockStartCycle = 0;
	};
}
