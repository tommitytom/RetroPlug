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
#else

// TODO: Should not be used from within the RetroPlug library
//#include "IPlugPaths.h"

fs::path getContentPath(tstring file) {
    return "";
/*	WDL_String path;
    iplug::AppSupportPath(path);

	tstring strPath = tstr(path.Get());
    strPath += tstr("/RetroPlug/") + tstr(PLUG_VERSION_STR);

    if (file.size() > 0) {
        file = TSTR("/") + file;
    }

	return strPath + file;*/
}
#endif
