#include "LsdjController.h"

#include <liblsdj/liblsdj/src/song_offsets.h>

#include "foundation/Event.h"
#include "core/RetroPlugProjectContext.h"
#include "lsdj/LsdjTasks.h"
#include "lsdj/KitUtil.h"
#include "lsdj/SampleUtil.h"

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

			if (!state.dirtyKits.empty()) {
				const LsdjComponent& lsdj = _registry.get<LsdjComponent>(system);

				for (auto it = state.dirtyKits.begin(); it != state.dirtyKits.end(); ) {
					assert(*it != INVALID_KIT_INDEX);
					if (!state.patchingKits.contains(*it)) {
						TaskManager& taskManager = _registry.ctx().at<TaskManager>();
						lsdj::Rom lsdjRom = getLsdjRom(system);
						fw::Uint8Buffer kitData = lsdjRom.getKit(*it).getBuffer().clone();

						std::unique_ptr<PatchKitTask> task = std::make_unique<PatchKitTask>(system, *findKit(lsdj, *it), std::move(kitData), *state.sampleCache);
						taskManager.addTask(std::move(task));

						state.patchingKits.insert(*it);
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

	lsdj::Ram LsdjController::getLsdjRam(entt::entity system) {
		const SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, system);
		const LsdjStateComponent* lsdjState = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);

		if (systemState && lsdjState && lsdjState->ramOffsets.has_value()) {
			const VersionedMemory* ramData = systemState->find(MemoryType::Ram);
			if (ramData) {
				MemoryAccessor accessor(MemoryType::Ram, ramData->data.ref(), 0);
				return lsdj::Ram(accessor, lsdjState->ramOffsets.value());
			}
		}

		return lsdj::Ram();
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

	lsdj::Song LsdjController::getLsdjWorkingSong(entt::entity system) {
		SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, system);
		if (systemState) {
			VersionedMemory* savData = systemState->find(MemoryType::Sram);
			if (savData) {
				return lsdj::Song(savData->data);
			}
		}

		return lsdj::Song();
	}

	int32 LsdjController::getNextEmptyKit(entt::entity system) {
		lsdj::Rom rom = getLsdjRom(system);
		if (!rom.isValid()) return -1;
		return rom.nextEmptyKitIdx();
	}

	uint32 LsdjController::getKitVersion(entt::entity system, uint32 kitId) {
		const LsdjStateComponent *state = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);
		if (state) return state->kitVersions[kitId];
		return 0;
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
		assert(kitId != INVALID_KIT_INDEX);
		const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return nullptr;

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });
		if (found != lsdj->kits.end()) {
			return &(*found);
		}

		return nullptr;
	}

	LsdjKitComponent* LsdjController::getKitComponent(entt::entity system, uint32 kitId) {
		assert(kitId != INVALID_KIT_INDEX);
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return nullptr;

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });
		if (found != lsdj->kits.end()) {
			return &(*found);
		}

		return nullptr;
	}

	void LsdjController::setKitDirty(entt::entity system, uint32 kitId) {
		assert(kitId != INVALID_KIT_INDEX);
		LsdjStateComponent* lsdjState = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);
		if (!lsdjState) return;

		lsdjState->dirtyKits.insert(kitId);
	}

	bool LsdjController::updateKit(entt::entity system, uint32 kitId) {
		LsdjKitComponent* kitComponent = getKitComponent(system, kitId);
		if (!kitComponent) return false;

		LsdjStateComponent& lsdjState = _registry.get<LsdjStateComponent>(system);
		SystemStateComponent& systemState = _registry.get<SystemStateComponent>(system);
		VersionedMemory* romData = systemState.find(MemoryType::Rom);

		const size_t offset = lsdj::Rom::getKitBankOffset(kitId);
		fw::Uint8Buffer kitData = romData->data.slice(offset, lsdj::Rom::BANK_SIZE);
		KitUtil::updateKit2(*kitComponent, kitData, *lsdjState.sampleCache);

		romData->version++;
		_registry.ctx().at<RetroPlugProjectContext>().dirty = true;

		MemoryPatch patch;
		patch.type = MemoryType::Rom;
		patch.data = kitData.clone();
		patch.offset = lsdj::Rom::KIT_LOOKUP[kitId] * lsdj::Rom::BANK_SIZE;

		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();
		return ctx.eventNode.trySend("Audio"_hs, MemoryPatchEvent{
			.entity = system,
			.patches = { patch }
		});
	}

	bool LsdjController::setKitComponent(entt::entity system, uint32 kitId, LsdjKitComponent&& comp) {
		assert(kitId != INVALID_KIT_INDEX);
		assert(comp.kit.discrimininator_.string_view() != "empty"); // Use removeKitComponent instead

		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();

		comp.id = kitId;

		auto found = std::find_if(lsdj->kits.begin(), lsdj->kits.end(), [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });

		if (found == lsdj->kits.end()) {
			lsdj->kits.push_back(std::move(comp));
		} else {
			*found = std::move(comp);
		}

		lsdj::Rom rom = getLsdjRom(system);
		if (rom.isValid() && rom.kitIsEmpty(kitId)) {
			ctx.requiresReset = true;
		}

		ctx.dirty = true;

		LsdjStateComponent* lsdjState = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);
		if (lsdjState) {
			lsdjState->dirtyKits.insert(kitId);
		}

		return true;
	}

	bool LsdjController::removeKitComponent(entt::entity system, uint32 kitId) {
		assert(kitId != INVALID_KIT_INDEX);
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		std::erase_if(lsdj->kits, [kitId](const LsdjKitComponent& kit) { return kit.id == kitId; });
		_registry.ctx().at<RetroPlugProjectContext>().dirty = true;

		// Replace with original kit from ROM (may be empty, thats fine too)

		const SystemLoadComponent& load = _registry.get<SystemLoadComponent>(system);
		const SystemLoadEntry* entry = load.findEntry("rom");
		assert(entry);
		if (!entry) return false;

		lsdj::Rom sourceRom(entry->data());
		if (!sourceRom.isValid()) return false;

		lsdj::Rom targetRom = getLsdjRom(system);
		if (!targetRom.isValid()) return false;

		targetRom.getKit(kitId).setKitData(sourceRom.getKit(kitId).getBuffer());

		LsdjStateComponent *state = RegistryUtil::tryGet<LsdjStateComponent>(_registry, system);
		if (state) state->kitVersions[kitId]++;

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

	void LsdjController::invalidateSampleCacheItem(const std::string& path) {
		for (const auto& [e, lsdj, state] : _registry.view<LsdjComponent, LsdjStateComponent>().each()) {
			spdlog::info("Invalidating LSDJ sample cache item: {}", path);
			state.sampleCache->erase(path);

			for (const LsdjKitComponent& kit : lsdj.kits) {
				kit.kit.visit([&](const auto& field) {
					using Type = std::decay_t<decltype(field)>;
					if constexpr (std::is_same<Type, LsdjEditableKit>()) {
						const std::vector<LsdjSampleComponent>& samples = field.samples;
						auto found = std::find_if(samples.begin(), samples.end(), [&](const LsdjSampleComponent& sample) {
							return sample.path == path;
						});

						if (found != samples.end()) {
							state.dirtyKits.insert(kit.id);
						}
					}
				});
			}
		}
	}

	fw::Uint8Buffer LsdjController::getSynthData(entt::entity system, uint32 synthId) {
		lsdj::Song song = getLsdjWorkingSong(system);
		if (!song.isValid()) return fw::Uint8Buffer();
		return song.getSynthData((uint8)synthId);
	}

	bool LsdjController::setSynthData(entt::entity system, uint32 synthId, const fw::Uint8Buffer& data) {
		lsdj::Song song = getLsdjWorkingSong(system);
		if (!song.isValid()) return false;
		song.setSynthData((uint8)synthId, data);

		const size_t index = synthId * (LSDJ_WAVE_PER_SYNTH_COUNT + 1) * LSDJ_WAVE_BYTE_COUNT;
		assert(index < 4096);

		MemoryPatch patch;
		patch.type = MemoryType::Sram;
		patch.data = data;
		patch.offset = WAVES_OFFSET + index;

		MemoryPatch owPatch;
		owPatch.type = MemoryType::Sram;
		owPatch.data = song.getBuffer()[SYNTH_OVERWRITES_OFFSET + synthId / 8];
		owPatch.offset = SYNTH_OVERWRITES_OFFSET + synthId / 8;

		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();
		return ctx.eventNode.trySend("Audio"_hs, MemoryPatchEvent{
			.entity = system,
			.patches = { patch, owPatch }
		});

		return true;
	}

	bool LsdjController::getKits(entt::entity system, std::vector<LsdjKitComponent>& kits) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		kits = lsdj->kits;

		lsdj::Rom rom = getLsdjRom((entt::entity)system);
		if (rom.isValid()) {
			rom.eachKit([&](lsdj::Kit kit) {
				const uint32 kitIndex = (uint32)kit.getIndex();
				std::string name = std::string(kit.getName());

				auto found = std::find_if(kits.begin(), kits.end(), [&](const LsdjKitComponent& k) { return k.id == kitIndex; });
				if (found != kits.end()) {
					found->kit.visit(entt::overloaded{
						[&](LsdjRomKit& kit) { kit.name = kit.name.value_or(std::move(name)); },
						[&](LsdjPatchedKit& kit) { kit.name = kit.name.value_or(std::move(name)); },
						[&](const LsdjRomKit&) {},
						[&](const LsdjEmptyKit&) {},
						[&](const LsdjEditableKit&) {},
					});
				} else {
					kits.push_back({ .id = kitIndex, .kit = LsdjRomKit{ .name = std::move(name) } });
				}
			});
		}

		return true;
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
			assert(romData->data.size() == lsdj::Rom::ROM_SIZE);
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
