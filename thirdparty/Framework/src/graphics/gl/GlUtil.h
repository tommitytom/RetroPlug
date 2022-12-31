#pragma once

#include "foundation/Types.h"

namespace fw::GlUtil {
	bool checkShaderCompileError(uint32 program);

	bool checkProgramLinkError(uint32 program);
}
