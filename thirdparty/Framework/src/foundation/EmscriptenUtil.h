#pragma once

#ifdef FW_PLATFORM_WEB

#include <emscripten.h>

namespace fw::EmscriptenUtil {
	inline void doLog(const char* str) {
		EM_ASM({
			console.log(UTF8ToString($0));
		}, str);
	}
}
#endif