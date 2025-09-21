#pragma once

#include <vector>

#include <entt/entity/registry.hpp>

#include "foundation/Types.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/RegistryUtil.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "ecs/Tasks.h"
#include "ecs/TaskSchedulerGlobal.h"

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

		void onUpdate(f32 deltaTime);

		entt::registry& getRegistry() { return _registry; }

		const entt::registry& getRegistry() const { return _registry; }

		lsdj::Sav getLsdjSav(entt::entity system);

		lsdj::Ram getLsdjRam(entt::entity system);

		lsdj::Project getLsdjProject(entt::entity system);

		lsdj::Song getLsdjWorkingSong(entt::entity system);

		void invalidateSampleCacheItem(const std::string& path);

		int32 getNextEmptyKit(entt::entity system);

		uint32 getKitVersion(entt::entity system, uint32 kitId);

		void getKitNames(entt::entity system, std::unordered_map<rp::KitIndex, std::string>& target, bool includeUseCount);

		bool getKits(entt::entity system, std::vector<LsdjKitComponent>& target);

		const LsdjKitComponent* getKitComponent(entt::entity system, uint32 kitId) const;

		LsdjKitComponent* getKitComponent(entt::entity system, uint32 kitId);

		bool setKitComponent(entt::entity system, uint32 kitId, LsdjKitComponent&& comp);

		bool removeKitComponent(entt::entity system, uint32 kitId);

		rp::KitIndex addKitComponent(entt::entity system, LsdjKitComponent&& comp);

		fw::Uint8Buffer getKitSample(entt::entity system, uint32 kitId, uint32 sampleId);

		fw::Uint8Buffer getKitData(entt::entity system, uint32 kitId);

		LsdjComponent* getComponent(entt::entity system);

		lsdj::Rom getLsdjRom(const SystemStateComponent& systemState) const;

		lsdj::Rom getLsdjRom(entt::entity system) const;

		bool updateKit(entt::entity system, uint32 kitId);

		void setKitDirty(entt::entity system, uint32 kitId);

	private:
	};
}
