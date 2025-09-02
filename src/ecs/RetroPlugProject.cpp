#include "RetroPlugProject.h"

#include <spdlog/spdlog.h>

#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/SameBoyHooks.h"
#include "ecs/LsdjHooks.h"

namespace rp {
	bool resolveEntries(SystemLoadComponent& load) {
		bool error = false;

		for (auto& [type, entry] : load.entries) {
			if (entry.data.empty()) {
				if (!fw::FsUtil::readFile(entry.path, entry.data)) {
					error = true;
					spdlog::error("Failed to read file: {}", entry.path);
				}
			}
		}

		return error;
	}

	RetroPlugProject::RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId) : _eventNode(std::move(eventNode)) {
		_systemHooks.push_back(std::make_unique<SameboyHooks>());
		_serviceHooks.push_back(std::make_unique<LsdjHooks>());

		fw::Replicator::subscribe(_registry, _eventNode, targetNodeId, true);
		fw::Replicator::replicate<ReplicatedTypes>(_registry);

		_eventNode.receive<SystemIoEvent>([this](SystemIoEvent&& ev) {
			if (_registry.valid(ev.entity)) {
				_registry.emplace_or_replace<VideoFrameComponent>(ev.entity, std::move(ev.io->output.video));
			} else {
				spdlog::error("Received SystemIoEvent for invalid entity {}", ev.entity);
			}
		});
	}

	RetroPlugProject::~RetroPlugProject() {
		_eventNode.unsubscribe<SystemIoEvent>();
		fw::Replicator::shutdown(_registry);
	}

	void RetroPlugProject::onUpdate(f32 deltaTime) {
		// NOTE: Delta time is currently always 0
		fw::Replicator::beginUpdate(_registry);
		_eventNode.update();
		fw::Replicator::endUpdate(_registry);
	}

	void RetroPlugProject::handleLoad(entt::entity entity, SystemLoadComponent& load, entt::id_type systemType) {
		resolveEntries(load);
		eachHook(systemType, _serviceHooks, [&](const SystemHookBase& hook) { hook.onBeforeLoad(_registry, entity, load); });
		eachHook(systemType, _systemHooks, [&](const SystemHookBase& hook) { hook.onLoad(_registry, entity, load); });
		eachHook(systemType, _serviceHooks, [&](const SystemHookBase& hook) { hook.onAfterLoad(_registry, entity, load); });
	}

	void RetroPlugProject::removeSystem(entt::entity entity) {
		eachHook(_serviceHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });
		eachHook(_systemHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });

		fw::Replicator::destroy(_registry, entity);
	}
}
