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
		targetRegistry.ctx().at<RetroPlugProjectContext>().version++;
	}


	void LoadProjectTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		const bool success = ProjectBuilder::loadFromPaths(registry, paths);
		setSuccess(success);
	}

	void LoadProjectTask::finalize(entt::registry& targetRegistry) {
		const HooksContext& ctx = registry.ctx().at<HooksContext>();
		for (const auto& [taskEntity, c] : this->registry.view<SystemComponent>().each()) {
			entt::entity targetEntity = fw::Replicator::spawn(targetRegistry);
			handleRegistryCopy(ctx, this->registry, taskEntity, targetRegistry, targetEntity);
		}

		handleReplicate(targetRegistry);
		targetRegistry.ctx().at<RetroPlugProjectContext>().version++;
	}


	void PatchKitTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		assert(_kitState.id != INVALID_KIT_INDEX);

		spdlog::info("Patching kit {} for entity {}", _kitState.id, _system);

		_kitData.resize(lsdj::Rom::BANK_SIZE);
		lsdj::Kit kit(MemoryAccessor(MemoryType::Rom, _kitData.ref(), 0), -1);

		bool success;
		if (_kitState.path.has_value()) {
			success = fw::FsUtil::readFile(_kitState.path.value(), _kitData);
			if (!success) setError("Failed to read kit file at " + _kitState.path.value());
		} else if (_kitState.samples.has_value()) {
			success = KitUtil::createKit(_sampleCache, kit, _kitState);
			if (!success) setError("Failed to create kit " + std::to_string(_kitState.id));
		} else if (!_kitState.name.empty()) {
			kit.setName(_kitState.name);
			success = true;
		} else {
			setError("No data to patch kit " + std::to_string(_kitState.id));
			success = false;
		}

		setSuccess(success);
	}

	void PatchKitTask::finalize(entt::registry& registry) {
		SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(registry, _system);
		if (!systemState) return;

		VersionedMemory* romData = systemState->find(MemoryType::Rom);
		if (!romData) return;

		lsdj::Rom rom(MemoryAccessor(MemoryType::Rom, romData->data.ref(), 0));
		rom.setKit(_kitState.id, _kitData);

		MemoryPatch patch;
		patch.type = MemoryType::Rom;
		patch.data = std::move(_kitData);
		patch.offset = lsdj::Rom::KIT_LOOKUP[_kitState.id] * lsdj::Rom::BANK_SIZE;

		RetroPlugProjectContext& ctx = registry.ctx().at<RetroPlugProjectContext>();
		ctx.eventNode.trySend("Audio"_hs, MemoryPatchEvent{
			.entity = _system,
			.patches = { patch }
		});

		LsdjStateComponent* lsdjState = registry.try_get<LsdjStateComponent>(_system);
		if (lsdjState) {
			lsdjState->kitVersions[_kitState.id]++;
			lsdjState->patchingKits.erase(_kitState.id);
		}

		spdlog::info("Patched kit {} for entity {}", _kitState.id, _system);
	}
}
