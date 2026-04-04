#include "StringUtil.h"

#include <algorithm>
#include <cctype>
#include <codecvt>
#include <locale>
#include <format>

using namespace orb;

#ifdef FW_OS_WINDOWS
#include <windows.h>
#endif

//_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

std::wstring StringUtil::toWString(const std::string& str) {
	if (str.empty()) return {};

#ifdef FW_OS_WINDOWS
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (size <= 0) return {};

	std::wstring result(size - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), size);
	return result;

#else
	try {
		std::mbstate_t state = std::mbstate_t();
		std::locale loc("en_US.UTF-8");
		const std::codecvt<wchar_t, char, std::mbstate_t>& codecvt =
			std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(loc);

		std::vector<wchar_t> buffer(str.length());
		const char* from_end;
		wchar_t* to_end;

		auto result = codecvt.in(state, str.data(), str.data() + str.length(), from_end,
								buffer.data(), buffer.data() + buffer.size(), to_end);

		if (result == std::codecvt_base::ok) {
			return std::wstring(buffer.data(), to_end);
		}
	} catch (...) {
		// Fallback to simple conversion for ASCII
	}

	// Fallback for ASCII-compatible strings
	return std::wstring(str.begin(), str.end());
#endif
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
				lastLower = false;
			} else {
				name.push_back(className[i]);
				lastLower = true;
			}
		}

		return name;
	}

	return std::string(className);
}

// TODO: Move to MetaUtil?
std::string StringUtil::formatMemberName(std::string_view memberName) {
	std::string name;
	bool lastLower = false;

	name.push_back(std::toupper(memberName[0]));

	for (size_t i = 1; i < memberName.size(); ++i) {
		if (lastLower && std::isupper(memberName[i])) {
			name.push_back(' ');
			name.push_back(memberName[i]);
			lastLower = false;
		} else {
			name.push_back(memberName[i]);
			lastLower = true;
		}
	}

	return name;
}

std::string StringUtil::toString(const std::wstring& wstr) {
	if (wstr.empty()) return {};

#ifdef FW_OS_WINDOWS
	int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};

	std::string result(size - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), size, nullptr, nullptr);
	return result;

#else
	try {
		std::mbstate_t state = std::mbstate_t();
		std::locale loc("en_US.UTF-8");
		const std::codecvt<wchar_t, char, std::mbstate_t>& codecvt =
			std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(loc);

		std::vector<char> buffer(wstr.length() * 4); // UTF-8 can be up to 4 bytes per character
		const wchar_t* from_end;
		char* to_end;

		auto result = codecvt.out(state, wstr.data(), wstr.data() + wstr.length(), from_end,
								 buffer.data(), buffer.data() + buffer.size(), to_end);

		if (result == std::codecvt_base::ok) {
			return std::string(buffer.data(), to_end);
		}
	} catch (...) {
		// Fallback to simple conversion for ASCII
	}

	// Fallback for ASCII-compatible strings
	std::string result;
	result.reserve(wstr.length());
	for (wchar_t wc : wstr) {
		if (wc < 128) { // ASCII range
			result.push_back(static_cast<char>(wc));
		} else {
			result.push_back('?'); // Replace non-ASCII with placeholder
		}
	}
	return result;
#endif
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
