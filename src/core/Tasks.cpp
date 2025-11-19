#include "Tasks.h"

#include "core/ProjectBuilder.h"
#include "core/RetroPlugProjectContext.h"
#include "foundation/Replicator.h"
#include "core/RegistryUtil.h"
#include "core/CoreComponents.h"

namespace rp {
	void handleRegistryCopy(const HooksContext& hooks, entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) {
		RegistryUtil::moveComponent<SystemComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<SystemLoadComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<SystemStateComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);

		eachHook(hooks.serviceHooks, [&](const SystemHookBase& hook) { hook.onMoveComponents(sourceRegistry, sourceEntity, targetRegistry, targetEntity); });
		eachHook(hooks.systemHooks, [&](const SystemHookBase& hook) { hook.onMoveComponents(sourceRegistry, sourceEntity, targetRegistry, targetEntity); });
	}

	void handleReplicate(entt::registry& registry) {
		const HooksContext& ctx = registry.ctx().at<HooksContext>();
		eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onReplicate(registry); });
		eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onReplicate(registry); });
	}


	void LoadSystemTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		const bool success = ProjectBuilder::handleLoad(registry, entity, registry.get<SystemLoadComponent>(entity), systemType);
		setSuccess(success);
	}

	void LoadSystemTask::finalize(entt::registry& targetRegistry) {
		const HooksContext& ctx = registry.ctx().at<HooksContext>();
		handleRegistryCopy(ctx, this->registry, this->entity, targetRegistry, entity);
		handleReplicate(targetRegistry);

		targetRegistry.ctx().at<ProjectPathContext>() = std::move(registry.ctx().at<ProjectPathContext>());

		RetroPlugProjectContext& projectCtx = targetRegistry.ctx().at<RetroPlugProjectContext>();
		projectCtx.increaseVersion();
		projectCtx.loading = false;
	}


	void LoadProjectTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		const bool success = ProjectBuilder::loadFromPaths(registry, paths);
		setSuccess(success);
	}

	void LoadProjectTask::finalize(entt::registry& targetRegistry) {
		const HooksContext& ctx = registry.ctx().at<HooksContext>();
		for (const auto& [e, system] : targetRegistry.view<SystemComponent>().each()) {
			eachHook(ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onDestroy(targetRegistry, e); });
			eachHook(ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onDestroy(targetRegistry, e); });
			orb::Replicator::destroy(targetRegistry, e);
		}

		targetRegistry.ctx().at<ProjectPathContext>() = std::move(registry.ctx().at<ProjectPathContext>());

		for (const auto& [taskEntity, c] : this->registry.view<SystemComponent>().each()) {
			entt::entity targetEntity = orb::Replicator::spawn(targetRegistry);
			handleRegistryCopy(ctx, this->registry, taskEntity, targetRegistry, targetEntity);
		}

		handleReplicate(targetRegistry);
		RetroPlugProjectContext& projectCtx = targetRegistry.ctx().at<RetroPlugProjectContext>();
		projectCtx.version++;
		projectCtx.loading = false;
		projectCtx.dirty = targetRegistry.ctx().at<ProjectPathContext>().projectPath.empty();
	}
}
