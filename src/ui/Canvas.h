#pragma once

#include "NanovgCanvas.h"
#include "graphics/BgfxCanvas.h"

namespace rp {
	//class Canvas : public NanovgCanvas {};
	class Canvas : public rp::engine::BgfxCanvas {};
}
