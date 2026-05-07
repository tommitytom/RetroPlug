#pragma once

#define xstringify(a) stringify(a)
#define stringify(a) #a

#define VERSION_INT(v1, v2, v3) ((int)(((unsigned char)v1 << 16) | ((unsigned char)v2 << 8) | ((unsigned char)v3 & 0xFF)))
#define VERSION_STRING(v1, v2, v3) xstringify(v1) "." xstringify(v2) "." xstringify(v3)
