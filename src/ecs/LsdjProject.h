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

		void getKitDescs(entt::entity entity, std::vector<LsdjKitDesc>& target) const {
			const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, entity);
			if (!lsdj) return;

			SystemStateComponent* systemState = RegistryUtil::tryGet<SystemStateComponent>(_registry, entity);
			if (!systemState) return;

			const VersionedMemory* romData = systemState->find(MemoryType::Rom);
			spdlog::info("rom data: {}, size: {}", romData ? "valid" : "invalid", romData ? romData->data.size() : 0);
			if (romData) {
				lsdj::Rom rom(MemoryAccessor(MemoryType::Rom, romData->data.ref(), 0));

				spdlog::info("rom valid: {}", rom.isValid() ? "yes" : "no");
				if (rom.isValid()) {
					for (size_t i = 0; i < rom.getKitCount(); i++) {
						if (rom.kitIsEmpty(i)) continue;
						target.push_back({(KitIndex)i, std::string(rom.getKitName(i)), false});
					}
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

			spdlog::info("kit count {}", target.size());
		}

		const LsdjKitComponent* getKitComponent(entt::entity entity, uint32 kitId) const {
			const LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, entity);
			if (!lsdj) return nullptr;

			auto found = lsdj->kits.find(kitId);
			if (found != lsdj->kits.end()) {
				return &found->second;
			}

			return nullptr;
		}

		bool setKitComponent(entt::entity entity, uint32 kitId, const LsdjKitComponent& comp) {
			LsdjComponent* lsdj = RegistryUtil::tryGet<LsdjComponent>(_registry, entity);
			if (!lsdj) return false;

			lsdj->kits[kitId] = comp;
			return true;
		}
	};
}
