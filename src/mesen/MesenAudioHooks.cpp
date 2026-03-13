#include "MesenAudioHooks.h"

#include "MesenComponents.h"
#include "MesenAudioDevice.h"
#include "core/AudioEffect.h"
#include "core/AudioSettingsContext.h"
#include "core/HierarchyUtil.h"

#include "Core/NES/NesConsole.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/APU/NesApu.h"
#include "Core/NES/NesSoundMixer.h"
#include "Core/NES/BaseNesPpu.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/SettingTypes.h"

namespace rp {
	double		_sampleRate = 44100.0;
	double		_cyclesPerSample = CPU_CLOCK_RATE / 44100.0;

	MesenAudioHooks::MesenAudioHooks() : AudioSystemHook(entt::type_id<MesenComponent>().index()) {}

	void MesenAudioHooks::onSaveState(entt::registry& registry, entt::entity entity, orb::Uint8Buffer& target) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
	}

	MemoryAccessor MesenAudioHooks::onGetMemory(entt::registry& registry, entt::entity entity, MemoryType type, AccessType access) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		return MemoryAccessor();
	}

	void MesenAudioHooks::onPatchMemory(entt::registry& registry, entt::entity entity, const MemoryPatch& patch) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
	}

	void MesenAudioHooks::onReset(entt::registry& registry, entt::entity entity) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		state.emulator->Reset();
		state.blockStartCycle = 0;
	}

	void MesenAudioHooks::onProcess(entt::registry& registry, orb::AudioBuffer& out, const orb::AudioBuffer& in) const {
		onCreate<MesenStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
			MesenStateComponent& s = registry.get<MesenStateComponent>(entity);

			// Create and register our capture device with the emulator's SoundMixer.
			s.audioDevice = std::make_shared<MesenAudioDevice>();
			s.emulator->GetSoundMixer()->RegisterAudioDevice(s.audioDevice.get());

			// Tell Mesen to render at the plugin's sample rate so we receive
			// samples at exactly the rate the host expects.
			AudioConfig audioCfg = s.emulator->GetSettings()->GetAudioConfig();
			audioCfg.SampleRate = (uint32_t)settings.sampleRate;
			s.emulator->GetSettings()->SetAudioConfig(audioCfg);
		});

		onDestroy<MesenStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
			registry.remove<MesenStateComponent>(entity);
		});

		const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();

		for (const auto& [e, s] : registry.view<MesenStateComponent>().each()) {
			if (!s.audioDevice) {
				continue;
			}

			orb::Float32BufferPtr outSamples = registry.get<SystemIoComponent>(e).io->output.audio;
			if (!outSamples) {
				continue;
			}

			//s.emulator->Run();

			auto console = dynamic_cast<NesConsole*>(s.emulator->GetConsole().get());
			if (!console) {
				continue;
			}

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
				cpu->Exec();
			}

			// Drain exactly blockSize stereo frames into the output buffer as
			// normalised float32. Any excess samples remain buffered for the
			// next block, keeping the CPU/audio clocks in lock-step.
			s.audioDevice->drain(outSamples->data(), blockSize);
		}
	}
}
