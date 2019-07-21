#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include "util/xstring.h"

void openFolder(const tstring& path);
