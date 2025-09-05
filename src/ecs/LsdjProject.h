#pragma once

#include <vector>

#include <entt/entity/registry.hpp>

#include "foundation/Types.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/RegistryUtil.h"
#include "lsdj/Rom.h"

namespace rp {
	struct LsdjKitDesc {
		KitIndex id = -1;
		std::string name;
		bool editable = false;
	};

	class LsdjController {
	private:
		entt::registry& _registry;

	public:
		LsdjController(entt::registry& registry): _registry(registry) {}
		~LsdjController() = default;

		void getKitDescs(entt::entity system, std::vector<LsdjKitDesc>& target) const {
			const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
			if (!lsdj) return;

			lsdj::Rom rom = getLsdjRom(system);
			if (rom.isValid()) {
				for (size_t i = 0; i < rom.getKitCount(); i++) {
					if (rom.kitIsEmpty(i)) continue;
					target.push_back({(KitIndex)i, std::string(rom.getKitName(i)), false});
				}
			}

			for (const auto& [id, kit] : lsdj->kits) {
				auto found = std::find_if(target.begin(), target.end(), [id](const LsdjKitDesc& desc) { return desc.id == id; });
				if (found != target.end()) {
					found->editable = true;
				} else {
					target.push_back({id, kit.name, true});
				}
			}
		}

		const LsdjKitComponent* getKitComponent(entt::entity system, uint32 kitId) const {
			const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
			if (!lsdj) return nullptr;

			auto found = lsdj->kits.find(kitId);
			if (found != lsdj->kits.end()) {
				return &found->second;
			}

			return nullptr;
		}

		bool setKitComponent(entt::entity system, uint32 kitId, const LsdjKitComponent& comp) {
			LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
			if (!lsdj) return false;

			lsdj->kits[kitId] = comp;
			return true;
		}

		bool removeKitComponent(entt::entity system, uint32 kitId) {
			LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
			if (!lsdj) return false;

			lsdj->kits.erase(kitId);
			return true;
		}

		bool addKitComponent(entt::entity system, const LsdjKitComponent& comp) {
			LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, system);
			if (!lsdj) return false;

			lsdj::Rom rom = getLsdjRom(system);
			if (!rom.isValid()) return false;

			lsdj::Kit nextEmpty = rom.getNextEmptyKit();
			if (!nextEmpty.isValid()) return false;

			lsdj->kits[nextEmpty.getIndex()] = comp;

			return true;
		}

		fw::Uint8Buffer getKitSample(entt::entity system, uint32 kitId, uint32 sampleId) {
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

		fw::Uint8Buffer getKitData(entt::entity system, uint32 kitId) {
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

		lsdj::Rom getLsdjRom(const SystemStateComponent& systemState) const {
			const VersionedMemory* romData = systemState.find(MemoryType::Rom);
			if (romData) {
				return lsdj::Rom(MemoryAccessor(MemoryType::Rom, romData->data.ref(), 0));
			}

			return lsdj::Rom();
		}

		lsdj::Rom getLsdjRom(entt::entity system) const {
			const SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, system);
			if (systemState) {
				return getLsdjRom(*systemState);
			}

			return lsdj::Rom();
		}
	};
}
