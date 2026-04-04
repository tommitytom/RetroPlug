#pragma once

#include <string>
#include "foundation/Types.h"
#include "util/GameboyUtil.h"

namespace rp::GameboyUtil {
	const uint32 ROM_NAME_OFFSET = 0x0134;
	const uint32 GAMEBOY_SAMPLE_RATE = 11468;

	inline std::string getRomName(const char* romData) {
		std::string romName = std::string(romData + ROM_NAME_OFFSET, 15);

		for (size_t i = 0; i < romName.size(); ++i) {
			if (romName[i] == '\0') {
				romName = romName.substr(0, i);
				break;
			}
		}

		return romName;
	}

	inline std::string_view getRomName(const orb::Uint8Buffer& romData) {
		std::string_view romName((const char*)romData.data() + ROM_NAME_OFFSET, 15);

		for (size_t i = 0; i < romName.size(); ++i) {
			if (romName[i] == '\0') {
				romName = romName.substr(0, i);
				break;
			}
		}

		return romName;
	}

	inline void fixChecksum(orb::Uint8Buffer& romData) {
		int checksum014D = 0;
        for (int i = 0x134; i < 0x14D; ++i) {
            checksum014D = checksum014D - romData[i] - 1;
        }
        romData[0x14D] = (uint8)(checksum014D & 0xFF);

        int checksum014E = 0;
        for (size_t i = 0; i < romData.size(); ++i) {
            if (i == 0x14E || i == 0x14F) {
                continue;
            }
            checksum014E += romData[i] & 0xFF;
        }

        romData[0x14E] = (uint8)((checksum014E & 0xFF00) >> 8);
        romData[0x14F] = (uint8)(checksum014E & 0x00FF);
	}
}
