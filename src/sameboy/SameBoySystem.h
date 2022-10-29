#pragma once

#include <gb_struct_def.h>

#include "core/System.h"
#include "core/ProxySystem.h"
#include "audio/AudioBuffer.h"

namespace rp {
	const size_t PIXEL_WIDTH = 160;
	const size_t PIXEL_HEIGHT = 144;
	const size_t PIXEL_COUNT = (PIXEL_WIDTH * PIXEL_HEIGHT);
	const size_t FRAME_BUFFER_SIZE = (PIXEL_COUNT * 4);
	const size_t AUDIO_SCRATCH_SIZE = 44100;

	enum class GameboyModel {
		Auto,
		DmgB,
		//SgbNtsc,
		//SgbPal,
		//Sgb2,
		CgbC,
		CgbE,
		Agb
	};

	struct OffsetButton {
		int offset;
		int duration;
		int button;
		bool down;
	};

	class SameBoySystem final : public System<SameBoySystem> {
	public:
		struct State {
			// TODO: This should use std::unique_ptr
			SystemIo* io = nullptr;

			GB_gameboy_t* gb = nullptr;
			char frameBuffer[FRAME_BUFFER_SIZE];
			size_t audioFrameCount = 0;

			GameboyModel model = GameboyModel::Auto;
			bool fastBoot = true;

			uint32 frameTicks = 0;
			std::queue<OffsetButton> buttonQueue;

			std::queue<TimedByte> serialQueue;

			//int processTicks = 0;

			bool linkEnabled = false;
			int linkTicksRemain = 0;
			std::vector<SameBoySystem::State*> linkTargets;
			bool bitToSend;

			uint8 currentLinkByte = 0;
			size_t currentBitCount = 0;
		};

	private:
		State _state;
		uint32 _sampleRate = 48000;
		std::string _romName;

	public:
		SameBoySystem(SystemId id);
		~SameBoySystem();

		MemoryAccessor getMemory(MemoryType type, AccessType access = AccessType::ReadWrite) override;

		bool load(LoadConfig&& loadConfig) override;

		void reset() override;

		std::string getRomName() override {
			return _romName;
		}

		void setSampleRate(uint32 sampleRate) override;

		bool saveState(fw::Uint8Buffer& target) override;

		void setGameLink(bool gameLink);

		bool getGameLink() { return _state.linkEnabled; }

		void addLinkTarget(SystemBase* system);

		void removeLinkTarget(SystemBase* system);

		bool processTick(size_t targetFrameCount);

		void acquireIo(SystemIo* io);

		SystemIo* releaseIo();

		State& getState() {
			return _state;
		}

		const State& getState() const {
			return _state;
		}

		uint32 getSampleRate() const {
			return _sampleRate;
		}

	private:
		void destroy();
	};
}
