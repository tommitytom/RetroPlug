#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rp::StringUtil {
	std::string toLower(std::string_view s);

	std::string toUpper(std::string_view s);

	bool endsWith(std::string_view str, std::string_view comp);

	std::vector<std::string_view> split(std::string_view str, std::string_view delim);
}
