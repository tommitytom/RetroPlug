#pragma once

#include <string>
#include <gb_struct_def.h>
#include "sameboy/semver.hpp"

#include "platform/Types.h"
#include "lsdj/Ram.h"

namespace rp::lsdj {
	namespace OffsetCalculator {
		struct Context {
			std::string filename;
			std::string tags;
			std::string romName;
			std::string version;
			semver::version semver;

			GB_gameboy_t* gb;
			MemoryOffsets offsets;
			uint32 frameBuffer[160 * 144];
			bool keyStates[8];

			bool hadAudio = false;
		};

		bool calculate(const char* romData, Context& ctx, const std::string& filename = "");
	}
}
