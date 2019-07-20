#pragma once

#ifdef WIN32
#include <Shlobj.h>

static std::string getContentPath(std::string file = "") {
	TCHAR szPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
		std::string out = std::string(szPath) + "\\RetroPlug";
		if (file.size() > 0) {
			out += "\\" + file;
		}

		return out;
	}

	return "";
}
#else

#include "IPlugPaths.h"

static std::string getContentPath(std::string file = "") {
	WDL_String path;
    AppSupportPath(path);

	std::string strPath(path.Get());
    
    if (file.size() > 0) {
        file = "/" + file;
    }
    
	return strPath + file;
}
#endif
