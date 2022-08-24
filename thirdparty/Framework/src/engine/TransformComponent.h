#pragma once

#include "foundation/Math.h"

namespace fw {
	struct TransformComponent {
		PointF position = { 0, 0 };
		PointF scale = { 1, 1 };
		f32 rotation = 0.0f;
	};

	struct WorldTransformComponent {
		Mat3x3 transform;
	};

	struct WorldAabbComponent {
		PointF min;
		PointF max;
	};
}
