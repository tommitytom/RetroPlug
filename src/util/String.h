#pragma once

#include <string>
#include <codecvt>

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

// NOTE: ext must include "."!
static std::wstring changeExt(const std::wstring& path, const std::wstring& ext) {
	size_t dotIdx = path.find_last_of('.');
	return path.substr(0, dotIdx) + ext;
}

// NOTE: ext must include "."!
static std::string changeExt(const std::string& path, const std::string& ext) {
	size_t dotIdx = path.find_last_of('.');
	return path.substr(0, dotIdx) + ext;
}
