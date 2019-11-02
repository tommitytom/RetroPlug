#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include "util/xstring.h"

tstring getContentPath(tstring file = TSTR(""));
