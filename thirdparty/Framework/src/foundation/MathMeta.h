#pragma once

#include <refl.hpp>

#include "Math.h"

REFL_AUTO(type(fw::PointF32), field(x), field(y))
REFL_AUTO(type(fw::PointI32), field(x), field(y))
REFL_AUTO(type(fw::Color4F), field(r), field(g), field(b), field(a))
