#pragma once

#include <Shlobj.h>

static std::string getContentPath() {
	TCHAR szPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
		return std::string(szPath) + "\\RetroPlug";
	}

	return "";
}
