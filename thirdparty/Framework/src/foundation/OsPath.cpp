#include "OsPath.h"

#ifdef FW_OS_WINDOWS
#include <Shlobj.h>

namespace fw::OsPath {
	std::string getContentPath() {
		char szPath[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
			return std::string(szPath);
		}

		return "";
	}
}

#endif