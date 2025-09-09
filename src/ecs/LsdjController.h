#pragma once

#include <vector>

#include <entt/entity/registry.hpp>

#include "foundation/Types.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/RegistryUtil.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"

namespace rp {
	struct LsdjKitDesc {
		KitIndex id = -1;
		std::string name;
		bool editable = false;
		size_t useCount = 0;
	};

	class LsdjController {
	private:
		entt::registry& _registry;

	public:
		LsdjController(entt::registry& registry): _registry(registry) {}
		~LsdjController() = default;

		lsdj::Sav getLsdjSav(entt::entity system);

		lsdj::Project getLsdjProject(entt::entity system);

		int32 getNextEmptyKit(entt::entity system);

		void getKitNames(entt::entity system, std::unordered_map<rp::KitIndex, std::string>& target, bool includeUseCount);

		const LsdjKitComponent* getKitComponent(entt::entity system, uint32 kitId) const;

		bool setKitComponent(entt::entity system, uint32 kitId, const LsdjKitComponent& comp);

		bool setKitComponent(entt::entity system, uint32 kitId, LsdjKitComponent&& comp, std::vector<fw::Uint8Buffer>&& samples);

		bool removeKitComponent(entt::entity system, uint32 kitId);

		bool addKitComponent(entt::entity system, const LsdjKitComponent& comp);

		fw::Uint8Buffer getKitSample(entt::entity system, uint32 kitId, uint32 sampleId);

		fw::Uint8Buffer getKitData(entt::entity system, uint32 kitId);

		LsdjComponent* getComponent(entt::entity system);

		lsdj::Rom getLsdjRom(const SystemStateComponent& systemState) const;

		lsdj::Rom getLsdjRom(entt::entity system) const;

	private:
		bool updateKit(entt::entity system, uint32 kitId, const LsdjKitComponent& comp);
	};
}
