#pragma once

#include <memory>
#include <queue>

#include <defs.h>

#include "core/System.h"
#include "core/SystemTypes.h"
#include "sameboy/Constants.h"
#include "sameboy/SameboyConfig.h"

namespace rp {
	struct OffsetButton {
		int offset;
		int duration;
		int button;
		bool down;
	};

	struct SameBoyComponent {
		GameboyModel model = GameboyModel::Auto;
		bool fastBoot = true;
	};

	struct SameBoyState {
		GB_gameboy_t* gb;
		SystemIoPtr io;
		char frameBuffer[FRAME_BUFFER_SIZE];
		std::queue<OffsetButton> buttonQueue;
		//std::queue<TimedByte> serialQueue;
		GameboyModel model = GameboyModel::Auto;
		bool fastBoot = true;
		uint32 audioFrameCount = 0;
	};

	struct SameBoyStateComponent {
		std::unique_ptr<SameBoyState> state;
	};
}
