#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include "util/xstring.h"

void openShellFolder(const tstring& path);
