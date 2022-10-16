#pragma once

#include <string>

#include "foundation/Curves.h"
#include "foundation/Types.h"

namespace fw {
	class UriBrowser {
	private:
		std::vector<entt::type_info> _items;

	public:
		UriBrowser() {}
		UriBrowser(const std::vector<entt::type_info>& items): _items(items) {}
		UriBrowser(const UriBrowser& other): _items(other._items) {}

		UriBrowser& operator=(const UriBrowser& other) {
			_items = other._items;
			return *this;
		}

		const std::vector<entt::type_info>& getItems() const {
			return _items;
		}
	};

	class Range {
	private:
		f32 _min = 0.0f;
		f32 _max = 1.0f;

	public:
		Range() {}
		Range(f32 min, f32 max) : _min(min), _max(max) {}

		f32 getMin() const {
			return _min;
		}

		f32 getMax() const {
			return _max;
		}
	};

	class Curve {
	private:
		Curves::Func _func;

	public:
		Curve() {}
		Curve(const Curves::Func& func) : _func(func) {}

		const Curves::Func& getFunc() const {
			return _func;
		}
	};

	class StepSize {
	private:
		f32 _stepSize = 0.0f;

	public:
		StepSize() {}
		StepSize(f32 value) : _stepSize(value) {}

		f32 getValue() const {
			return _stepSize;
		}
	};

	class DisplayName {
	private:
		std::string _name;

	public:
		DisplayName() {}
		DisplayName(std::string_view name) : _name(std::string(name)) {}

		const std::string& getName() const {
			return _name;
		}
	};
}
