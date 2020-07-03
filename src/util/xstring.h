#pragma once

#include <string>
#include <vector>
#include <codecvt>
#include <locale>

#include <algorithm> 
#include <cctype>

#ifdef WIN32
using tstring = std::wstring;
#define TSTR(str) L##str
#else
using tstring = std::string;
#define TSTR(str) str
#endif

static std::wstring s2ws(const std::string& str) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.from_bytes(str);
}

static std::string ws2s(const std::wstring& wstr) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes(wstr);
}

static std::string ws2s(const std::string& str) {
	return str;
}

#ifdef WIN32
static tstring tstr(const std::string& str) {
	return s2ws(str);
}

static const tstring& tstr(const std::wstring& str) {
	return str;
}
#else
static const tstring& tstr(const std::string& str) {
	return str;
}

static tstring tstr(const std::wstring& str) {
	return ws2s(str);
}
#endif



// NOTE: ext must include "."!
static tstring changeExt(const tstring& path, const tstring& ext) {
	size_t dotIdx = path.find_last_of('.');
	return path.substr(0, dotIdx) + ext;
}

static tstring getExt(const tstring& path) {
	size_t dotIdx = path.find_last_of('.');
	if (dotIdx != -1) {
		return path.substr(dotIdx, path.length() - dotIdx);
	}

	return TSTR("");
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

static inline std::vector<std::string> split(const std::string& i_str, const std::string& i_delim) {
	std::vector<std::string> result;

	size_t found = i_str.find(i_delim);
	size_t startIndex = 0;

	while (found != std::string::npos) {
		std::string temp(i_str.begin() + startIndex, i_str.begin() + found);
		result.push_back(temp);
		startIndex = found + i_delim.size();
		found = i_str.find(i_delim, startIndex);
	}

	if (startIndex != i_str.size()) {
		result.push_back(std::string(i_str.begin() + startIndex, i_str.end()));
	}

	return result;
}
