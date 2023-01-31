#include "ConfigLoader.h"

#include <sol/sol.hpp>
#include "foundation/SolUtil.h"
#include "foundation/FsUtil.h"
#include "foundation/LuaSerializer.h"

namespace rp {
	bool ConfigLoader::loadConfig(const fw::TypeRegistry& typeRegistry, const std::filesystem::path& path, GlobalConfig& target) {
		if (fs::exists(path)) {
			std::string data = fw::FsUtil::readTextFile(path);

			sol::state lua;
			fw::SolUtil::prepareState(lua);

			sol::table table;
			if (!fw::SolUtil::deserializeTable(lua, data, table)) {
				return false;
			}

			// TODO: Validate against schema

			return fw::LuaSerializer::deserialize(typeRegistry, table, target);
		}

		return false;
	}
}
