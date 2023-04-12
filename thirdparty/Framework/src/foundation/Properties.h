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

	template <typename T> requires std::is_arithmetic_v<T>
	class PropertyNumeric : public PropertyBase {
	private:
		T* _value = nullptr;
		T _min = 0;
		T _max = 0;
		T _stepSize = 0;
		Curves::Func _curve = nullptr;

	public:
		PropertyNumeric() {}
		PropertyNumeric(const PropertyNumeric& other) { *this = other; }
		PropertyNumeric(PropertyNumeric&& other) noexcept { *this = std::move(other); }
		PropertyNumeric(T* value, T min = 0, T max = 0, T stepSize = 0, Curves::Func curve = Curves::linear)
			: _value(value), _min(min), _max(max), _stepSize(stepSize), _curve(curve) {}

		PropertyNumeric& operator=(const PropertyNumeric& other) noexcept {
			_value = other._value;
			_min = other._min;
			_max = other._max;
			_stepSize = other._stepSize;
			_curve = other._curve;

			incrementVersion();

			return *this;
		}

		PropertyNumeric& operator=(PropertyNumeric&& other) noexcept {
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

		void setValue(T value) {
			f32 range = static_cast<f32>(getRange());
			f32 rangedValue = static_cast<f32>(value - _min);

			if (_min != 0 && _max != 0) {
				f32 frac = MathUtil::clamp(rangedValue, 0.0f, range) / range;
				frac = MathUtil::clamp(_curve(frac), 0.0f, 1.0f);
				rangedValue = frac * range;
			}

			if (_stepSize > 0) {
				rangedValue -= fmod(rangedValue, static_cast<f32>(_stepSize));
			}

			*_value = static_cast<T>(rangedValue + (f32)_min);
			incrementVersion();
		}

		void setRange(T min, T max) {
			_min = min;
			_max = max;
			setValue(*_value);
		}

		void setCurve(Curves::Func func) {
			_curve = func;
		}

		T getMin() const {
			return _min;
		}

		T getMax() const {
			return _max;
		}

		T getValue() const {
			return *_value;
		}

		T getRange() const {
			return _max - _min;
		}

		bool isValid() const {
			return _value != nullptr;
		}
	};

	using PropertyF32 = PropertyNumeric<f32>;
}
