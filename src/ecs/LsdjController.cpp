#include "LsdjController.h"

#include "lsdj/KitUtil.h"
#include "lsdj/SampleUtil.h"
#include "ecs/RetroPlugProjectContext.h"
#include "ecs/Tasks.h"
#include "foundation/Event.h"

namespace rp {
	inline LsdjKitDesc* findKitDesc(std::vector<LsdjKitDesc>& target, uint32 id) {
		for (LsdjKitDesc& desc : target) {
			if (desc.id == id) {
				return &desc;
			}
		}
		return nullptr;
	}

	const LsdjKitComponent* findKit(const LsdjComponent& lsdj, KitIndex kitId) {
		auto found = std::find_if(lsdj.kits.begin(), lsdj.kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });
		if (found != lsdj.kits.end()) {
			return &(*found);
		}
		return nullptr;
	}

	void LsdjController::onUpdate(f32 deltaTime) {
		for (const auto& [system, state] : _registry.view<LsdjStateComponent>().each()) {
			// Resolve any patching tasks
			for (auto it = state.patchTasks.begin(); it != state.patchTasks.end(); ) {
				if (it->second->completed) {
					if (it->second->success) {
						it->second->finalize(_registry, system);
					} else {
						spdlog::error("Failed to patch kit {}", it->first);
					}
					it = state.patchTasks.erase(it);
				} else {
					++it;
				}
			}

			if (!state.dirtyKits.empty()) {
				const LsdjComponent& lsdj = _registry.get<LsdjComponent>(system);

				for (auto it = state.dirtyKits.begin(); it != state.dirtyKits.end(); ) {
					if (!state.patchTasks.contains(*it)) {
						std::unique_ptr<PatchKitTask> task = std::make_unique<PatchKitTask>();
						task->system = system;
						task->kitIndex = *it;
						task->kitState = *findKit(lsdj, *it);
						task->kitData.resize(lsdj::Rom::BANK_SIZE);
						task->sampleCache = state.sampleCache.get();
						state.patchTasks[*it] = std::move(task);

						_registry.ctx().at<enki::TaskScheduler>().AddTaskSetToPipe(state.patchTasks[*it].get());

						it = state.dirtyKits.erase(it);
					} else {
						++it;
					}
				}
			}
		}
	}

	lsdj::Sav LsdjController::getLsdjSav(entt::entity system) {
		const SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, system);
		if (systemState) {
			const VersionedMemory* savData = systemState->find(MemoryType::Sram);
			if (savData) {
				return lsdj::Sav(savData->data);
			}
		}

		return lsdj::Sav();
	}

	lsdj::Project LsdjController::getLsdjProject(entt::entity system) {
		SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, system);
		if (systemState) {
			VersionedMemory* savData = systemState->find(MemoryType::Sram);
			if (savData) {
				return lsdj::Project(savData->data);
			}
		}

		return lsdj::Project();
	}

	int32 LsdjController::getNextEmptyKit(entt::entity system) {
		lsdj::Rom rom = getLsdjRom(system);
		if (!rom.isValid()) return -1;
		return rom.nextEmptyKitIdx();
	}

	void LsdjController::getKitNames(entt::entity system, std::unordered_map<rp::KitIndex, std::string>& target, bool includeUseCount) {
		const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return;

		lsdj::Rom rom = getLsdjRom(system);
		if (rom.isValid()) {
			rom.eachKit([&](lsdj::Kit kit) {
				target[kit.getIndex()] = std::string(kit.getName());
			});
		}
/*
		for (const auto& [id, kit] : lsdj->kits) {
			LsdjKitDesc* found = findKitDesc(target, id);
			if (found) {
				found->editable = true;
			} else {
				target.push_back({ id, kit.name, true });
			}
		}

		if (includeUseCount) {
			//lsdj::Project project = getLsdjProject(system);

			lsdj::Sav sav = getLsdjSav(system);
			if (!sav.isValid()) return;

			lsdj::Project project = sav.getWorkingProject();

			if (project.isValid()) {
				lsdj::Song song = project.getSong();

				for (uint8 i = 0; i < song.getInstrumentCount(); ++i) {
					lsdj::Instrument instr = song.getInstrument(i);
					if (!instr.isValid()) continue;

					if (instr.getType() == lsdj_instrument_type_t::LSDJ_INSTRUMENT_TYPE_KIT) {
						LsdjKitDesc* found1 = findKitDesc(target, instr.getKit1());
						if (found1) found1->useCount++;
						LsdjKitDesc* found2 = findKitDesc(target, instr.getKit2());
						if (found2) found2->useCount++;
					}
				}
			}
		}*/
	}

	const LsdjKitComponent* LsdjController::getKitComponent(entt::entity system, uint32 kitId) const {
		const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return nullptr;

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });
		if (found != lsdj->kits.end()) {
			return &(*found);
		}

		return nullptr;
	}

	LsdjKitComponent* LsdjController::getKitComponent(entt::entity system, uint32 kitId) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return nullptr;

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });
		if (found != lsdj->kits.end()) {
			return &(*found);
		}

		return nullptr;
	}

	void LsdjController::setKitDirty(entt::entity system, uint32 kitId) {
		LsdjStateComponent* lsdjState = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);
		if (!lsdjState) return;

		lsdjState->dirtyKits.insert(kitId);
	}

	bool LsdjController::updateKit(entt::entity system, uint32 kitId) {
		lsdj::Rom rom = getLsdjRom(system);
		if (!rom.isValid()) return false; // Return true as we updated the component, even if the ROM is invalid

		LsdjKitComponent* kitComponent = getKitComponent(system, kitId);
		if (!kitComponent) return false;

		LsdjStateComponent& state = _registry.get<LsdjStateComponent>(system);
		lsdj::Kit kit = rom.getKit(kitId);
		KitUtil::createKit(*state.sampleCache.get(), kit, *kitComponent);

		MemoryPatch patch;
		patch.type = MemoryType::Rom;
		patch.data = kit.getBuffer().clone();
		patch.offset = lsdj::Rom::KIT_LOOKUP[kitId] * lsdj::Rom::BANK_SIZE;

		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();
		return ctx.eventNode.trySend("Audio"_hs, MemoryPatchEvent{
			.entity = system,
			.patches = { patch }
		});
	}

	bool LsdjController::setKitComponent(entt::entity system, uint32 kitId, LsdjKitComponent&& comp) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });

		if (found == lsdj->kits.end()) {
			lsdj->kits.push_back(std::move(comp));
		} else {
			*found = std::move(comp);
		}

		//updateKit(system, kitId);

		LsdjStateComponent* lsdjState = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);
		lsdjState->dirtyKits.insert(comp.id);

		return true;
	}

	bool LsdjController::setKitComponent(entt::entity system, uint32 kitId, LsdjKitComponent&& comp, std::vector<fw::Uint8Buffer>&& samples) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		/*if (comp.samples.has_value()) {
			auto& compSamples = comp.samples.value();
			if (compSamples.size() == samples.size()) {
				for (size_t i = 0; i < samples.size(); ++i) {
					compSamples[i].data = std::move(samples[i]);
				}
			}
		} else {
			auto& compSamples = comp.samples.emplace();
			for (size_t i = 0; i < samples.size(); ++i) {
				compSamples[i].data = std::move(samples[i]);
			}
		}*/

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });

		if (found == lsdj->kits.end()) {
			lsdj->kits.push_back(std::move(comp));
		} else {
			*found = comp;
		}

		updateKit(system, kitId);

		return true;
	}

	bool LsdjController::removeKitComponent(entt::entity system, uint32 kitId) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		std::erase_if(lsdj->kits, [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });

		return true;
	}

	rp::KitIndex LsdjController::addKitComponent(entt::entity system, LsdjKitComponent&& comp) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return INVALID_KIT_INDEX;

		lsdj::Rom rom = getLsdjRom(system);
		if (!rom.isValid()) return INVALID_KIT_INDEX;

		lsdj::Kit nextEmpty = rom.getNextEmptyKit();
		if (!nextEmpty.isValid()) return INVALID_KIT_INDEX;

		const rp::KitIndex kitId = nextEmpty.getIndex();
		comp.id = kitId;
		lsdj->kits.push_back(std::move(comp));

		updateKit(system, kitId);

		return kitId;
	}

	LsdjComponent* LsdjController::getComponent(entt::entity system) {
		return RegistryUtil::tryGet<LsdjComponent>(_registry, system);
	}

	fw::Uint8Buffer LsdjController::getKitSample(entt::entity system, uint32 kitId, uint32 sampleId) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return fw::Uint8Buffer();

		lsdj::Rom rom = getLsdjRom(system);
		if (rom.isValid()) {
			if (kitId >= rom.getKitCount()) return fw::Uint8Buffer();

			lsdj::Kit kit = rom.getKit(kitId);
			if (!kit.isValid()) return fw::Uint8Buffer();

			return kit.getSampleData(sampleId);
		}

		return fw::Uint8Buffer();
	}

	fw::Uint8Buffer LsdjController::getKitData(entt::entity system, uint32 kitId) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return fw::Uint8Buffer();

		lsdj::Rom rom = getLsdjRom(system);
		if (rom.isValid()) {
			if (kitId >= rom.getKitCount()) return fw::Uint8Buffer();

			lsdj::Kit kit = rom.getKit(kitId);
			if (!kit.isValid()) return fw::Uint8Buffer();

			return kit.getBuffer();
		}

		return fw::Uint8Buffer();
	}

	lsdj::Rom LsdjController::getLsdjRom(const SystemStateComponent& systemState) const {
		const VersionedMemory* romData = systemState.find(MemoryType::Rom);
		if (romData) {
			return lsdj::Rom(MemoryAccessor(MemoryType::Rom, romData->data.ref(), 0));
		}

		return lsdj::Rom();
	}

	lsdj::Rom LsdjController::getLsdjRom(entt::entity system) const {
		const SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, system);
		if (systemState) {
			return getLsdjRom(*systemState);
		}

		return lsdj::Rom();
	}
}
