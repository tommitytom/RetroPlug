#pragma once

#include <string>
#include <codecvt>
#include <locale>

#ifdef WIN32
using tstring = std::wstring;
#define T(str) L##str
#else
using tstring = std::string;
#define T(str) str
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

#if WIN32
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
		return path.substr(0, dotIdx + 1);
	}

	return T("");
}
