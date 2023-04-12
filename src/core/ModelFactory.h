#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include <entt/meta/factory.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>

#include "Model.h"

namespace rp {
	using RomFilter = std::function<bool(std::string_view)>;

	class ModelFactory {
	private:
		std::vector<std::pair<RomFilter, entt::meta_type>> _modelFactories;

	public:
		template <typename T>
		void addModelFactory(RomFilter&& romFilter) {
			entt::meta<T>()
				.type()
				.template ctor< &constructModel<T>, entt::as_ref_t >();

			_modelFactories.push_back({ romFilter, entt::resolve<T>() });
		}

		void createModels(std::string_view romName, std::vector<std::pair<entt::id_type, ModelPtr>>& models) {
			for (auto& factory : _modelFactories) {
				if (factory.first(romName)) {
					entt::meta_type type = factory.second;
					ModelPtr model;
					entt::meta_any v = type.construct(&model);
					models.push_back({ type.id(), model });
				}
			}
		}

	private:
		template <typename T>
		static T& constructModel(ModelPtr* model) {
			std::shared_ptr<T> p = std::make_shared<T>();
			*model = p;
			return *p;
		}
	};
}
