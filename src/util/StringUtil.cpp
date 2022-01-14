#include "StringUtil.h"

#include <algorithm>
#include <cctype>

using namespace rp;

std::string StringUtil::toLower(std::string_view s) {
	std::string t = std::string(s);

	std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
		return std::tolower(c);
	});

	return std::move(t);
}

std::string StringUtil::toUpper(std::string_view s) {
	std::string t = std::string(s);

	std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
		return std::toupper(c);
	});

	return std::move(t);
}

bool StringUtil::endsWith(std::string_view str, std::string_view comp) {
	if (str.size() > comp.size()) {
		return str.substr(str.size() - comp.size()) == comp;
	}

	return false;
}
