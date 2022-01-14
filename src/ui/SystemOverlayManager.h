#pragma once

#include <string_view>
#include <vector>

#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

#include "ui/View.h"

namespace rp {
	using RomFilter = std::function<bool(std::string_view)>;

	class SystemOverlayManager {
	private:
		std::vector<std::pair<RomFilter, entt::meta_type>> _factories;

		template <typename T>
		static T& constructOverlay(std::vector<ViewPtr>* target) {
			std::shared_ptr<T> p = std::make_shared<T>();
			target->push_back(p);
			return *p;
		}

	public:
		template <typename T>
		void addOverlayFactory(RomFilter&& romFilter) {
			entt::meta<T>()
				.type()
				.template ctor< &constructOverlay<T>, entt::as_ref_t >();

			_factories.push_back({ romFilter, entt::resolve<T>() });
		}

		std::vector<ViewPtr> createOverlays(std::string_view romName) {
			std::vector<ViewPtr> overlays;

			for (auto& factory : _factories) {
				if (factory.first(romName)) {
					entt::meta_type type = factory.second;
					spdlog::info("Creating overlay {} for {}", type.info().name(), romName);
					type.construct(&overlays);
				}
			}

			return overlays;
		}
	};
}
