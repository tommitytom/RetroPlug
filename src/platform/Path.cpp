#include "Path.h"

#ifdef WIN32
#include <Shlobj.h>

tstring getContentPath(tstring file) {
	TCHAR szPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
		tstring out = tstr(szPath) + T("\\RetroPlug");
		if (file.size() > 0) {
			out += T("\\") + file;
		}

		return out;
	}

	return T("");
}
#else

#include "IPlugPaths.h"

tstring getContentPath(tstring file) {
	WDL_String path;
    iplug::AppSupportPath(path);

	tstring strPath = tstr(path.Get());
    strPath += T("/RetroPlug");

    if (file.size() > 0) {
        file = T("/") + file;
    }

	return strPath + file;
}
#endif
