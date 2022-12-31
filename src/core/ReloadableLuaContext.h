#pragma once

#include <filesystem>

#include <sol/forward.hpp>

namespace rp {
	class ReloadableLuaContext {
	private:
		sol::state* _lua = nullptr;

	public:
		ReloadableLuaContext(const std::filesystem::path& scriptPath);
		~ReloadableLuaContext();
	};
}
