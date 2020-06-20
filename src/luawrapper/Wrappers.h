#pragma once

#include <sol/forward.hpp>

namespace luawrappers {
	void registerLsdj(sol::state& s);

	void registerChrono(sol::state& s);

	void registerZipp(sol::state& s);

	void registerRetroPlug(sol::state& s);

	void registerCommon(sol::state& s);
}
