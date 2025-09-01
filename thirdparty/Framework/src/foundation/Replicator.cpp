#include "Replicator.h"

#include <spdlog/spdlog.h>

namespace fw::Replicator {
	bool sendState(entt::registry& registry, fw::EventNode& eventNode, fw::EventNode::NodeId target) {
		ReplicatorContext& ctx = getContext(registry);
		StateResponseEvent response;

		for (const auto& [e, _] : registry.storage<entt::entity>().each()) {
			response.entities.push_back(e);
		}

		for (const auto& [_, sub] : ctx.replicators) {
			sub.collector(registry, response.components);
		}

		return eventNode.trySend(target, std::move(response));
	}

	void setupMutators(entt::registry& registry, fw::EventNode& eventNode) {
		ReplicatorContext& ctx = getContext(registry);

		eventNode.receive<CreateEntityEvent>([&registry, &ctx](CreateEntityEvent&& ev) {
			assert(ctx.state != ReplicatorState::Unsubscribed);

			if (ctx.state == ReplicatorState::Ready) [[likely]] {
				if (!registry.valid(ev.entity)) {
					[[maybe_unused]] const entt::entity created = registry.create(ev.entity);
					assert(created == ev.entity);
				} else {
					toggleError(registry);
				}
			}
		});

		eventNode.receive<DestroyEntityEvent>([&registry, &ctx](DestroyEntityEvent&& ev) {
			assert(ctx.state != ReplicatorState::Unsubscribed);

			if (ctx.state == ReplicatorState::Ready) [[likely]] {
				if (registry.valid(ev.entity)) {
					registry.destroy(ev.entity);
				} else {
					toggleError(registry);
				}
			}
		});

		eventNode.receive<EmplaceComponentEvent>([&registry, &ctx](EmplaceComponentEvent&& ev) {
			assert(ctx.state != ReplicatorState::Unsubscribed);

			if (ctx.state == ReplicatorState::Ready) [[likely]] {
				if (registry.valid(ev.entity)) {
					ev.emplacer(registry, ev.entity, std::move(ev.data));
				} else {
					toggleError(registry);
				}
			}
		});

		eventNode.receive<UpdateComponentEvent>([&registry, &ctx](UpdateComponentEvent&& ev) {
			assert(ctx.state != ReplicatorState::Unsubscribed);

			if (ctx.state == ReplicatorState::Ready) [[likely]] {
				if (registry.valid(ev.entity)) {
					ev.updater(registry, ev.entity, std::move(ev.data));
				} else {
					toggleError(registry);
				}
			}
		});

		eventNode.receive<PatchComponentFieldEvent>([&registry, &ctx](PatchComponentFieldEvent&& ev) {
			assert(ctx.state != ReplicatorState::Unsubscribed);

			if (ctx.state == ReplicatorState::Ready) [[likely]] {
				if (registry.valid(ev.entity)) {
					ev.patcher(registry, ev.entity, std::move(ev.data));
				} else {
					toggleError(registry);
				}
			}
		});

		eventNode.receive<DestroyComponentEvent>([&registry, &ctx](DestroyComponentEvent&& ev) {
			assert(ctx.state != ReplicatorState::Unsubscribed);

			if (ctx.state == ReplicatorState::Ready) [[likely]] {
				if (registry.valid(ev.entity)) {
					ev.destroyer(registry, ev.entity);
				} else {
					toggleError(registry);
				}
			}
		});
	}

	void Replicator::setupOwner(entt::registry& registry, fw::EventNode& eventNode) {
		ReplicatorContext& ctx = registry.ctx().emplace<ReplicatorContext>(eventNode, true, true, ReplicatorState::Ready);

		eventNode.receive<RegistrySubscribeEvent>([&registry, &ctx](RegistrySubscribeEvent&& ev) {
			sendState(registry, ctx.eventNode, ev.nodeId);
			ctx.targets.push_back(ev.nodeId);
		});

		eventNode.receive<RegistryUnsubscribeEvent>([&registry, &ctx](RegistryUnsubscribeEvent&& ev) {
			ctx.targets.erase(std::remove(ctx.targets.begin(), ctx.targets.end(), ev.nodeId), ctx.targets.end());
		});

		eventNode.receive<StateRequestEvent>([&registry, &ctx](StateRequestEvent&& ev) {
			sendState(registry, ctx.eventNode, ev.nodeId);
		});

		setupMutators(registry, eventNode);
	}

	bool Replicator::subscribe(entt::registry& registry, fw::EventNode& eventNode, fw::EventNode::NodeId targetNodeId, bool canMutate) {
		if (!eventNode.trySend(targetNodeId, RegistrySubscribeEvent{ .nodeId = eventNode.getId() })) {
			return false;
		}

		ReplicatorContext& ctx = registry.ctx().emplace<ReplicatorContext>(eventNode, false, canMutate, ReplicatorState::Ready);
		ctx.targets.push_back(targetNodeId);

		eventNode.receive<StateResponseEvent>([&registry, &ctx](StateResponseEvent&& ev) {
			registry.clear();

			for (entt::entity entity : ev.entities) {
				[[maybe_unused]] const entt::entity created = registry.create(entity);
				assert(created == entity);
			}

			for (EmplaceComponentEvent& component : ev.components) {
				assert(registry.valid(component.entity));
				component.emplacer(registry, component.entity, std::move(component.data));
			}

			ctx.state = ReplicatorState::Ready;
		});

		setupMutators(registry, eventNode);

		return true;
	}

	bool Replicator::unsubscribe(entt::registry& registry, fw::EventNode::NodeId ownerNodeId) {
		ReplicatorContext& ctx = getContext(registry);
		assert(ctx.state == ReplicatorState::Ready);

		if (!ctx.eventNode.trySend(ownerNodeId, RegistryUnsubscribeEvent{ .nodeId = ctx.eventNode.getId() })) {
			ctx.state = ReplicatorState::Unsubscribed;
			ctx.canMutate = false;
			ctx.targets.clear();
			return true;
		}

		return false;
	}

	entt::entity Replicator::spawn(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);
		assert(!ctx.receiving);

		entt::entity e = registry.create();

		for (const fw::EventNode::NodeId target : ctx.targets) {
			ctx.eventNode.send(target, CreateEntityEvent{
				.entity = e
				});
		}

		return e;
	}

	entt::entity Replicator::destroy(entt::registry& registry, entt::entity entity) {
		ReplicatorContext& ctx = getContext(registry);
		assert(!ctx.receiving);
		assert(registry.valid(entity));

		for (const fw::EventNode::NodeId target : ctx.targets) {
			ctx.eventNode.send(target, DestroyEntityEvent{
				.entity = entity
			});
		}

		registry.destroy(entity);

		return entity;
	}

	void Replicator::shutdown(entt::registry& registry) {
		ReplicatorContext& ctx = getContext(registry);

		if (ctx.owner) {
			ctx.eventNode.unsubscribe<RegistrySubscribeEvent>();
			ctx.eventNode.unsubscribe<RegistryUnsubscribeEvent>();
			ctx.eventNode.unsubscribe<StateRequestEvent>();
		} else {
			ctx.eventNode.unsubscribe<StateResponseEvent>();

			if (ctx.state != ReplicatorState::Unsubscribed) {
				assert(ctx.targets.size() == 1);
				unsubscribe(registry, ctx.targets[0]);
			}
		}

		ctx.eventNode.unsubscribe<CreateEntityEvent>();
		ctx.eventNode.unsubscribe<DestroyEntityEvent>();
		ctx.eventNode.unsubscribe<EmplaceComponentEvent>();
		ctx.eventNode.unsubscribe<UpdateComponentEvent>();
		ctx.eventNode.unsubscribe<DestroyComponentEvent>();

		// Dereplicator functions modify ctx.replicators, so we need to make a copy first
		std::vector<DereplicatorFunction> dereplicators;
		for (const auto& replicator : ctx.replicators) {
			dereplicators.push_back(replicator.second.dereplicator);
		}

		for (const DereplicatorFunction& func : dereplicators) {
			func(registry);
		}

		registry.ctx().erase<ReplicatorContext>();
	}
}
