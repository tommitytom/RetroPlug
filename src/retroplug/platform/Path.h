#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include "util/xstring.h"
#include "util/fs.h"

fs::path getContentPath(tstring file = TSTR(""), bool isSystem = false);
