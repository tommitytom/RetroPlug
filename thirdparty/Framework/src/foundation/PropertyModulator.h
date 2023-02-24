#pragma once

#include "foundation/Math.h"
#include "foundation/MathUtil.h"
#include "foundation/MetaProperties.h"
#include "foundation/Properties.h"
#include "foundation/TypeRegistry.h"

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
			const fw::Field* field = nullptr;
			entt::any source;
			entt::any target;
			
			/*Target() {}

			Target(const Target& other) = delete;

			Target(Target&& other) noexcept {
				*this = std::move(other);
			}*/

			/*Target& operator=(Target&& other) noexcept {
				assert(!other.source.owner());
				assert(!other.target.owner());

				name = std::move(other.name);
				field = other.field;
				source = std::move(other.source);
				target = std::move(other.target);

				other.field = nullptr;

				assert(!source.owner());
				assert(!target.owner());

				return *this;
			}*/
			
			/*Target& operator=(const Target& other) {
				assert(!other.source.owner());
				assert(!other.target.owner());
				
				name = other.name;
				field = other.field;
				source = other.source.as_ref();
				target = other.target.as_ref();

				assert(!source.owner());
				assert(!target.owner());

				return *this;
			}*/
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
				assert(!target.source.owner());
				assert(!target.target.owner());

				f32 source = entt::any_cast<f32>(target.source);
				f32 min = 0.0f;
				f32 range = 1.0f;

				if (const TypedProperty<Range>* rangeProp = target.field->findProperty<Range>(); rangeProp) {
					min = rangeProp->getValue().getMin();
					range = rangeProp->getValue().getMax() - min;
				}

				if (const TypedProperty<StepSize>* stepSize = target.field->findProperty<StepSize>(); stepSize) {
					
				}

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

				target.target.assign(min + ranged);
				assert(!target.target.owner());
			}
		}
	};
}
