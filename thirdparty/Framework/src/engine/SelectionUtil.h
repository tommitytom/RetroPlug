#pragma once

#include <vector>
#include <entt/meta/meta.hpp>
#include <entt/entity/registry.hpp>

namespace fw {
	struct SelectionSingleton {
		std::vector<entt::meta_any> selected;
	};

	struct SelectionEvent {
		std::vector<entt::meta_any>* selected;
	};

	namespace SelectionUtil {
		void setup(entt::registry& reg) {
			reg.ctx().emplace<SelectionSingleton>();
		}

		void setSelection(entt::registry& reg, entt::meta_any handle, bool removeExisting) {
			SelectionSingleton& sel = reg.ctx().at<SelectionSingleton>();

			if (removeExisting) {
				sel.selected.clear();
			}

			sel.selected.push_back(std::move(handle));

			WorldUtil::pushEvent<SelectionEvent>(reg, { .selected = &sel.selected });
		}

		const std::vector<entt::meta_any>& getSelected(entt::registry& reg) {
			return reg.ctx().at<SelectionSingleton>().selected;
		}
	}
}
