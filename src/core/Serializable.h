#pragma once

#include <sol/forward.hpp>

namespace rp {
	class Serializable {
	public:
		virtual void serialize(sol::state& s, sol::table target) = 0;

		virtual void deserialize(sol::state& s, sol::table source) = 0;
	};
}