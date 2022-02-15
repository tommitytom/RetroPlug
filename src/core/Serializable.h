#pragma once

#include <sol/forward.hpp>

namespace rp {
	class Serializable {
	public:
		virtual void onSerialize(sol::state& s, sol::table target) = 0;

		virtual void onDeserialize(sol::state& s, sol::table source) = 0;
	};
}