#include "Path.h"

#include "config.h"

#ifdef WIN32
#include <Shlobj.h>

fs::path getContentPath(tstring file) {
	TCHAR szPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
		fs::path out = szPath;
		out /= "RetroPlug";

		if (file.size() > 0) {
			out /= file;
		}

		return out;
	}

	return fs::path();
}
#endif
