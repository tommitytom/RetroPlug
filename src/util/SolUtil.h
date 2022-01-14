#pragma once

#include <sol/forward.hpp>

namespace rp::SolUtil {
	void addIncludePath(sol::state& s, std::string_view path);

	bool serializeTable(sol::state& s, const sol::table& source, std::string& target);

	bool deserializeTable(sol::state& s, const std::string& data, sol::table& target);
}
