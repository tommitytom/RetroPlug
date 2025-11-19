#include "Shell.h"

#ifdef FW_OS_WINDOWS

#include <windows.h>

namespace orb {
	void openShellFolder(const std::string& path) {
		ShellExecuteA(NULL, NULL, path.c_str(), NULL, NULL, SW_SHOWNORMAL);
	}
}

#else

namespace orb {
	void openShellFolder(const std::string& path) {}
}

#endif
