#include "SameBoyHooks.h"

#include <chrono>

#include "foundation/Replicator.h"
#include "sameboy/SameBoyUtil.h"
#include "core/ProjectSerializer.h"
#include "core/RegistryUtil.h"
#include "util/GameboyUtil.h"
#include "foundation/FsUtil.h"
#include "core/RetroPlugComponents.h"

namespace rp {
	void SameboyHooks::onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const {
		filterEntries(paths, entries, ".gb", "rom");
		filterEntries(paths, entries, ".gbc", "rom");
		filterEntries(paths, entries, ".sav", "sram");
		filterEntries(paths, entries, ".state", "state");
	}

	void SameboyHooks::onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const {
		SameBoyStateComponent& state = registry.emplace<SameBoyStateComponent>(entity);
		state.state.reset(new SameBoyState());

		if (!SameBoyUtil::setup(system, *state.state, 11050, load)) {
			registry.remove<SameBoyStateComponent>(entity);
			registry.emplace<ErrorComponent>(entity, "Failed to setup SameBoy instance");
			return;
		}

		if (system.fastBoot) {
			std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
			SameBoyUtil::spinMs(state.state->gb, 400);// Skip bootloader
			std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			spdlog::info("Fast boot completed in {} ms", duration);
		}

		SystemStateComponent& systemState = registry.get_or_emplace<SystemStateComponent>(entity);
		systemState.stateOffsets = SameBoyUtil::getStateOffsets(*state.state);
		SameBoyUtil::saveState(*state.state, systemState.state);

		for (size_t i = 0; i < (size_t)MemoryType::MAX; i++) {
			const MemoryType type = (MemoryType)i;
			const MemoryAccessor accessor = SameBoyUtil::getMemory(*state.state, type, AccessType::Read);

			if (type == MemoryType::Rom) {
				systemState.name = GameboyUtil::getRomName(accessor.getBuffer());
			} else if (type == MemoryType::Sram) {
				if (accessor.isValid() && !accessor.getBuffer().empty()) {
					systemState.saveType = SaveType::Sram;
				} else {
					systemState.saveType = SaveType::State;
				}
			}

			if (accessor.isValid() && !accessor.getBuffer().empty()) {
				systemState.memory.push_back(VersionedMemory{
					.type = type,
					.data = accessor.getBuffer().clone(),
					.version = 1,
					.subscriberCount = 0
				});
			}
		}

		// The following writes out a sav if one doesn't exist.
		// Deciding not to do this for now since it's perfectly valid to have an unsaved project
		// with no path decided yet
		/*
		const SystemLoadEntry* foundSram = load.findEntry("sram");
		if (!foundSram || (foundSram && foundSram->path.empty())) {
			// TODO: Does this rom actually use SRAM? Test with mgb or nanoloop demo
			const MemoryAccessor accessor = SameBoyUtil::getMemory(*state.state, MemoryType::Sram, AccessType::Read);
			if (accessor.isValid() && !accessor.getBuffer().empty()) {
				std::filesystem::path savPath = load.entries["rom"].path;
				savPath.replace_extension(".sav");

				if (!std::filesystem::exists(savPath)) {
					if (!orb::FsUtil::writeFile(savPath, accessor.getBuffer())) {
						spdlog::warn("Failed to write initial SRAM file: {}", savPath.string());
					}

					spdlog::info("Wrote initial SRAM file: {}", savPath.string());
					load.entries["sram"] = { savPath.string() };
				} else {
					spdlog::error("Sav data already exists! Not overwriting with empty SRAM");
				}
			}
		}
		*/
	}

	void SameboyHooks::onReplicate(entt::registry& registry) const {
		for (const auto& [e, c] : registry.view<SameBoyStateComponent>().each()) {
			orb::Replicator::emplaceRemote(registry, e, std::move(c));
			registry.remove<SameBoyStateComponent>(e);
		}
	}

	void SameboyHooks::onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const {
		RegistryUtil::moveComponent<SameBoyComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<SameBoyStateComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
	}

	void SameboyHooks::onReset(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const {

	}

	void SameboyHooks::onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const {
		ProjectSerializer::serializeComponent<SameBoyComponent>(registry, entity, ctx);
	}

	void SameboyHooks::onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const {
		if (ProjectSerializer::deserializeComponent<SameBoyComponent>(registry, entity, ctx)) {
			registry.emplace<SystemComponent>(entity, getType());
		}
	}
}
