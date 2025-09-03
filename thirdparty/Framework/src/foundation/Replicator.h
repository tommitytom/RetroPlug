#pragma once

#include <type_traits>
#include <concepts>

#include <entt/entity/registry.hpp>
#include <entt/entity/entity.hpp>

#include "foundation/Event.h"

namespace fw::Replicator {
	template<typename T, typename M>
	concept MemberPointerOf = requires {
		requires std::is_member_object_pointer_v<M>;
	};

	template<auto MemberPtr, typename Component, typename FieldType>
		requires std::is_member_object_pointer_v<decltype(MemberPtr)>
	void patchField(Component& obj, FieldType&& value) {
		obj.*MemberPtr = static_cast<std::remove_reference_t<decltype(obj.*MemberPtr)>>(
			std::forward<FieldType>(value)
		);
	}


	enum class ReplicatorState {
		Unsubscribed,
		RequestingState,
		Ready,
		Error
	};

	struct EmplaceComponentEvent;

	using ReplicatorFunction = void(*)(entt::registry&);
	using DereplicatorFunction = void(*)(entt::registry&);
	using EmplacerFunction = void(*)(entt::registry&, entt::entity, entt::any&&);
	using CollectorFunction = void(*)(entt::registry&, std::vector<EmplaceComponentEvent>&);
	using UpdaterFunction = void(*)(entt::registry&, entt::entity, entt::any&&);
	using PatcherFunction = void(*)(entt::registry&, entt::entity, entt::any&&);
	using DestroyerFunction = void(*)(entt::registry&, entt::entity);

	struct ReplicatorSubscription {
		EmplacerFunction emplacer;
		CollectorFunction collector;
		DereplicatorFunction dereplicator;
	};

	struct ReplicatorContext {
		fw::EventNode& eventNode;
		bool owner = false;
		bool canMutate = false;

		ReplicatorState state = ReplicatorState::Unsubscribed;
		bool receiving = false;

		std::vector<fw::EventNode::NodeId> targets;
		std::unordered_map<entt::id_type, ReplicatorSubscription> replicators;
	};

	struct RegistrySubscribeEvent {
		fw::EventNode::NodeId nodeId;
	};

	struct RegistryUnsubscribeEvent {
		fw::EventNode::NodeId nodeId;
	};

	struct CreateEntityEvent {
		entt::entity entity;
	};

	struct DestroyEntityEvent {
		entt::entity entity;
	};

	struct EmplaceComponentEvent {
		entt::entity entity;
		entt::any data;
		EmplacerFunction emplacer;
	};

	struct UpdateComponentEvent {
		entt::entity entity;
		entt::any data;
		UpdaterFunction updater;
	};

	struct PatchComponentFieldEvent {
		entt::entity entity;
		entt::any data;
		PatcherFunction patcher;
	};

	struct DestroyComponentEvent {
		entt::entity entity;
		DestroyerFunction destroyer;
	};

	struct StateRequestEvent {
		fw::EventNode::NodeId nodeId;
	};

	struct StateResponseEvent {
		std::vector<entt::entity> entities;
		std::vector<EmplaceComponentEvent> components;
	};

	template <typename Component> struct ComponentCreatedTag {};
	template <typename Component> struct ComponentUpdatedTag {};
	template <typename Component> struct ComponentDestroyedTag {};

	inline ReplicatorContext& getContext(entt::registry& registry) {
		return registry.ctx().at<ReplicatorContext>();
	}

	inline void toggleError(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);
		if (!ctx.owner) {
			ctx.state = ReplicatorState::Error;
		}
	}

	template <typename Component>
	void componentEmplacer(entt::registry& registry, entt::entity entity, entt::any&& data) {
		assert(data.owner());
		assert(getContext(registry).state == ReplicatorState::Ready);

		if (!registry.all_of<Component>(entity)) [[likely]] {
			registry.emplace<Component>(entity, std::move(entt::any_cast<Component&&>(data)));
			registry.emplace_or_replace<ComponentCreatedTag<Component>>(entity);
		} else {
			toggleError(registry);
		}
	}

	template <typename Component>
	void componentUpdater(entt::registry& registry, entt::entity entity, entt::any&& data) {
		assert(data.owner());
		assert(getContext(registry).state == ReplicatorState::Ready);

		if (registry.all_of<Component>(entity)) [[likely]] {
			registry.replace<Component>(entity, std::move(entt::any_cast<Component&&>(data)));
			registry.emplace_or_replace<ComponentUpdatedTag<Component>>(entity);
		} else {
			toggleError(registry);
		}
	}

	template<auto MemberPtr, typename Component, typename FieldType>
	void componentFieldPatcher(entt::registry& registry, entt::entity entity, entt::any&& data) {
		assert(data.owner());
		assert(getContext(registry).state == ReplicatorState::Ready);

		Component* comp = registry.try_get<Component>(entity);
		if (comp) [[likely]] {
			auto value = entt::any_cast<FieldType&&>(std::move(data));
			patchField<MemberPtr, Component, FieldType>(*comp, std::move(value));
			registry.emplace_or_replace<ComponentUpdatedTag<Component>>(entity);
		} else {
			toggleError(registry);
		}
	}

	template <typename Component>
	void componentDestroyer(entt::registry& registry, entt::entity entity) {
		assert(getContext(registry).state == ReplicatorState::Ready);

		if (registry.all_of<Component>(entity)) [[likely]] {
			registry.remove<Component>(entity);
			registry.emplace_or_replace<ComponentDestroyedTag<Component>>(entity);
		} else {
			toggleError(registry);
		}
	}

	template <typename Component>
	void componentCollector(entt::registry& registry, std::vector<EmplaceComponentEvent>& out) {
		auto view = registry.view<Component>();
		for (const auto& [e, comp] : view.each()) {
			out.push_back(EmplaceComponentEvent{
				.entity = e,
				.data = entt::make_any<Component>(comp),
				.emplacer = componentEmplacer<Component>
			});
		}
	}


	template <typename Component>
	void handleConstruct(entt::registry& registry, entt::entity e) {
		ReplicatorContext& ctx = getContext(registry);
		if (!ctx.receiving) {
			for (const fw::EventNode::NodeId target : ctx.targets) {
				ctx.eventNode.send(target, EmplaceComponentEvent{
					.entity = e,
					.data = entt::make_any<Component>(registry.get<Component>(e)),
					.emplacer = componentEmplacer<Component>
				});
			}
		}

		registry.emplace_or_replace<ComponentCreatedTag<Component>>(e);
	}

	template <typename Component>
	void handleUpdate(entt::registry& registry, entt::entity e) {
		ReplicatorContext& ctx = getContext(registry);
		if (!ctx.receiving) {
			for (const fw::EventNode::NodeId target : ctx.targets) {
				ctx.eventNode.send(target, UpdateComponentEvent{
					.entity = e,
					.data = entt::make_any<Component>(registry.get<Component>(e)),
					.updater = componentUpdater<Component>
				});
			}
		}

		registry.emplace_or_replace<ComponentUpdatedTag<Component>>(e);
	}

	template<auto MemberPtr, typename Component, typename FieldType>
	void handleFieldPatch(entt::registry& registry, entt::entity e, const FieldType& data) {
		ReplicatorContext& ctx = getContext(registry);
		if (!ctx.receiving) {
			for (const fw::EventNode::NodeId target : ctx.targets) {
				ctx.eventNode.send(target, PatchComponentFieldEvent{
					.entity = e,
					.data = entt::any(data),
					.patcher = componentFieldPatcher<MemberPtr, Component, FieldType>
				});
			}
		}

		registry.emplace_or_replace<ComponentUpdatedTag<Component>>(e);
	}

	template <typename Component>
	void handleDestroy(entt::registry& registry, entt::entity e) {
		ReplicatorContext& ctx = getContext(registry);
		if (!ctx.receiving) {
			for (const fw::EventNode::NodeId target : ctx.targets) {
				ctx.eventNode.send(target, DestroyComponentEvent{
					.entity = e,
					.destroyer = componentDestroyer<Component>
				});
			}
		}

		registry.emplace_or_replace<ComponentDestroyedTag<Component>>(e);
	}

	inline void handleErrorState(ReplicatorContext& ctx) {
		if (ctx.state == ReplicatorState::Error && !ctx.targets.empty()) {
			if (ctx.eventNode.trySend(ctx.targets[0], StateRequestEvent{ ctx.eventNode.getId() })) {
				ctx.state = ReplicatorState::RequestingState;
			}
		}
	}

	inline void beginUpdate(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);
		handleErrorState(ctx);
		ctx.receiving = true;
	}

	inline void endUpdate(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);
		handleErrorState(ctx);
		ctx.receiving = false;
	}

	void setupOwner(entt::registry& registry, fw::EventNode& eventNode);

	bool subscribe(entt::registry& registry, fw::EventNode& eventNode, fw::EventNode::NodeId targetNodeId, bool canMutate, bool requestState = true);

	bool unsubscribe(entt::registry& registry, fw::EventNode::NodeId ownerNodeId);

	template <typename Component>
	void replicate(entt::registry& registry);

	template <typename Component>
	void dereplicate(entt::registry& registry);

	template <typename Component>
	bool isReplicating(const ReplicatorContext& ctx) {
		return ctx.replicators.contains(entt::type_id<Component>().index());
	}

	inline ReplicatorState getState(entt::registry& registry) {
		return getContext(registry).state;
	}

	template <typename Component>
	void replicate(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);

		assert(ctx.canMutate);
		assert(!isReplicating<Component>(ctx));

		registry.on_construct<Component>().connect<handleConstruct<Component>>();
		registry.on_destroy<Component>().connect<handleDestroy<Component>>();
		registry.on_update<Component>().connect<handleUpdate<Component>>();

		ctx.replicators[entt::type_id<Component>().index()] = ReplicatorSubscription{
			.emplacer = componentEmplacer<Component>,
			.collector = componentCollector<Component>,
			.dereplicator = dereplicate<Component>
		};
	}

	// Overload for multiple components (variadic template)
	template <typename Component, typename... OtherComponents>
		requires (sizeof...(OtherComponents) > 0)  // Only enable when there's more than one component
	void replicate(entt::registry& registry) {
		replicate<Component>(registry);
		replicate<OtherComponents...>(registry);
	}

	// Helper to unpack type_list (internal implementation detail)
	template <typename... Components>
	void replicate_from_list(entt::registry& registry, entt::type_list<Components...>) {
		replicate<Components...>(registry);
	}

	// Overload for type_list
	template <typename TypeList>
		requires std::is_same_v<TypeList, typename TypeList::type>  // Check if it's a type_list
	void replicate(entt::registry& registry) {
		replicate_from_list(registry, TypeList{});
	}

	template <typename Component>
	void dereplicate(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);

		assert(isReplicating<Component>(ctx));

		ctx.replicators.erase(entt::type_id<Component>().index());

		registry.on_construct<Component>().disconnect<handleConstruct<Component>>();
		registry.on_destroy<Component>().disconnect<handleDestroy<Component>>();
		registry.on_update<Component>().disconnect<handleUpdate<Component>>();
	}

	entt::entity spawn(entt::registry& registry);

	entt::entity destroy(entt::registry& registry, entt::entity entity);

	template <typename Component>
	bool emplaceRemote(entt::registry& registry, entt::entity entity, Component&& component) {
		ReplicatorContext& ctx = getContext(registry);
		assert(ctx.canMutate);
		assert(!isReplicating<Component>(ctx));

		return ctx.eventNode.trySend(ctx.targets[0], EmplaceComponentEvent{
			.entity = entity,
			.data = entt::make_any<Component>(std::forward<Component>(component)),
			.emplacer = componentEmplacer<Component>
		});
	}

	template<typename T>
	struct member_pointer_class;

	template<typename T, typename C>
	struct member_pointer_class<T C::*> {
		using type = C;
	};

	template<auto MemberPtr>
	void patchField(entt::registry& registry, entt::entity entity, auto&& value) {
		using Component = typename member_pointer_class<decltype(MemberPtr)>::type;
		using FieldType = decltype(value);
		using ValueType = std::remove_cvref_t<FieldType>;

		assert(isReplicating<Component>(getContext(registry)));

		Component& component = registry.get<Component>(entity);
		
		handleFieldPatch<MemberPtr, Component, ValueType>(registry, entity, value);
		patchField<MemberPtr>(component, std::forward<ValueType>(value));
		
		registry.emplace_or_replace<ComponentUpdatedTag<Component>>(entity);
	}

	void shutdown(entt::registry& registry);
}
