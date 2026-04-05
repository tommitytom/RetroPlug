#include "OsPath.h"

#ifdef FW_OS_WINDOWS
#include <Shlobj.h>

namespace orb::OsPath {
	std::string getContentPath() {
		char szPath[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
			return std::string(szPath);
		}

		return "";
	}
}
#elif defined(FW_PLATFORM_WEB)
namespace orb::OsPath {
	std::string getContentPath() {
		return "/";
	}
}
#elif defined(FW_OS_LINUX)
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <limits.h>
#include <stdlib.h>
#include <string>

namespace orb::OsPath {
	std::string getContentPath() {
		const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
		if (xdgConfig && *xdgConfig) {
			return std::string(xdgConfig);
		}

		const char* home = std::getenv("HOME");
		if (home && *home) {
			return std::string(home) + "/.config";
		}

		return "~/.config";
	}
}
#endif