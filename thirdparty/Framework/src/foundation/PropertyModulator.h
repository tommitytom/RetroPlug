#pragma once

#include "foundation/Math.h"
#include "foundation/MathUtil.h"
#include "foundation/Properties.h"

namespace fw {
	struct PropertyModulator {
	public:
		enum class Type {
			Sine,
			Triangle,
			SawTooth,
			Pulse
		};

		enum class Mode {
			Wrap,
			Clamp,
			PingPong
		};

		enum class Timing {
			Frequency,
			Multiplier
		};

		struct Target {
			std::string name;
			entt::meta_data field;
			PropertyF32 source;
			PropertyF32 target;
		};

		Type type = Type::Sine;
		Mode mode = Mode::Wrap;
		Timing timing = Timing::Frequency;

		f32 frequency = 1.0f;
		f32 multiplier = 1.0f;

		f32 range = 0.5f;

		f32 position = 0.0f;
		f32 cycleLength = 1.0f;

		std::vector<Target> targets;

		f32 getFrac() const {
			return position / cycleLength;
		}

		void update(f32 delta) {
			f32 cycleLength = 1.0f / this->frequency;
			this->position = fmod(this->position + delta, cycleLength);

			f32 modFrac = this->position / cycleLength;
			f32 modSource = 0.0f;

			switch (this->type) {
			case PropertyModulator::Type::Sine:
				modSource = sin(modFrac * PI2) * 0.5f;
				break;
			case PropertyModulator::Type::SawTooth:
				modSource = modFrac - 0.5f;
				break;
			case PropertyModulator::Type::Pulse:
				modSource = modFrac > 0.5f ? 0.5f : -0.5f;
				break;
			case PropertyModulator::Type::Triangle:
				modSource = modFrac > 0.5f ? 0.5f : -0.5f;
				break;
			}

			for (PropertyModulator::Target& target : this->targets) {
				if (!target.source.isValid()) {
					continue;
				}

				f32 source = target.source.getValue();
				f32 min = target.source.getMin();
				f32 range = target.source.getRange();

				f32 ranged = source - min;
				ranged += modSource * range * this->range;

				switch (this->mode) {
				case PropertyModulator::Mode::Wrap:
					while (ranged < 0.0f) { ranged += range; }
					ranged = fmod(ranged, range);
					break;
				case PropertyModulator::Mode::Clamp:
					ranged = MathUtil::clamp(ranged, 0.0f, range);
					break;
				case PropertyModulator::Mode::PingPong:
					while (ranged < 0.0f || ranged > range) {
						if (ranged < 0.0f) {
							ranged = fabs(ranged);
						} else {
							ranged = range - (ranged - range);
						}
					}

					break;
				}

				target.target.setValue(min + ranged);
			}
		}
	};
}
