#include "LsdjHooks.h"

#include "util/GameboyUtil.h"
#include "ecs/RetroPlugComponents.h"
#include "lsdj/Sav.h"
#include "ecs/EcsProjectSerializer.h"

namespace rp {
	void LsdjHooks::onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const {
		fw::Uint8Buffer* rom = load.findData("rom");
		if (!rom) {
			return;
		}

		std::string_view romName = GameboyUtil::getRomName(*rom);
		std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
		if (shortName != "lsdj") {
			return;
		}

		LsdjComponent* comp = registry.try_get<LsdjComponent>(entity);
		if (comp) {
			// TODO: Patch kits!
		} else {
			registry.emplace<LsdjComponent>(entity);
		}

		fw::Uint8Buffer* sram = load.findData("sram");
		if (!sram) {
			// LSDj has to initialize the SRAM if no save data is available when it starts
			// Create an SRAM buffer from an empty save to skip this init step

			lsdj::Sav sav;
			sav.save(load.entries["sram"].data());
		}
	}

	fw::ViewPtr LsdjHooks::onCreateOverlay(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const {
		LsdjComponent* comp = registry.try_get<LsdjComponent>(entity);
		if (comp) {
			return std::make_shared<EcsLsdjOverlay>(LsdjController{ registry });
		}

		return nullptr;
	}

	void LsdjHooks::onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const {
		ProjectSerializer::serializeComponent<LsdjComponent>(registry, entity, ctx);
	}

	void LsdjHooks::onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const {
		ProjectSerializer::deserializeComponent<LsdjComponent>(registry, entity, ctx);
	}
}
