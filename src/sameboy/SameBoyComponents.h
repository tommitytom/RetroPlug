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

	struct SameBoyStateComponent {
		GB_gameboy_t* gb;
		std::queue<OffsetButton> buttonQueue;
		char frameBuffer[FRAME_BUFFER_SIZE];
		//std::queue<TimedByte> serialQueue;
		GameboyModel model = GameboyModel::Auto;
		bool fastBoot = true;
		SystemIoPtr io;
		uint32 audioFrameCount = 0;

		// Delete copy operations
		SameBoyStateComponent(const SameBoyStateComponent&) = delete;
		SameBoyStateComponent& operator=(const SameBoyStateComponent&) = delete;

		// Keep move operations (automatically generated)
		SameBoyStateComponent(SameBoyStateComponent&&) = default;
		SameBoyStateComponent& operator=(SameBoyStateComponent&&) = default;

		SameBoyStateComponent() = default;
		~SameBoyStateComponent() = default;
	};
}
