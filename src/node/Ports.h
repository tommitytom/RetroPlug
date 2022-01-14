#pragma once

#include <string>
#include <entt/entity/registry.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/handle.hpp>
#include <entt/core/any.hpp>

namespace rp {
	struct OutputBase {
		std::string name;
		entt::any data;
	};

	template <typename T>
	class Output : public OutputBase {
	public:
		T& value() {
			assert(data.type() == entt::type_id<T>());
			return *static_cast<T*>(data.data());
		}

		const T& value() const {
			assert(data.type() == entt::type_id<T>());
			return *static_cast<const T*>(data.data());
		}

		void setValue(T&& value) {
			assert(data.type() == entt::type_id<T>());
			data = std::move(value);
			//data.emplace<T>(std::move(value));
		}
	};

	struct InputBase {
		std::string name;
		entt::any* data;
		entt::any defaultValue;
	};

	template <typename T>
	class Input : public InputBase {
	public:
		T& value() {
			assert(data->type() == entt::type_id<T>());
			return *static_cast<T*>(data->data());
		}

		const T& value() const {
			assert(data->type() == entt::type_id<T>());
			return *static_cast<const T*>(data->data());
		}
	};
}
