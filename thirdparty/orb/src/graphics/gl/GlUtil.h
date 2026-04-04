#pragma once

#include "foundation/Types.h"

namespace orb::GlUtil {
	bool checkShaderCompileError(uint32 program);

	bool checkProgramLinkError(uint32 program);
}
