#pragma once

#include <string>

#include "core/ProjectState.h"

namespace rp {
	struct GlobalSettings {
		uint32 audioDeviceId = 0;
		std::string audioDeviceName;
		std::string keyboard;
		std::string pad;
	};
	
	struct RetroPlugConfig {
		GlobalSettings settings;
		ProjectState::Settings project;
		SystemSettings system;
	};
}