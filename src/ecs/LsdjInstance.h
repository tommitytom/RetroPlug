#pragma once

#include "ecs/HierarchyUtil.h"
#include "ecs/RetroPlugComponents.h"

namespace rp {
	class ReplicatedHierarhcyAccessor {
	protected:
		entt::registry& _registry;
		entt::entity _entity;

	public:
		ReplicatedHierarhcyAccessor(entt::registry& registry, entt::entity entity): _registry(registry), _entity(entity) {}
		virtual ~ReplicatedHierarhcyAccessor() {}

		template <typename ...Components>
		using EachFunc = std::function<void(entt::entity, Components&...)>;

		template <typename Component>
		using CollectVector = std::vector<std::pair<entt::entity, Component&>>;

		template <typename Component>
		void spawnChild(entt::entity parent, const Component& comp) {
			entt::entity e = fw::Replicator::spawn(_registry);
			_registry.emplace<Component>(e, comp);
			HierarchyUtil::addChild(_registry, parent, e);
		}

		template <typename ...Components>
		void each(entt::entity entity, EachFunc<Components...>&& func) {
			assert(_registry.valid(entity));
			HierarchyUtil::eachAllOf(_registry, entity, std::forward<EachFunc<Components...>>(func));
		}

		template <typename Component>
		void collect(entt::entity entity, CollectVector<Component>& target) {
			assert(_registry.valid(entity));
			each<Component>(entity, [&](entt::entity e, Component& comp) { target.push_back({ e, comp }); });
		}
	};

	class LsdjInstance final : public ReplicatedHierarhcyAccessor {
	public:
		LsdjInstance(entt::registry& registry, entt::entity entity) : ReplicatedHierarhcyAccessor(registry, entity) {}
		~LsdjInstance() = default;

		size_t getKitCount() const {
			assert(_registry.valid(_entity));
			return HierarchyUtil::count(_registry, _entity);
		}

		void eachKit(EachFunc<const LsdjKitComponent>&& func) {
			each(_entity, std::forward<EachFunc<const LsdjKitComponent>>(func));
		}

		void getKits(std::vector<std::pair<entt::entity, const LsdjKitComponent&>>& target) {
			collect<const LsdjKitComponent>(_entity, target);
		}

		void addKit(entt::entity parent, const LsdjKitComponent& kit) {
			spawnChild(parent, kit);
		}

		void eachKitSample(entt::entity kitEntity, EachFunc<const LsdjSampleComponent>&& func) {
			assert(_registry.all_of<LsdjKitComponent>(kitEntity));
			each(kitEntity, std::forward<EachFunc<const LsdjSampleComponent>>(func));
		}

		void getKitSamples(entt::entity kitEntity, CollectVector<const LsdjSampleComponent>& target) {
			assert(_registry.all_of<LsdjKitComponent>(kitEntity));
			collect<const LsdjSampleComponent>(kitEntity, target);
		}

		void addKitSample(entt::entity kitEntity, const LsdjSampleComponent& sample) {
			assert(_registry.all_of<LsdjKitComponent>(kitEntity));
			spawnChild(kitEntity, sample);
		}

		void eachEffect(entt::entity entity, EachFunc<const LsdjKitEffect>&& func) {
			//assert(_registry.any_of<LsdjKitComponent, LsdjSampleComponent>(entity));
			each(entity, std::forward<EachFunc<const LsdjKitEffect>>(func));
		}

		void getEffects(entt::entity entity, CollectVector<const LsdjKitEffect>& target) {
			//assert(_registry.any_of<LsdjKitComponent, LsdjSampleComponent>(entity));
			collect<const LsdjKitEffect>(entity, target);
		}

		void addEffect(entt::entity entity, const LsdjKitEffect& effect) {
			//assert(_registry.any_of<LsdjKitComponent, LsdjSampleComponent>(entity));
			spawnChild(entity, effect);
		}
	};
}
