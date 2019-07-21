#include "Shell.h"

#ifdef WIN32 

void openShellFolder(const tstring& path) {
	ShellExecuteW(NULL, NULL, path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

#endif
