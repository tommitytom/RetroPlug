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
	};

	struct SystemSettings {
		bool includeRom = true;
		bool gameLink = false;

		std::string serialized;
		//std::unordered_map<entt::id_type, std::shared_ptr<Model>> models;
	};

	struct SystemDesc {
		SystemPaths paths;
		SystemSettings settings;
		std::unordered_map<SystemServiceType, entt::any> services;
	};
}
