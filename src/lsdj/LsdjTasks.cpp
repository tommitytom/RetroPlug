#include "LsdjTasks.h"

#include "foundation/Event.h"
#include "core/RegistryUtil.h"
#include "core/RetroPlugProjectContext.h"
#include "lsdj/KitUtil.h"

namespace rp {
	void PatchKitTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		assert(_kitState.id != INVALID_KIT_INDEX);

		//spdlog::info("Patching kit {} for entity {}", _kitState.id, _system);

		std::optional<std::string> error = KitUtil::updateKit2(_kitState, _kitData, _sampleCache);

		if (error.has_value()) {
			setError(error.value());
		} else {
			setSuccess();
		}
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

		//spdlog::info("Patched kit {} for entity {}", _kitState.id, _system);
	}
}
