#include "MesenAudioHooks.h"

#include "MesenComponents.h"
//#include "MesenUtil.h"
#include "core/AudioEffect.h"
#include "core/AudioSettingsContext.h"
#include "core/HierarchyUtil.h"

#include "Core/NES/NesConsole.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/APU/NesApu.h"
#include "Core/NES/NesSoundMixer.h"
#include "Core/NES/BaseNesPpu.h"

namespace rp {
	double		_sampleRate = 44100.0;
	double		_cyclesPerSample = CPU_CLOCK_RATE / 44100.0;

	MesenAudioHooks::MesenAudioHooks() : AudioSystemHook(entt::type_id<MesenComponent>().index()) {}

	void MesenAudioHooks::onSaveState(entt::registry& registry, entt::entity entity, orb::Uint8Buffer& target) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		//MesenUtil::saveState(*state.state, target);
	}

	MemoryAccessor MesenAudioHooks::onGetMemory(entt::registry& registry, entt::entity entity, MemoryType type, AccessType access) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		//return MesenUtil::getMemory(*state.state, type, access);
		return MemoryAccessor();
	}

	void MesenAudioHooks::onPatchMemory(entt::registry& registry, entt::entity entity, const MemoryPatch& patch) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		/*MemoryAccessor accessor = MesenUtil::getMemory(*state.state, patch.type, AccessType::Write);

		std::visit(entt::overloaded{
			[&](uint8 val) { accessor.set(patch.offset, val); },
			[&](uint16 val) { accessor.write(patch.offset, val); },
			[&](uint32 val) { accessor.write(patch.offset, val); },
			[&](const orb::Uint8Buffer& val) { accessor.write(patch.offset, val); },
		}, patch.data);*/
	}

	void MesenAudioHooks::onReset(entt::registry& registry, entt::entity entity) const {
		MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
		state.emulator->Reset();
		state.blockStartCycle = 0;
		//MesenUtil::reset(*state.state);
	}

	int stepCpu(NesCpu* cpu, BaseNesPpu* ppu) {
		uint64_t before = cpu->GetCycleCount();

		// Execute one instruction
		// ⚠ May be named Step(), RunOnce(), or Exec() — verify
		cpu->Exec();

		uint64_t after = cpu->GetCycleCount();
		int		 delta = (int)(after - before);

		// Advance PPU to stay in sync.
		// The PPU must tick even though we never render — mapper IRQs
		// (MMC3 scanline counter, etc.) depend on PPU cycle timing.
		// ⚠ Verify RunTo() / Run() signature in NesPpu.h
		ppu->Run(after * PPU_DIVIDER);

		return delta;
	}

	void MesenAudioHooks::onProcess(entt::registry& registry, orb::AudioBuffer& out, const orb::AudioBuffer& in) const {
		// These should be hooks in the same vein as the system hooks, but this is easier for now
		onCreate<MesenStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
			//MesenState& state = *registry.get<MesenStateComponent>(entity).state;
			//MesenUtil::setSampleRate(state, (uint32)settings.sampleRate);
		});

		onDestroy<MesenStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			MesenStateComponent& state = registry.get<MesenStateComponent>(entity);
			//state.state = nullptr;
			//HierarchyUtil::destroyHierarchy(registry, entity, false);
			registry.remove<MesenStateComponent>(entity);
		});

		const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();

		uint32 i = 0;
		for (const auto& [e, s] : registry.view<MesenStateComponent>().each()) {
			// Interleaved audio buffer for mixed output (stereo)
			orb::Float32BufferPtr outSamples = registry.get<SystemIoComponent>(e).io->output.audio;

			auto console = dynamic_cast<NesConsole*>(s.emulator->GetConsole().get());
			if (!console) {
				continue;
			}

			auto cpu = console->GetCpu();
			auto ppu = console->GetPpu();
			auto apu = console->GetApu();
			auto soundMixer = console->GetSoundMixer();

			int32_t	samplesOut = 0;

			while (samplesOut < (int32_t)settings.blockSize) {
				uint64_t currentCycle = cpu->GetCycleCount();

				// ---- Step one CPU instruction ----
				stepCpu(cpu, ppu);

				// ---- Check how many samples the APU has accumulated ----
				//
				// The APU writes into Blip_Buffer as it runs alongside the CPU.
				// We check after each instruction rather than after each cycle
				// because the APU typically emits samples at ~44100Hz while the
				// CPU runs at ~1.79MHz — samples are sparse relative to instructions.
				//
				// ⚠ Verify the sample count / read API in NesApu.h or SoundMixer.h.
				//   It may be _soundMixer->GetBufferedSampleCount() instead.
				//samplesOut = (int32_t)apu->GetBufferedSampleCount();
			}

			// ---- Read mixed output ----
			// ⚠ Verify signature — may take a frame count or use a callback
			//_soundMixer->ReadSamples(channels.mixed, needed);
		}

		
		//MesenUtil::process(comps.data(), view.size(), settings.blockSize);
	}
}
