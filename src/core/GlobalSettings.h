#pragma once

#include <string>

namespace rp {
	struct GlobalSettings {
		uint32 audioDeviceId = 0;
		std::string audioDeviceName;
		std::string keyboard;
		std::string joypad;
	};
}