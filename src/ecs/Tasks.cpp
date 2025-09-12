#include "Tasks.h"

#include "ecs/ProjectBuilder.h"
#include "lsdj/KitUtil.h"
#include "ecs/RetroPlugProjectContext.h"
#include "foundation/Replicator.h"
#include "ecs/RegistryUtil.h"
#include "core/CoreComponents.h"

namespace rp {
	void handleRegistryCopy(const HooksContext& hooks, entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) {
		RegistryUtil::moveComponent<SystemComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<SystemLoadComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<SystemStateComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);

		eachHook(hooks.serviceHooks, [&](const SystemHookBase& hook) { hook.onMoveComponents(sourceRegistry, sourceEntity, targetRegistry, targetEntity); });
		eachHook(hooks.systemHooks, [&](const SystemHookBase& hook) { hook.onMoveComponents(sourceRegistry, sourceEntity, targetRegistry, targetEntity); });
	}

	void LoadSystemTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		success = ProjectBuilder::handleLoad(registry, entity, registry.get<SystemLoadComponent>(entity), systemType);
		completed = true;
	}

	void LoadSystemTask::finalize(entt::registry& targetRegistry, entt::entity entity) {
		const HooksContext& ctx = registry.ctx().at<HooksContext>();
		handleRegistryCopy(ctx, this->registry, this->entity, targetRegistry, entity);
	}

	void LoadProjectTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		success = ProjectBuilder::loadFromPaths(registry, paths);
		completed = true;
	}

	void LoadProjectTask::finalize(entt::registry& targetRegistry, entt::entity entity) {
		const HooksContext& ctx = registry.ctx().at<HooksContext>();
		for (const auto& [taskEntity, c] : this->registry.view<SystemComponent>().each()) {
			entt::entity targetEntity = fw::Replicator::spawn(targetRegistry);
			handleRegistryCopy(ctx, this->registry, taskEntity, targetRegistry, targetEntity);
		}
	}

	void PatchKitTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		assert(sampleCache);

		kitData.clear();
		lsdj::Kit kit(MemoryAccessor(MemoryType::Rom, kitData.ref(), 0), -1);
		success = KitUtil::createKit(*sampleCache, kit, kitState);
		completed = true;
	}

	void PatchKitTask::finalize(entt::registry& registry, entt::entity entity) {
		SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(registry, entity);
		if (!systemState) return;

		VersionedMemory* romData = systemState->find(MemoryType::Rom);
		if (!romData) return;

		lsdj::Rom rom(MemoryAccessor(MemoryType::Rom, romData->data.ref(), 0));
		rom.setKit(kitIndex, kitData);

		MemoryPatch patch;
		patch.type = MemoryType::Rom;
		patch.data = std::move(kitData);
		patch.offset = lsdj::Rom::KIT_LOOKUP[kitIndex] * lsdj::Rom::BANK_SIZE;

		RetroPlugProjectContext& ctx = registry.ctx().at<RetroPlugProjectContext>();
		ctx.eventNode.trySend("Audio"_hs, MemoryPatchEvent{
			.entity = system,
			.patches = { patch }
		});
	}
}
