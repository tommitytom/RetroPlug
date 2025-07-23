#pragma once

#include <string>
#include <unordered_map>

#include <entt/entity/registry.hpp>
#include <entt/meta/meta.hpp>

//#include "core/Model.h"
#include "core/Serializable.h"
#include "core/Forward.h"
#include "foundation/TypeRegistry.h"

namespace rp {
	struct SystemPaths {
		std::string romPath;
		std::string sramPath;
		std::string statePath;
	};

	struct SystemSettings {
		bool includeRom = true;
		bool gameLink = false;
		bool reloadRomOnChange = true;
	};

	struct SystemDesc {
		SystemPaths paths;
		SystemSettings settings;
		std::unordered_map<SystemServiceType, entt::any> services;
	};
}
