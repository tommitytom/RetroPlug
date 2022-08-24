#include "StringUtil.h"

#include <algorithm>
#include <cctype>
#include <codecvt>
#include <locale>

using namespace fw;

//_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

std::wstring StringUtil::toWString(const std::string& str) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	return converterX.from_bytes(str);
}

std::string StringUtil::formatClassName(std::string_view className) {
	std::string name;
	size_t offset = className.find_last_of(" :");

	if (offset != std::string::npos) {
		bool lastLower = false;
		offset += 1;

		for (size_t i = offset; i < className.size(); ++i) {
			if (lastLower && std::isupper(className[i])) {
				name.push_back(' ');
				name.push_back(className[i]);
			} else {
				name.push_back(className[i]);
				lastLower = true;
			}
		}

		return name;
	}

	return std::string(className);
}

std::string StringUtil::toString(const std::wstring& wstr) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;
	return converterX.to_bytes(wstr);
}

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

std::vector<std::string_view> StringUtil::split(std::string_view str, std::string_view delim) {
	size_t start = 0U;
	size_t end = str.find(delim);
	std::vector<std::string_view> target;

	if (end == std::string::npos) {
		target.push_back(str);
	} else {
		while (end != std::string::npos) {
			target.push_back(str.substr(start, end - start));

			start = end + delim.length();
			end = str.find(delim, start);
		}
	}

	return target;
}
