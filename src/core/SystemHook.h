#pragma once

#include <functional>
#include <entt/entity/registry.hpp>
#include "ui/View.h"
#include "ecs/ProjectSerializerContext.h"
#include "core/CoreComponents.h"

namespace rp {
	class SystemHookBase {
	private:
		entt::id_type _systemType;

	public:
		SystemHookBase(entt::id_type systemType) : _systemType(systemType) {}
		virtual ~SystemHookBase() {}

		entt::id_type getType() const {
			return _systemType;
		}

		virtual void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const {}

		virtual void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const {}

		virtual void onAfterLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const {}

		virtual void onDestroy(entt::registry& registry, entt::entity entity) const {}

		virtual fw::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity) const { return nullptr; }

		virtual void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const {}

		virtual void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const {}
	};

	template <typename SystemComponent>
	class SystemHook : public SystemHookBase {
	public:
		SystemHook() : SystemHookBase(entt::type_id<SystemComponent>().index()) {}

		void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const override {
			onBeforeLoad(registry, entity, load, registry.get<SystemComponent>(entity));
		}

		virtual void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SystemComponent& component) const {}

		void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const override {
			onLoad(registry, entity, load, registry.get<SystemComponent>(entity));
		}

		virtual void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SystemComponent& system) const {}

		void onAfterLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const override {
			onAfterLoad(registry, entity, load, registry.get<SystemComponent>(entity));
		}

		virtual void onAfterLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SystemComponent& component) const {}

		void onDestroy(entt::registry& registry, entt::entity entity) const override {
			onDestroy(registry, entity, registry.get<SystemComponent>(entity));
		}

		virtual void onDestroy(entt::registry& registry, entt::entity entity, SystemComponent& component) const {}

		fw::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity) const override {
			return onCreateOverlay(registry, entity, registry.get<SystemComponent>(entity));
		}

		virtual fw::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity, SystemComponent& system) const { return nullptr; }
	};

	inline void eachHook(entt::id_type systemType, const std::vector<std::unique_ptr<SystemHookBase>>& hooks, std::function<void(const SystemHookBase&)>&& func) {
		for (const std::unique_ptr<SystemHookBase>& hook : hooks) {
			if (hook->getType() == systemType) {
				func(*hook);
			}
		}
	}

	inline void eachHook(const std::vector<std::unique_ptr<SystemHookBase>>& hooks, std::function<void(const SystemHookBase&)>&& func) {
		for (const std::unique_ptr<SystemHookBase>& hook : hooks) {
			func(*hook);
		}
	}
}
