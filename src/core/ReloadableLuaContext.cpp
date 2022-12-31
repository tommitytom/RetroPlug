#include "ReloadableLuaContext.h"

namespace rp {
	ReloadableLuaContext::ReloadableLuaContext(const std::filesystem::path& scriptPath) {
		
	}

	ReloadableLuaContext::~ReloadableLuaContext() {
		delete _lua;
	}
}
