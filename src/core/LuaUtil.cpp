#include "LuaUtil.h"

#include <sol/sol.hpp>

using namespace rp;

void LuaUtil::prepareState(sol::state& state) {
	fw::SolUtil::prepareState(state);
	state.add_package_loader(rp::CompiledScripts::config::loader);
	state.add_package_loader(rp::CompiledScripts::utils::loader);
}
