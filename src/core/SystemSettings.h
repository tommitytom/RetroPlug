#pragma once

#include <string>
#include <unordered_map>

#include <entt/entity/registry.hpp>
#include <entt/meta/meta.hpp>

#include "core/Model.h"
#include "core/Serializable.h"

namespace rp {
	struct SystemSettings {
		struct InputSettings {
			std::string key;
			std::string pad;
		};

		std::string romPath;
		std::string sramPath;
		bool includeRom = true;
		InputSettings input;

		std::string serialized;
		//std::unordered_map<entt::id_type, std::shared_ptr<Model>> models;
	};
}
