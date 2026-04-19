#include "MesenAudioHooks.h"

#include "MesenComponents.h"
#include "MesenAudioDevice.h"
#include "MesenVideoDevice.h"
#include "NesEverdriveFifo.h"

#ifndef FW_PLATFORM_WEB
#include "EdioProxy.h"
#include "core/EverdriveComponents.h"
#endif

#include "core/AudioEffect.h"
#include "core/AudioSettingsContext.h"
#include "core/HierarchyUtil.h"

#include "Core/NES/NesConsole.h"
#include "Core/NES/Input/NesController.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/APU/NesApu.h"
#include "Core/NES/NesSoundMixer.h"
#include "Core/NES/BaseNesPpu.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/EventType.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/SettingTypes.h"

MemoryType getNesMemoryType(rp::MemoryType type) {
	switch (type) {
	case rp::MemoryType::Ram: return MemoryType::NesInternalRam;
	case rp::MemoryType::Rom: return MemoryType::NesPrgRom;
	case rp::MemoryType::Sram: return MemoryType::NesSaveRam;
	case rp::MemoryType::Vram: return MemoryType::NesChrRam;
	default: return MemoryType::None;
	}
}

namespace rp {
	MesenAudioHooks::MesenAudioHooks() : AudioSystemHook(entt::type_id<MesenComponent>().index()) {}

	void MesenAudioHooks::onSaveState(entt::registry& registry, entt::entity entity, orb::Uint8Buffer& target) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
	}

	MemoryAccessor MesenAudioHooks::onGetMemory(entt::registry& registry, entt::entity entity, rp::MemoryType type, AccessType access) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		auto nesType = getNesMemoryType(type);
		ConsoleMemoryInfo memoryInfo = state.emulator->GetMemory(nesType);
		return MemoryAccessor(type, orb::Uint8Buffer(static_cast<uint8*>(memoryInfo.Memory), static_cast<size_t>(memoryInfo.Size)), 0);
	}

	void MesenAudioHooks::onPatchMemory(entt::registry& registry, entt::entity entity, const MemoryPatch& patch) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
	}

	void MesenAudioHooks::onReset(entt::registry& registry, entt::entity entity) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		state.emulator->Reset();
	}

	std::optional<NesController::Buttons> toNesButton(orb::ButtonType button) {
		switch (button) {
		case orb::ButtonType::Right: return NesController::Buttons::Right;
		case orb::ButtonType::Left: return NesController::Buttons::Left;
		case orb::ButtonType::Up: return NesController::Buttons::Up;
		case orb::ButtonType::Down: return NesController::Buttons::Down;
		case orb::ButtonType::A: return NesController::Buttons::A;
		case orb::ButtonType::B: return NesController::Buttons::B;
		case orb::ButtonType::Select: return NesController::Buttons::Select;
		case orb::ButtonType::Start: return NesController::Buttons::Start;
		}

		return std::nullopt;
	}

	void MesenAudioHooks::onProcess(entt::registry& registry, orb::AudioBuffer& out, const orb::AudioBuffer& in) const {
		onCreate<MesenStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
			MesenStateComponent& s = registry.get<MesenStateComponent>(entity);

			EmuSettings* emuSettings = s.emulator->GetSettings();

			// Tell Mesen to render at the plugin's sample rate so we receive
			// samples at exactly the rate the host expects.
			AudioConfig audioCfg = emuSettings->GetAudioConfig();
			audioCfg.SampleRate = (uint32_t)settings.sampleRate;
			emuSettings->SetAudioConfig(audioCfg);

			// Register the audio thread as the emulation thread so that
			// IsEmulationThread() returns true throughout Mesen's internals
			// (e.g. NesApu::PeekRam, debug guards) when we drive cpu->Exec().
			s.emulator->SetEmulationThreadId(std::this_thread::get_id());

			registry.emplace<EverdriveComponent>(entity, std::make_shared<EdioProxy>());
		});

		onDestroy<MesenStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			registry.remove<MesenStateComponent>(entity);
		});

		const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();

		for (const auto& [e, s] : registry.view<MesenStateComponent>().each()) {
			if (!s.audioDevice) {
				continue;
			}

			SystemIo& io = *registry.get<SystemIoComponent>(e).io;
			orb::Float32BufferPtr outSamples = io.output.audio;
			if (!outSamples) {
				continue;
			}

			auto console = dynamic_cast<NesConsole*>(s.emulator->GetConsole().get());
			if (!console) {
				continue;
			}

			auto controller = console->GetControlManager()->GetControlDeviceByIndex(0);

			for (const auto& press : io.input.buttons) {
				auto nesButton = toNesButton(press.button);
				if (controller && nesButton) {
					controller->SetBitValue(*nesButton, press.down);
				}
			}

			s.emulator->ProcessEvent(EventType::InputPolled, CpuType::Nes);

			constexpr double kNesCpuHz = 1789773.0;
			const double cyclesPerSample = kNesCpuHz / settings.sampleRate;
			const uint64_t cyclesAtBlockStart = console->GetCpu()->GetCycleCount();

			auto serial = io.input.serial;

			#ifndef FW_PLATFORM_WEB
			EverdriveComponent* everdrive = registry.try_get<EverdriveComponent>(e);
			if (everdrive && everdrive->edioProxy) {
				for (size_t i = 0; i < serial.count(); i++) {
					const TimedByte& b = serial.at(i);
					everdrive->edioProxy->sendCommand(EdioSerialCommand{ b.byte, 1 });
				}
			}
			#endif

			auto cpu = console->GetCpu();
			const uint32_t blockSize = settings.blockSize;

			// Step one CPU instruction at a time.
			//
			// cpu->Exec() internally calls StartCpuCycle() for every clock cycle
			// of the instruction, which:
			//   - advances the PPU (keeping mapper IRQ timing correct)
			//   - calls NesApu::Exec() each cycle
			//
			// The APU auto-flushes into MesenAudioDevice every CycleLength
			// (10,000) APU cycles via NesApu::EndFrame() -> PlayAudioBuffer().
			// At ~1.79 MHz CPU / 44100 Hz that yields ~227 samples per flush,
			// so blockSize samples are ready well within one PPU frame.
			while (s.audioDevice->availableFrames() < blockSize) {
				const uint64_t currentCycle = console->GetCpu()->GetCycleCount();

				while (serial.count()) {
					const TimedByte& b = serial.front();
					const uint64_t targetCycle = cyclesAtBlockStart + static_cast<uint64_t>(static_cast<double>(b.audioFrameOffset) * cyclesPerSample);

					if (currentCycle >= targetCycle) {
						s.fifo->pushByte(b.byte);
						serial.pop();
					}
				}

				cpu->Exec();
			}

			// Drain exactly blockSize stereo frames into the output buffer as
			// normalised float32. Any excess samples remain buffered for the
			// next block, keeping the CPU/audio clocks in lock-step.
			s.audioDevice->drain(outSamples->data(), blockSize);

			// Write the latest decoded video frame to io.output.video if one
			// became available during the CPU execution above.
			if (s.videoDevice) {
				if (orb::ImagePtr frame = s.videoDevice->takeFrame()) {
					io.output.video = std::move(frame);
				}
			}
		}
	}
}
