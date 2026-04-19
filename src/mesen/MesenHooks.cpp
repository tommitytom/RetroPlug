#include "MesenHooks.h"

#include <chrono>

#include "foundation/Replicator.h"
#include "foundation/OsPath.h"
#include "core/ProjectSerializer.h"
#include "core/RegistryUtil.h"
#include "foundation/FsUtil.h"
#include "core/RetroPlugComponents.h"

#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/KeyManager.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Core/Shared/Video/VideoDecoder.h"
#include "Core/NES/NesConsole.h"
#include "Core/NES/NesMemoryManager.h"
#include "Core/NES/NesTypes.h"
#include "Core/NES/APU/NesApu.h"
#include "Utilities/FolderUtilities.h"
//#include "Core/NES/APU/ApuChannel.h"		// ⚠ verify: per-channel blip access

#include "MesenAudioDevice.h"
#include "MesenVideoDevice.h"
#include "NesEverdriveFifo.h"

#ifndef FW_PLATFORM_WEB
#include "EdioProxy.h"
#include "core/EverdriveComponents.h"
#endif

namespace rp {
	void MesenHooks::onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const {
		filterEntries(paths, entries, ".nes", "rom");
	}

	void setupNes(Emulator& emu) {
		EmuSettings* emuSettings = emu.GetSettings();

		NesConfig nesConfig{
			.Port1 = {.Type = ControllerType::NesController },
			.Port2 = {.Type = ControllerType::NesController },
			.UserPalette = { 0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00, 0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08, 0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00, 0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32, 0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF, 0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22, 0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000, 0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5, 0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000 },
			.ChannelVolumes = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 },
		};

		emuSettings->SetNesConfig(nesConfig);
	}

	MesenSystemType getMesenSystemType(const std::string& filePath) {
		std::string ext = orb::StringUtil::toLower(std::filesystem::path(filePath).extension().string());
		if (ext == ".nes") return MesenSystemType::Nes;
		if (ext == ".sfc" || ext == ".smc") return MesenSystemType::Snes;
		if (ext == ".gb" || ext == ".gbc") return MesenSystemType::Gameboy;
		if (ext == ".pce" || ext == ".sgx") return MesenSystemType::PcEngine;
		if (ext == ".sms") return MesenSystemType::Sms;
		if (ext == ".cv") return MesenSystemType::Cv;
		if (ext == ".gba") return MesenSystemType::Gba;
		if (ext == ".ws") return MesenSystemType::Ws;
		return MesenSystemType::None;
	}

	void MesenHooks::onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, MesenComponent& system) const {
		std::string contentPath = orb::OsPath::getContentPath();
		contentPath += "./retroplug";

		FolderUtilities::SetHomeFolder(contentPath + "/mesen2");
		MessageManager::SetOptions(false, true);

		auto entry = load.findEntry("rom");
		MesenSystemType systemType = getMesenSystemType(entry->path);

		auto emu = std::make_unique<Emulator>();
		emu->Initialize();

		if (systemType == MesenSystemType::Nes) {
			setupNes(*emu);
		}

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

		MesenStateComponent& s = registry.emplace<MesenStateComponent>(entity);
		s.emulator = std::move(emu);

		// Create and register our capture device with the emulator's SoundMixer.
		s.audioDevice = std::make_shared<MesenAudioDevice>();
		s.emulator->GetSoundMixer()->RegisterAudioDevice(s.audioDevice.get());

		// Create and register our video capture device with the emulator's VideoRenderer.
		s.videoDevice = std::make_shared<MesenVideoDevice>();
		s.emulator->GetVideoRenderer()->RegisterRenderingDevice(s.videoDevice.get());

		if (systemType == MesenSystemType::Nes) {
			s.fifo = std::make_shared<NesEverdriveFifo>();

			// Use the directory containing the ROM as the SD card root.
			std::filesystem::path romDir = std::filesystem::path(entry->path).parent_path();
			fs::path contentPath = fs::path(orb::OsPath::getContentPath()) / "retroplug" / "n8sd";
			if (!fs::exists(contentPath)) {
				fs::create_directories(contentPath);
			}

			s.fifo->setSdRoot(contentPath.string());

			auto* nesConsole = dynamic_cast<NesConsole*>(s.emulator->GetConsole().get());
			nesConsole->GetMemoryManager()->RegisterIODevice(s.fifo.get());

			#ifndef FW_PLATFORM_WEB
			//registry.emplace<EverdriveComponent>(entity, std::make_shared<EdioProxy>());
			#endif
		}
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
