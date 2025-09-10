#include "RetroPlugProject.h"

#include <spdlog/spdlog.h>

#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/SameBoyHooks.h"
#include "ecs/LsdjHooks.h"
#include "ecs/EcsProjectSerializer.h"
#include "core/Events.h"
#include "ecs/RetroPlugProjectContext.h"

namespace rp {
	bool resolveEntries(const RetroPlugProjectContext& ctx, SystemLoadComponent& load, const std::filesystem::path& rootPath) {
		bool error = false;

		for (auto& [type, entry] : load.entries) {
			if (entry.data().empty()) {
				std::filesystem::path path(entry.path);

				if (!path.is_absolute()) {
					path = (rootPath / path).lexically_normal();
				} else {
					path = path.lexically_normal();
				}

				if (!fw::FsUtil::readFile(path.string(), entry.data())) {
					error = true;
					spdlog::error("Failed to read file: {}", path.string());
				}
			}
		}

		return error;
	}

	RetroPlugProject::RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId) : _eventNode(std::move(eventNode)) {
		RetroPlugProjectContext& projectCtx = _registry.ctx().emplace<RetroPlugProjectContext>(_eventNode);
		projectCtx.addSystemHook<SameboyHooks>();
		projectCtx.addServiceHook<LsdjHooks>();

		_registry.ctx().emplace<ProjectConfig>();

#ifdef FW_PLATFORM_WEB
		projectCtx.mountPath = "/mount";
#endif

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

	void RetroPlugProject::loadConfigs() {

	}

	bool RetroPlugProject::loadFromFile(std::filesystem::path path) {
		spdlog::info("Loading project from file: {}", path.string());

		const RetroPlugProjectContext& ctx = getContext();
		if (!ctx.mountPath.empty()) {
			path = ctx.mountPath / path;
		}

		std::string data = fw::FsUtil::readTextFile(path);
		if (data.empty()) {
			spdlog::error("Failed to read project file: {}", path.string());
			return false;
		}

		_projectPath = path;

		return deserializeFromString(data, _projectPath.parent_path());
	}

	int32 indexOfExtension(const PathVector& paths, const std::string& ext) {
		for (size_t i = 0; i < paths.size(); ++i) {
			if (paths[i].extension() == ext) {
				return (int32)i;
			}
		}
		return -1;
	}

	bool RetroPlugProject::loadFromPaths(const PathVector& paths) {
		spdlog::info("Loading project from the following paths:");
		for (const auto& path : paths) { spdlog::info(" - {}", path.string()); }

		const int32 projIndex = indexOfExtension(paths, ".rplg");
		if (projIndex != -1) {
			// Just load the project
			return loadFromFile(paths[0]);
		}

		const RetroPlugProjectContext& ctx = getContext();
		NamedEntryVector entries;

		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onLoadRequest(_registry, paths, entries); });
		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onLoadRequest(_registry, paths, entries); });

		if (entries.empty()) {
			spdlog::error("Unable to load: Unrecognised path");
			return false;
		}

		return true;
	}

	bool RetroPlugProject::saveToFile(std::filesystem::path path) {
		const RetroPlugProjectContext& ctx = getContext();

		if (!ctx.mountPath.empty()) {
			path = ctx.mountPath / path;
		}

		_projectPath = path;
		_projectRoot = _projectPath.parent_path();

		// Ensure all entries are relative to the new project root!
		for (const auto& [e, c] : _registry.view<SystemLoadComponent>().each()) {
			for (auto& [k, v] : c.entries) {
				std::filesystem::path entryPath(v.path);
				if (entryPath.is_relative()) {
					v.path = (_projectRoot / entryPath).lexically_normal().string();
				}
			}
		}

		std::string data = serializeToString(_projectRoot);
		if (data.empty()) {
			spdlog::error("Failed to serialize project");
			return false;
		}
		if (!fw::FsUtil::writeTextFile(path, data)) {
			spdlog::error("Failed to write project file: {}", path.string());
			return false;
		}
		return true;
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
		RetroPlugProjectContext& ctx = getContext();

		resolveEntries(ctx, load, _projectRoot);
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onBeforeLoad(_registry, entity, load); });
		eachHook(systemType, ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onLoad(_registry, entity, load); });
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onAfterLoad(_registry, entity, load); });

		ctx.version++;
	}

	void RetroPlugProject::removeSystem(entt::entity entity) {
		RetroPlugProjectContext& ctx = getContext();

		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });
		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });

		fw::Replicator::destroy(_registry, entity);
		ctx.version++;
	}

	void RetroPlugProject::reset() {
		for (const auto& [e, system] : _registry.view<SystemComponent>().each()) {
			removeSystem(e);
		}

		_registry.ctx().at<ProjectConfig>() = ProjectConfig();
		_projectPath.clear();
		_projectRoot.clear();
	}

	void RetroPlugProject::serialize(fw::Uint8Buffer& archive, const std::filesystem::path& rootPath) const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		archive.resize(target.size());
		archive.write((const uint8*)target.data(), target.size());
	}

	std::string RetroPlugProject::serializeToString(const std::filesystem::path& rootPath) const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		return target;
	}

	bool RetroPlugProject::deserializeFromString(std::string_view str, const std::filesystem::path& rootPath) {
		_projectRoot = rootPath;

		RetroPlugProjectContext& ctx = getContext();
		if (!ProjectSerializer::deserialize(_registry, str)) {
			return false;
		}

		for (const auto& [e, system, load] : _registry.view<SystemComponent, SystemLoadComponent>().each()) {
			handleLoad(e, load, system.systemType);
		}

		spdlog::info("Project loaded");

		return true;
	}

	bool RetroPlugProject::deserialize(const fw::Uint8Buffer& archive, const std::filesystem::path& rootPath) {
		std::string_view source((const char*)archive.data(), archive.size());
		return deserializeFromString(source, rootPath);
	}
}
