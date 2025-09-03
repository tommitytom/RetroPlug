#include "RetroPlugProject.h"

#include <spdlog/spdlog.h>

#include "foundation/Replicator.h"
#include "ecs/RetroPlugComponents.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/SameBoyHooks.h"
#include "ecs/LsdjHooks.h"
#include "ecs/EcsProjectSerializer.h"

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
		RetroPlugProjectContext& projectCtx = _registry.ctx().emplace<RetroPlugProjectContext>();
		projectCtx.addSystemHook<SameboyHooks>();
		projectCtx.addServiceHook<LsdjHooks>();

		fw::Replicator::subscribe(_registry, _eventNode, targetNodeId, true, false);
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
		const RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();
		resolveEntries(load);
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onBeforeLoad(_registry, entity, load); });
		eachHook(systemType, ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onLoad(_registry, entity, load); });
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onAfterLoad(_registry, entity, load); });
	}

	void RetroPlugProject::removeSystem(entt::entity entity) {
		const RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();
		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });
		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onDestroy(_registry, entity); });

		fw::Replicator::destroy(_registry, entity);
	}

	void RetroPlugProject::serialize(fw::Uint8Buffer& archive) const {
		std::string target;
		ProjectSerializer::serialize(_registry, target);
		archive.resize(target.size());
		archive.write((const uint8*)target.data(), target.size());
	}

	void RetroPlugProject::deserialize(const fw::Uint8Buffer& archive) {
		std::string_view source((const char*)archive.data(), archive.size());
		ProjectSerializer::deserialize(_registry, source);

		for (const auto& [e, system, load] : _registry.view<SystemComponent, SystemLoadComponent>().each()) {
			handleLoad(e, load, system.systemType);
		}
	}
}
