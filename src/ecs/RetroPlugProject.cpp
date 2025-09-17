#include "RetroPlugProject.h"

#include <spdlog/spdlog.h>
#include <TaskScheduler.h>

#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/SameBoyHooks.h"
#include "ecs/LsdjHooks.h"
#include "ecs/EcsProjectSerializer.h"
#include "core/Events.h"
#include "ecs/RetroPlugProjectContext.h"
#include "ecs/TaskSchedulerGlobal.h"
#include "foundation/FsUtil.h"

namespace rp {
	RetroPlugProject::RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId) : _eventNode(std::move(eventNode)) {
		HooksContext& hooksCtx = _registry.ctx().emplace<HooksContext>();
		hooksCtx.addSystemHook<SameboyHooks>();
		hooksCtx.addServiceHook<LsdjHooks>();

		_registry.ctx().emplace<RetroPlugProjectContext>(_eventNode);
		_registry.ctx().emplace<ProjectPathContext>();
		_registry.ctx().emplace<ProjectConfig>();
		_registry.ctx().emplace<TaskManager>().getScheduler().Initialize(8);

#ifdef FW_PLATFORM_WEB
		_registry.ctx().at<ProjectPathContext>().mountPath = "/mount";
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
		spdlog::info("Loading configs...");

		std::string data = fw::FsUtil::readTextFile("/mount/config/input.json");
		if (data.empty()) {
			spdlog::warn("Failed to read /mount/config/input.json");
			return;
		}

		rfl::Result<InputConfig> result = rfl::json::read<InputConfig>(data);
		if (!result) {
			spdlog::warn("Failed to parse /mount/config/input.json: {}", result.error().what());
			return;
		}

		if (_registry.ctx().contains<InputConfig>()) {
			_registry.ctx().at<InputConfig>() = std::move(result.value());
		} else {
			_registry.ctx().emplace<InputConfig>(std::move(result.value()));
		}

		spdlog::info("Loaded input config");
	}

	bool RetroPlugProject::loadFromFile(std::filesystem::path path) {
		getContext().increaseVersion();
		if (ProjectBuilder::loadFromFile(_registry, path)) {
			handleReplicate();
			return true;
		}
		return false;
	}

	TaskId RetroPlugProject::loadFromPathsAsync(PathVector paths) {
		getContext().loading = true;
		getContext().increaseVersion();

		std::unique_ptr<LoadProjectTask> loadTask = std::make_unique<LoadProjectTask>();
		loadTask->paths = std::move(paths);
		loadTask->registry.ctx().emplace<HooksContext>(_registry.ctx().at<HooksContext>());
		loadTask->registry.ctx().emplace<ProjectPathContext>(_registry.ctx().at<ProjectPathContext>());
		loadTask->registry.ctx().emplace<ProjectConfig>(_registry.ctx().at<ProjectConfig>());

		return addTask(std::move(loadTask));
	}

	bool RetroPlugProject::loadFromPaths(PathVector paths) {
		getContext().increaseVersion();
		if (ProjectBuilder::loadFromPaths(_registry, paths)) {
			handleReplicate();
			return true;
		}
		return false;
	}

	bool RetroPlugProject::saveToFile(std::filesystem::path path) {
		//getContext().increaseVersion();
		if (ProjectBuilder::saveToFile(_registry, path)) {
			getContext().dirty = false;
			return true;
		}

		return false;
	}

	bool RetroPlugProject::addSystem(SystemLoadComponent&& config) {
		getContext().increaseVersion();
		entt::entity entity = fw::Replicator::spawn(_registry);
		if (ProjectBuilder::addSystemWithConfig<SameBoyComponent>(_registry, entity, std::forward<SystemLoadComponent>(config), SameBoyComponent{})) {
			handleReplicate();
			return true;
		}

		return false;
	}

	void RetroPlugProject::removeSystem(entt::entity entity) {
		const HooksContext& ctx = getHooksContext();

		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });
		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });

		fw::Replicator::destroy(_registry, entity);
		getContext().increaseVersion();
	}

	bool RetroPlugProject::resetSystem(entt::entity system, bool remote) {
		if (_eventNode.trySend("Audio"_hs, ResetSystemEntityEvent{ .entity = system })) {
			getContext().requiresReset = false;
			return true;
		}

		return false;
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

	std::vector<uint32> RetroPlugProject::getSystemIds() const {
		std::vector<uint32> ids;
		auto view = _registry.view<SystemComponent>();
		ids.reserve(view.size());
		for (entt::entity entity : view) {
			ids.push_back((uint32)entity);
		}

		return ids;
	}

	void RetroPlugProject::handleFetchTimers(f32 deltaTime) {
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
	}

	void RetroPlugProject::handlePing() {
		std::chrono::high_resolution_clock::time_point time = std::chrono::high_resolution_clock::now();

		if (_doPing && !_lastPingTime.has_value()) {
			_lastPingTime = time;
			_eventNode.send("Audio"_hs, PingEvent{ .time = time });
		}
	}

	void RetroPlugProject::handleAsyncTasks() {
		TaskManager& taskManager = _registry.ctx().at<TaskManager>();
		taskManager.resolveFinishedTasks(_registry, _finishedTasks);
	}

	void RetroPlugProject::handleReplicate() {
		const HooksContext& ctx = getHooksContext();
		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onReplicate(_registry); });
		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onReplicate(_registry); });
	}

	std::string RetroPlugProject::getProjectName() const {
		std::string projectName;
		const HooksContext& hooksContext = getHooksContext();

		for (const auto& [e, c] : _registry.view<SystemStateComponent>().each()) {
			std::string systemName;

			eachHook(hooksContext.serviceHooks, [&](const SystemHookBase& hook) {
				std::string name = hook.onGetSystemName(_registry, e);
				if (!name.empty()) systemName = name;
			});

			if (!systemName.empty()) {
				projectName += (projectName.empty() ? "" : " + ") + systemName;
			} else if (!c.name.empty()) {
				projectName += (projectName.empty() ? "" : " + ") + c.name;
			} else {
				projectName += (projectName.empty() ? "" : " + ") + ("System " + std::to_string((uint32)e));
			}
		}

		return projectName;
	}

	void RetroPlugProject::onUpdate(f32 deltaTime) {
		_totalTime += deltaTime;

		// Receive events from the audio thread
		fw::Replicator::beginUpdate(_registry);
		_eventNode.update();
		fw::Replicator::endUpdate(_registry);

		handleFetchTimers(deltaTime); // Checks if we need to request state
		handlePing();
		handleAsyncTasks();

		LsdjController(_registry).onUpdate(deltaTime);
	}

	void RetroPlugProject::reset() {
		for (const auto& [e, system] : _registry.view<SystemComponent>().each()) {
			removeSystem(e);
		}

		_registry.ctx().at<ProjectConfig>() = ProjectConfig();
		ProjectPathContext& pathContext = _registry.ctx().at<ProjectPathContext>();
		pathContext.projectPath.clear();
		pathContext.projectRoot.clear();
	}

	void RetroPlugProject::serialize(fw::Uint8Buffer& archive, const std::filesystem::path& rootPath) const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		archive.resize(target.size());
		archive.write((const uint8*)target.data(), target.size());
	}

	std::string RetroPlugProject::serializeJson(const std::filesystem::path& rootPath) const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		return target;
	}

	bool RetroPlugProject::deserializeJson(std::string_view str, const std::filesystem::path& rootPath) {
		return ProjectBuilder::deserializeJson(_registry, str, rootPath);
	}

	bool RetroPlugProject::deserialize(const fw::Uint8Buffer& archive, const std::filesystem::path& rootPath) {
		std::string_view source((const char*)archive.data(), archive.size());
		return deserializeJson(source, rootPath);
	}
}
