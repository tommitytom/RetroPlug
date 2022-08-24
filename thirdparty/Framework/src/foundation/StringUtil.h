#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fw::StringUtil {
	inline const char* WHITESPACE_CHARS = " \t\n\r\f\v";

	std::string formatClassName(std::string_view className);

	std::string toString(const std::wstring& str);

	std::wstring toWString(const std::string& str);

	std::string toLower(std::string_view s);

	std::string toUpper(std::string_view s);

	bool endsWith(std::string_view str, std::string_view comp);

	std::vector<std::string_view> split(std::string_view str, std::string_view delim);

	// trim from end of string (right)
	inline std::string& rtrim(std::string& s, const char* t = WHITESPACE_CHARS) {
		s.erase(s.find_last_not_of(t) + 1);
		return s;
	}

	// trim from beginning of string (left)
	inline std::string& ltrim(std::string& s, const char* t = WHITESPACE_CHARS) {
		s.erase(0, s.find_first_not_of(t));
		return s;
	}

	// trim from both ends of string (right then left)
	inline std::string& trim(std::string& s, const char* t = WHITESPACE_CHARS) {
		return ltrim(rtrim(s, t), t);
	}
}
