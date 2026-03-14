#include "MesenHooks.h"
#include "MesenAudioDevice.h"

#include <chrono>

#include "foundation/Replicator.h"
#include "core/ProjectSerializer.h"
#include "core/RegistryUtil.h"
#include "foundation/FsUtil.h"
#include "core/RetroPlugComponents.h"

#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/NES/NesConsole.h"
#include "Core/NES/NesTypes.h"
#include "Core/NES/APU/NesApu.h"
#include "Utilities/FolderUtilities.h"
//#include "Core/NES/APU/ApuChannel.h"		// ⚠ verify: per-channel blip access

namespace rp {
	void MesenHooks::onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const {
		filterEntries(paths, entries, ".nes", "rom");
	}

	void MesenHooks::onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, MesenComponent& system) const {
		auto emu = std::make_unique<Emulator>();
		emu->Initialize();
		FolderUtilities::SetHomeFolder("C:\\Users\\Tom\\Documents\\Mesen2");

		auto entry = load.findEntry("rom");

		// Mesen wraps file access in VirtualFile
		VirtualFile romFile(entry->path);
		if (!romFile.IsValid()) {
			spdlog::error("Failed to open ROM file: {}", entry->path);
			registry.emplace<ErrorComponent>(entity, "Failed to setup Mesen instance");
			return;
		}

		// LoadRom takes the game ROM and (optionally) a save file
		// Pass stopRom=false so InternalLoadRom does NOT call Stop() and does NOT
		// spawn _emuThread via the `if(stopRom)` branch. We drive the CPU ourselves
		// from the audio thread via cpu->Exec(), so Mesen must never run its own
		// emulation thread.
		if (!emu->LoadRom(romFile, VirtualFile(), /*stopRom=*/false)) {
			spdlog::error("Mesen failed to load ROM: {}", entry->path);
			registry.emplace<ErrorComponent>(entity, "Failed to setup Mesen instance");
			return;
		}

		registry.emplace<MesenStateComponent>(entity, std::move(emu));
	}

	void MesenHooks::onReplicate(entt::registry& registry) const {
		for (const auto& [e, c] : registry.view<MesenStateComponent>().each()) {
			orb::Replicator::emplaceRemote(registry, e, std::move(c));
			registry.remove<MesenStateComponent>(e);
		}
	}

	void MesenHooks::onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const {
		RegistryUtil::moveComponent<MesenComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
		RegistryUtil::moveComponent<MesenStateComponent>(sourceRegistry, sourceEntity, targetRegistry, targetEntity);
	}

	void MesenHooks::onReset(entt::registry& registry, entt::entity entity, MesenComponent& system) const {

	}

	void MesenHooks::onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const {
		ProjectSerializer::serializeComponent<MesenComponent>(registry, entity, ctx);
	}

	void MesenHooks::onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const {
		if (ProjectSerializer::deserializeComponent<MesenComponent>(registry, entity, ctx)) {
			registry.emplace<SystemComponent>(entity, getType());
		}
	}
}
