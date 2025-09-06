#include "RetroPlugProject.h"

#include <spdlog/spdlog.h>

#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/SameBoyHooks.h"
#include "ecs/LsdjHooks.h"
#include "ecs/EcsProjectSerializer.h"
#include "core/Events.h"

namespace rp {
	bool resolveEntries(SystemLoadComponent& load) {
		bool error = false;

		for (auto& [type, entry] : load.entries) {
			if (entry.data().empty()) {
				if (!fw::FsUtil::readFile(entry.path, entry.data())) {
					error = true;
					spdlog::error("Failed to read file: {}", entry.path);
				}
			}
		}

		return error;
	}

	RetroPlugProject::RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId) : _eventNode(std::move(eventNode)) {
		RetroPlugProjectContext& projectCtx = _registry.ctx().emplace<RetroPlugProjectContext>(_eventNode);
		projectCtx.addSystemHook<SameboyHooks>();
		projectCtx.addServiceHook<LsdjHooks>();

		fw::Replicator::subscribe(_registry, _eventNode, targetNodeId, true, false);
		fw::Replicator::replicate<ReplicatedTypes>(_registry);

		_eventNode.receive<SystemIoEvent>([this](SystemIoEvent&& ev) {
			if (_registry.valid(ev.entity)) {
				_registry.emplace_or_replace<VideoFrameComponent>(ev.entity, std::move(ev.io->output.video));
			} else {
				spdlog::warn("Received SystemIoEvent for invalid entity {}", ev.entity);
			}
		});

		_eventNode.receive<FetchMemoryResponse>([this](FetchMemoryResponse&& ev) {
			if (!_registry.valid(ev.entity)) {
				spdlog::warn("Received FetchMemoryResponse for invalid entity {}", ev.entity);
				return;
			}

			SystemStateComponent* stateComp = _registry.try_get<SystemStateComponent>(ev.entity);
			if (!stateComp) {
				spdlog::warn("Received FetchMemoryResponse for entity {} without SystemStateComponent", ev.entity);
				return;
			}

			if (ev.type == MemoryType::MAX) {
				stateComp->state = std::move(ev.state);
				stateComp->stateFetchTimer = STATE_FETCH_INTERVAL;
				stateComp->lastStateUpdate = _totalTime;

				if (stateComp->stateOffsets.has_value()) {
					const SystemStateOffsets& offsets = *stateComp->stateOffsets;

					for (size_t i = 0; i < (size_t)MemoryType::MAX; i++) {
						const MemoryType type = (MemoryType)i;
						VersionedMemory* memory = stateComp->find(ev.type);
						if (memory) {
							fw::Uint8Buffer slice = stateComp->state.slice(offsets[i].offset, offsets[i].size);
							if (slice != memory->data) {
								memory->data.resize(slice.size());
								memory->data.write(slice);
								memory->version++;
							}
						}
					}
				}
			} else {
				stateComp->memoryFetchTimer = MEMORY_FETCH_INTERVAL;

				VersionedMemory* memory = stateComp->find(ev.type);
				if (!memory) {
					spdlog::warn("Received FetchMemoryResponse for entity {} for unsubscribed memory type {}", ev.entity, (int)ev.type);
					return;
				}

				if (ev.state != memory->data) {
					memory->data.resize(ev.state.size());
					memory->data.write(ev.state);
					memory->version++;
				}

				memory->lastUpdate = _totalTime;
			}
		});

		_eventNode.receive<PongEvent>([&](PongEvent&& ev) {
			_lastPongTime = std::chrono::high_resolution_clock::now();
			//std::chrono::nanoseconds duration = *_lastPongTime - ev.time;
			_lastPingTime = std::nullopt;
		});
	}

	RetroPlugProject::~RetroPlugProject() {
		_eventNode.unsubscribe<PongEvent>();
		_eventNode.unsubscribe<FetchMemoryResponse>();
		_eventNode.unsubscribe<SystemIoEvent>();
		fw::Replicator::shutdown(_registry);
	}

	uint32 RetroPlugProject::getMemoryVersion(entt::entity entity, MemoryType type) const {
		if (!_registry.valid(entity)) {
			return 0;
		}

		const SystemStateComponent* state = _registry.try_get<SystemStateComponent>(entity);
		if (state) {
			const VersionedMemory* memory = state->find(type);
			if (memory) {
				return memory->version;
			}
		}

		return 0;
	}

	bool RetroPlugProject::resetSystem(entt::entity system, bool remote) {
		return _eventNode.trySend("Audio"_hs, ResetSystemEntityEvent{ .entity = system });
	}

	MemoryAccessor RetroPlugProject::getSystemMemory(entt::entity entity, MemoryType type, AccessType access) {
		if (!_registry.valid(entity)) {
			return MemoryAccessor();
		}

		SystemStateComponent* state = _registry.try_get<SystemStateComponent>(entity);
		if (state) {
			VersionedMemory* memory = state->find(type);
			if (memory) {
				return MemoryAccessor(type, memory->data.ref(), 0);
			}
		}

		return MemoryAccessor();
	}

	std::vector<uint32> RetroPlugProject::getSystemIds() const {
		std::vector<uint32> ids;
		auto view = _registry.view<SystemComponent>();
		ids.reserve(view.size());
		for (entt::entity entity : view) {
			ids.push_back((uint32)entity);
		}

		return ids;
	}

	entt::entity RetroPlugProject::addSystem(const std::vector<std::string>& paths) {
		SystemLoadComponent load;
		eachHook(_registry.ctx().at<RetroPlugProjectContext>().systemHooks, [&](const SystemHookBase& hook) { hook.onLoadRequset(_registry, paths, load); });

		if (load.entries.find("rom") != load.entries.end()) {
			return addSystem(std::move(load), SameBoyComponent{});
		}

		spdlog::error("Failed to add system: No ROM entry found");

		return entt::null;
	}

	void RetroPlugProject::subscribeToMemory(entt::entity entity, MemoryType type) {
		SystemStateComponent& state = _registry.get<SystemStateComponent>(entity);

		auto found = std::find_if(state.memory.begin(), state.memory.end(), [type](const VersionedMemory& mem) { return mem.type == type; });
		if (found != state.memory.end()) {
			found->subscriberCount++;
			return;
		}

		state.memory.push_back(VersionedMemory{
			.type = type,
			.subscriberCount = 1
		});

		spdlog::debug("Subscribed to memory type {} for entity {}", (int)type, entity);
	}

	void RetroPlugProject::unsubscribeFromMemory(entt::entity entity, MemoryType type) {
		SystemStateComponent& state = _registry.get<SystemStateComponent>(entity);

		auto found = std::find_if(state.memory.begin(), state.memory.end(), [type](const VersionedMemory& mem) { return mem.type == type; });
		if (found == state.memory.end()) {
			spdlog::warn("Attempted to unsubscribe from memory type {} for entity {} which is not subscribed", (int)type, entity);
			return;
		}

		if (found->subscriberCount > 1) {
			found->subscriberCount--;
			return;
		}

		state.memory.erase(found);

		spdlog::debug("Unsubscribed from memory type {} for entity {}", (int)type, entity);
	}

	void RetroPlugProject::onUpdate(f32 deltaTime) {
		_totalTime += deltaTime;

		fw::Replicator::beginUpdate(_registry);
		_eventNode.update();
		fw::Replicator::endUpdate(_registry);

		for (const auto& [e, system] : _registry.view<SystemStateComponent>().each()) {
			if (system.stateFetchTimer.update(deltaTime)) {
				// Fetching MemoryType::MAX means fetching the entire state
				_eventNode.trySend("Audio"_hs, FetchMemoryRequest{ .entity = e, .type = MemoryType::MAX });
			}

			if (system.memoryFetchTimer.update(deltaTime)) {
				bool fetching = false;

				for (const VersionedMemory& mem : system.memory) {
					if (mem.subscriberCount > 0) {
						fetching |= _eventNode.trySend("Audio"_hs, FetchMemoryRequest{ .entity = e, .type = mem.type });
					}
				}

				if (!fetching) {
					system.memoryFetchTimer = MEMORY_FETCH_INTERVAL;
				}
			}
		}

		std::chrono::high_resolution_clock::time_point time = std::chrono::high_resolution_clock::now();

		if (_doPing && !_lastPingTime.has_value()) {
			_lastPingTime = time;
			_eventNode.send("Audio"_hs, PingEvent{ .time = time });
		}
	}

	void RetroPlugProject::handleLoad(entt::entity entity, SystemLoadComponent& load, entt::id_type systemType) {
		spdlog::info("Handling load for entity {} of system type {}", entity, systemType);

		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();

		_registry.emplace<SystemComponent>(entity, systemType);
		_registry.emplace<SystemStateComponent>(entity);

		resolveEntries(load);
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onBeforeLoad(_registry, entity, load); });
		eachHook(systemType, ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onLoad(_registry, entity, load); });
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onAfterLoad(_registry, entity, load); });

		ctx.version++;
	}

	void RetroPlugProject::removeSystem(entt::entity entity) {
		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();

		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });
		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });

		fw::Replicator::destroy(_registry, entity);
		ctx.version++;
	}

	void RetroPlugProject::clearSystems() {
		for (const auto& [e, system] : _registry.view<SystemComponent>().each()) {
			removeSystem(e);
		}
	}

	void RetroPlugProject::serialize(fw::Uint8Buffer& archive) const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		archive.resize(target.size());
		archive.write((const uint8*)target.data(), target.size());
	}

	std::string RetroPlugProject::serializeToString() const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		return target;
	}

	bool RetroPlugProject::deserializeFromString(std::string_view str) {
		ProjectSerializer::deserialize(_registry, str);

		for (const auto& [e, system, load] : _registry.view<SystemComponent, SystemLoadComponent>().each()) {
			handleLoad(e, load, system.systemType);
		}

		return true;
	}

	bool RetroPlugProject::deserialize(const fw::Uint8Buffer& archive) {
		std::string_view source((const char*)archive.data(), archive.size());
		return deserializeFromString(source);
	}
}
