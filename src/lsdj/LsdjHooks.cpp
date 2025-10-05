#include "LsdjHooks.h"

#include "lsdj/LsdjSystemOverlay.h"
#include "core/ProjectSerializer.h"
#include "lsdj/LsdjController.h"
#include "core/RetroPlugComponents.h"
#include "lsdj/Sav.h"
#include "util/GameboyUtil.h"
#include "foundation/FsUtil.h"
#include "lsdj/KitUtil.h"
#include "lsdj/OffsetLookup.h"

namespace rp {
	std::string LsdjHooks::onGetSystemName(const entt::registry& registry, entt::entity entity) const {
		const LsdjComponent* comp = registry.try_get<LsdjComponent>(entity);
		if (!comp) {
			return "";
		}

		const SystemStateComponent* stateComp = registry.try_get<SystemStateComponent>(entity);
		if (!stateComp) {
			return "";
		}

		const VersionedMemory* sram = stateComp->find(MemoryType::Sram);
		if (!sram || sram->data.empty()) {
			return "";
		}

		// Get name of currently loaded song
		lsdj::Sav sav;
		sav.load(sram->data);

		if (!sav.isValid()) {
			return "";
		}

		lsdj::Song song = sav.getWorkingSong();

		lsdj::Project project = sav.getWorkingProject();
		if (!project.isValid()) {
			return "";
		}

		std::string projectName = std::string(project.getName());
		if (projectName.empty()) {
			return "";
		}

		return fmt::format("{} [{}]", projectName, stateComp->name);
	}

	void LsdjHooks::onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const {
		filterEntries(paths, entries, ".lsdsng", "lsdsng");
		filterEntries(paths, entries, ".lsdprj", "lsdprj");
		filterEntries(paths, entries, ".kit", "kit");

		NamedEntry* lsdsng = findEntry(entries, "lsdsng");
		NamedEntry* sram = findEntry(entries, "sram");

		if (!sram && lsdsng) {
			lsdj::Sav sav;

			fw::FsUtil::readFile(lsdsng->path.string(), lsdsng->data);
			if (!lsdsng->data.empty()) {
				lsdj::Project proj = lsdj::Project::fromLsdsng(lsdsng->data);

				if (proj.isValid()) {
					sav.setWorkingSong(proj.getSong());

					entries.push_back({ "sram", "", fw::Uint8Buffer() });

					if (!sav.save(entries.back().data)) {
						spdlog::warn("Failed to create initial save data");
						entries.back().data.clear();
					}
				} else {
					spdlog::warn("Failed to parse .lsdsng file for initial save data");
				}
			} else {
				spdlog::warn("Failed to open lsdsng at {}", lsdsng->path.string());
			}
		}
	}

	void LsdjHooks::onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const {
		fw::Uint8Buffer* romData = load.findData("rom");
		if (!romData) {
			// This should never happen
			spdlog::error("LSDJ system missing ROM data");
			return;
		}

		std::string_view romName = GameboyUtil::getRomName(*romData);
		std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
		if (shortName != "lsdj") {
			return;
		}

		LsdjStateComponent& lsdjState = registry.get_or_emplace<LsdjStateComponent>(entity);

		LsdjComponent* comp = registry.try_get<LsdjComponent>(entity);
		if (comp) {
			// Ensure that all kits are patched before loading
			for (const LsdjKitComponent& kit : comp->kits) {
				const size_t offset = lsdj::Rom::getKitBankOffset(kit.id);
				fw::Uint8Buffer kitData = romData->slice(offset, lsdj::Rom::BANK_SIZE);
				KitUtil::updateKit2(kit, kitData, *lsdjState.sampleCache);
			}
		} else {
			registry.emplace<LsdjComponent>(entity);
		}

		if (!load.findData("sram")) {
			lsdj::Sav sav;
			sav.save(load.entries["sram"].data());
		}

		MemoryAccessor buffer(MemoryType::Ram, romData->ref(), 0);
		lsdj::Rom rom(buffer);

		if (rom.isValid()) {
			lsdj::MemoryOffsets ramOffsets;
			if (lsdj::OffsetLookup::findOffsets(buffer.getBuffer(), ramOffsets, false)) {
				lsdjState.ramOffsets = ramOffsets;
			} else {
				spdlog::warn("Failed to find ROM offsets");
			}
		}
	}

	void LsdjHooks::onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const {
		RegistryUtil::moveComponent<LsdjComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<LsdjStateComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
	}

	fw::ViewPtr LsdjHooks::onCreateOverlay(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const {
		LsdjComponent* comp = registry.try_get<LsdjComponent>(entity);
		if (comp) {
			return std::make_shared<LsdjSystemOverlay>(entity, LsdjController{ registry });
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
