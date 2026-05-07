#pragma once

#include <string>

#include "core/ProjectState.h"

namespace rp {
	struct GlobalSettings {
		std::string audioDeviceName;
		std::string keyboard = "default.lua";
		std::string pad = "default.lua";
	};
	
	struct RetroPlugConfig {
		GlobalSettings settings;
		ProjectState::Settings project;
		SystemSettings system;
	};
}