#pragma once

#include "foundation/Curves.h"
#include "foundation/MathUtil.h"

namespace fw {
	class PropertyBase {
	private:
		uint32 _version = 0;

	public:
		uint32 getVersion() const {
			return _version;
		}

	protected:
		void incrementVersion() {
			_version += 1;
		}
	};

	class PropertyF32 : public PropertyBase {
	private:
		f32* _value = nullptr;
		f32 _min = 0.0f;
		f32 _max = 0.0f;
		f32 _stepSize = 0.0f;
		Curves::Func _curve = nullptr;

	public:
		PropertyF32() {}
		PropertyF32(const PropertyF32& other) { *this = other; }
		PropertyF32(PropertyF32&& other) noexcept { *this = std::move(other); }
		PropertyF32(f32* value, f32 min = 0.0f, f32 max = 0.0f, f32 stepSize = 0.0f, Curves::Func curve = Curves::linear)
			: _value(value), _min(min), _max(max), _stepSize(stepSize), _curve(curve) {}

		PropertyF32& operator=(const PropertyF32& other) noexcept {
			_value = other._value;
			_min = other._min;
			_max = other._max;
			_stepSize = other._stepSize;
			_curve = other._curve;

			incrementVersion();

			return *this;
		}
		
		PropertyF32& operator=(PropertyF32&& other) noexcept {
			_value = other._value;
			_min = other._min;
			_max = other._max;
			_stepSize = other._stepSize;
			_curve = other._curve;

			other._value = 0;
			other._min = 0;
			other._max = 0;
			other._stepSize = 0;
			other._curve = nullptr;

			incrementVersion();

			return *this;
		}

		void setValue(f32 value) {
			f32 range = getRange();
			f32 rangedValue = value - _min;

			if (_min != 0.0f && _max != 0.0f) {
				f32 frac = MathUtil::clamp(rangedValue, 0.0f, range) / range;
				frac = MathUtil::clamp(_curve(frac), 0.0f, 1.0f);
				rangedValue = frac * range;
			}

			if (_stepSize > 0) {
				rangedValue -= fmod(rangedValue, _stepSize);
			}

			*_value = rangedValue + _min;
			incrementVersion();
		}

		void setRange(f32 min, f32 max) {
			_min = min;
			_max = max;
			setValue(*_value);
		}

		void setCurve(Curves::Func func) {
			_curve = func;
		}

		f32 getMin() const {
			return _min;
		}

		f32 getMax() const {
			return _max;
		}

		f32 getValue() const {
			return *_value;
		}

		f32 getRange() const {
			return _max - _min;
		}

		bool isValid() const {
			return _value != nullptr;
		}
	};
}
