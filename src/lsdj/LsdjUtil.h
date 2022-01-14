#pragma once

#include <string>
#include <sstream>

#include "sameboy/semver.hpp"
#include "platform/Types.h"

namespace rp::LsdjUtil {
	static bool getVersionFromName(const std::string& romName, std::string& target) {
		if (romName.find("LSDj^4.2.9") != std::string::npos) {
			// This version has a different rom name than others (LSDj^4.2.9)
			target = "4.2.9";
			return true;
		}
		
		if (romName.find("LSDj") != std::string::npos) {
			target = romName.substr(6, 5);
			return true;
		}

		return false;
	}

	static bool getVersionFromFilename(const std::string& romFilename, std::string& target) {
		target = romFilename.substr(4, 1) + "." + romFilename.substr(6, 1) + "." + romFilename.substr(8, 1);
		return true;
	}

	static uint8 getVersionFromChar(char c) {
		if (c >= 'A' && c <= 'Z') {
			return (uint8)(10 + (c - 'A'));
		}

		if (c >= 'a' && c <= 'z') {
			return (uint8)(10 + (c - 'a'));
		}

		return c - 0x30;
	}

	static semver::version getSemVerFromVersion(const std::string& version) {
		return semver::version{ 
			getVersionFromChar(version[0]),
			getVersionFromChar(version[2]),
			getVersionFromChar(version[4])
		};
	}

	static Point<uint32> pixelToTile(Point<uint32> pos) {
		return { pos.x / 8u, pos.y / 8u };
	}

	static bool tileToCursorPos(Point<uint32> tile, Point<uint8>& target) {
		for (uint32 x = 0; x < 4; ++x) {
			uint32 xOffset = (x + 1) * 3;
			if (tile.x >= xOffset && tile.x < xOffset + 2) {
				target.x = x;

				if (tile.y > 1) {
					target.y = tile.y - 2;
					return true;
				}
			}
		}

		return false;
	}

	static bool pixelToCursorPos(Point<uint32> pos, Point<uint8>& target) {
		return tileToCursorPos(pixelToTile(pos), target);
	}
}
