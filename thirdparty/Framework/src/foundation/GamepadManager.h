#pragma once

namespace orb {
	class GamepadManager {
	public:
		virtual void update() = 0;
		virtual void setAxisButtonThreshold(float threshold) = 0;
	};
}
