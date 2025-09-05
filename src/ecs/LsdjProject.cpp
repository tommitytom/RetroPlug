#include "LsdjProject.h"

#include "lsdj/KitUtil.h"
#include "lsdj/SampleUtil.h"
#include "ecs/RetroPlugProjectContext.h"

namespace rp {
	inline LsdjKitDesc* findKitDesc(std::vector<LsdjKitDesc>& target, uint32 id) {
		for (LsdjKitDesc& desc : target) {
			if (desc.id == id) {
				return &desc;
			}
		}
		return nullptr;
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

	void LsdjController::getKitDescs(entt::entity system, std::vector<LsdjKitDesc>& target, bool includeUseCount) {
		const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return;

		lsdj::Rom rom = getLsdjRom(system);
		if (rom.isValid()) {
			for (size_t i = 0; i < rom.getKitCount(); i++) {
				if (rom.kitIsEmpty(i)) continue;
				target.push_back({ (KitIndex)i, std::string(rom.getKitName(i)), false });
			}
		}

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
		}
	}

	const LsdjKitComponent* LsdjController::getKitComponent(entt::entity system, uint32 kitId) const {
		const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return nullptr;

		auto found = lsdj->kits.find(kitId);
		if (found != lsdj->kits.end()) {
			return &found->second;
		}

		return nullptr;
	}

	void processSamples(const LsdjKitComponent& comp, std::vector<std::pair<std::string, fw::Uint8Buffer>>& samples) {
		for (const LsdjSampleComponent& sample : comp.samples) {
			KitUtil::SampleData sampleData = KitUtil::loadSample(sample.data());

			fw::Float32Buffer resampled;
			KitUtil::convertSamplerate((f64)sampleData.sampleRate, (f64)KitUtil::GAMEBOY_SAMPLE_RATE, *sampleData.buffer, resampled);

			fw::Uint8Buffer data;
			lsdj::SampleUtil::convertF32ToNibbles(resampled, data, 0.0f);

			samples.push_back({ sample.name, data });
		}
	}

	bool LsdjController::updateKit(entt::entity system, uint32 kitId, const LsdjKitComponent& comp) {
		lsdj::Rom rom = getLsdjRom(system);
		if (!rom.isValid()) return false; // Return true as we updated the component, even if the ROM is invalid

		std::vector<std::pair<std::string, fw::Uint8Buffer>> samples;
		processSamples(comp, samples);

		lsdj::Kit kit = rom.getKit(kitId);
		//rom.setKitName(kitId, comp.name);
		kit.writeSamples(comp.name, samples);

		MemoryPatch patch;
		patch.type = MemoryType::Rom;
		patch.data = kit.getBuffer();
		patch.offset = lsdj::Rom::KIT_LOOKUP[kitId] * lsdj::Rom::BANK_SIZE;

		RetroPlugProjectContext& ctx = _registry.ctx().at<RetroPlugProjectContext>();
		return ctx.eventNode.trySend("Audio"_hs, MemoryPatchEvent{
			.patches = { patch }
		});
	}

	bool LsdjController::setKitComponent(entt::entity system, uint32 kitId, const LsdjKitComponent& comp) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		lsdj->kits[kitId] = comp;
		updateKit(system, kitId, lsdj->kits[kitId]);

		return true;
	}

	bool LsdjController::removeKitComponent(entt::entity system, uint32 kitId) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		lsdj->kits.erase(kitId);
		return true;
	}

	bool LsdjController::addKitComponent(entt::entity system, const LsdjKitComponent& comp) {
		LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
		if (!lsdj) return false;

		lsdj::Rom rom = getLsdjRom(system);
		if (!rom.isValid()) return false;

		lsdj::Kit nextEmpty = rom.getNextEmptyKit();
		if (!nextEmpty.isValid()) return false;

		const rp::KitIndex kitId = nextEmpty.getIndex();
		lsdj->kits[kitId] = comp;

		updateKit(system, kitId, lsdj->kits[kitId]);

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
