#pragma once

#include <string>
#include "foundation/Types.h"

namespace rp::GameboyUtil {
	const uint32 ROM_NAME_OFFSET = 0x0134;

	static std::string getRomName(const char* romData) {
		std::string romName = std::string(romData + ROM_NAME_OFFSET, 15);
		
		for (size_t i = 0; i < romName.size(); ++i) {
			if (romName[i] == '\0') {
				romName = romName.substr(0, i);
				break;
			}
		}

		return romName;
	}

	static std::string_view getRomName(const fw::Uint8Buffer& romData) {
		std::string_view romName((const char*)romData.data() + ROM_NAME_OFFSET, 15);

		for (size_t i = 0; i < romName.size(); ++i) {
			if (romName[i] == '\0') {
				romName = romName.substr(0, i);
				break;
			}
		}

		return romName;
	}
}