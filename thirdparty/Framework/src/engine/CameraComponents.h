#pragma once

#include "foundation/Math.h"

namespace fw {
	struct ActiveCameraTag {};

	struct CameraSingleton {
		RectF viewPort;
		RectF projection;
	};

	struct OrthographicCameraComponent {
		f32 zoom = 1.0f;
	};
}
