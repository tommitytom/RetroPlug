#pragma once

#include "foundation/Event.h"

#include <entt/entity/registry.hpp>
#include <entt/entity/entity.hpp>

namespace fw::Replicator {
	enum class ReplicatorState {
		Unsubscribed,
		Ready,
		Error,
		RequestingState
	};

	struct EmplaceComponentEvent;

	using ReplicatorFunction = void(*)(entt::registry&);
	using DereplicatorFunction = void(*)(entt::registry&);
	using EmplacerFunction = void(*)(entt::registry&, entt::entity, entt::any&&);
	using CollectorFunction = void(*)(entt::registry&, std::vector<EmplaceComponentEvent>&);
	using UpdaterFunction = void(*)(entt::registry&, entt::entity, entt::any&&);
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
		std::unordered_map<ReplicatorFunction, ReplicatorSubscription> replicators;
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
	void componentUpdater(entt::registry& registry, entt::entity entity, entt::any&& data) {
		assert(data.owner());
		assert(getContext(registry).state == ReplicatorState::Ready);

		if (registry.all_of<Component>(entity)) [[likely]] {
			registry.replace<Component>(entity, std::move(entt::any_cast<Component&&>(data)));
		} else {
			toggleError(registry);
		}
	}

	template <typename Component>
	void componentDestroyer(entt::registry& registry, entt::entity entity) {
		assert(getContext(registry).state == ReplicatorState::Ready);

		if (registry.all_of<Component>(entity)) [[likely]] {
			registry.remove<Component>(entity);
		} else {
			toggleError(registry);
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

	bool subscribe(entt::registry& registry, fw::EventNode& eventNode, fw::EventNode::NodeId targetNodeId, bool canMutate);

	bool unsubscribe(entt::registry& registry, fw::EventNode::NodeId ownerNodeId);

	template <typename Component>
	void replicate(entt::registry& registry);

	template <typename Component>
	void dereplicate(entt::registry& registry);

	template <typename Component>
	bool isReplicating(const ReplicatorContext& ctx) {
		return ctx.replicators.contains(&replicate<Component>);
	}

	template <typename Component>
	void replicate(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);

		assert(ctx.canMutate);
		assert(!isReplicating<Component>(ctx));

		registry.on_construct<Component>().connect<handleConstruct<Component>>();
		registry.on_destroy<Component>().connect<handleDestroy<Component>>();
		registry.on_update<Component>().connect<handleUpdate<Component>>();

		ctx.replicators[&replicate<Component>] = ReplicatorSubscription{
			.emplacer = componentEmplacer<Component>,
			.collector = componentCollector<Component>,
			.dereplicator = dereplicate<Component>
		};
	}

	template <typename Component>
	void dereplicate(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);

		assert(isReplicating<Component>(ctx));

		ctx.replicators.erase(&replicate<Component>);

		registry.on_construct<Component>().disconnect<handleConstruct<Component>>();
		registry.on_destroy<Component>().disconnect<handleDestroy<Component>>();
		registry.on_update<Component>().disconnect<handleUpdate<Component>>();
	}

	entt::entity spawn(entt::registry& registry);

	entt::entity destroy(entt::registry& registry, entt::entity entity);

	void shutdown(entt::registry& registry);
}
