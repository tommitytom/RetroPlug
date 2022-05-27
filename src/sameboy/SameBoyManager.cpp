#include "SameBoyManager.h"

#include <memory>
#include <iostream>

extern "C" {
#include <gb.h>
}

#include "core/ThreadPool.h"
#include "util/GameboyUtil.h"

using namespace rp;

//const size_t CHANNEL_COUNT = 2;

FixedQueue<TimedButtonPress, 16> EMPTY_BUTTON_QUEUE;

void processPatches(GB_gameboy_t* gb, std::vector<MemoryPatch>& patches) {
	for (MemoryPatch& patch : patches) {
		size_t memSize;
		uint16 memBank;
		uint8* target = nullptr;

		switch (patch.type) {
		case MemoryType::Rom: target = (uint8*)GB_get_direct_access(gb, GB_DIRECT_ACCESS_ROM, &memSize, &memBank); break;
		case MemoryType::Ram: target = (uint8*)GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, &memSize, &memBank); break;
		case MemoryType::Sram: target = (uint8*)GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &memSize, &memBank); break;
		case MemoryType::Unknown: break;
		}

		if (target) {
			std::visit(overload{
				[&](uint8 val) { target[patch.offset] = val; },
				[&](uint16 val) { memcpy(target + patch.offset, &val, sizeof(uint16)); },
				[&](uint32 val) { memcpy(target + patch.offset, &val, sizeof(uint32)); },
				[&](const Uint8Buffer& val) { memcpy(target + patch.offset, val.data(), val.size()); },
			}, patch.data);
		}
	}
}

void processButtons(const std::vector<ButtonStream<8>>& source, std::queue<OffsetButton>& target, f32 timeScale) {
	for (const ButtonStream<8>& stream : source) {	
		for (size_t i = 0; i < stream.pressCount; ++i) {
			int offset = 0;
			if (target.size() > 0) {
				offset = target.back().offset + target.back().duration;
			}

			target.push(OffsetButton{
				.offset = offset,
				.duration = (int)(timeScale * stream.presses[i].duration),
				.button = stream.presses[i].button,
				.down = stream.presses[i].down
			});
		}
	}
}

SameBoyManager::SameBoyManager() { 
	_threadPool = std::make_unique<ThreadPool>();
	_threadPool->start(3);
}

SameBoyManager::~SameBoyManager() {}

std::string SameBoyManager::getRomName(const Uint8Buffer& romData) {
	return GameboyUtil::getRomName((const char*)romData.data());
}

void processSystem(SameBoySystem& system, uint32 frameCount) {
	//FixedQueue<TimedButtonPress, 16>* buttons = nullptr;
	SameBoySystem::State* state = &system.getState();
	state->io = system.getStream().get();

	int delta = 0;
	GB_gameboy_t* gb = state->gb;

	if (state->io) {
		SystemIo::Input& input = state->io->input;

		processButtons(input.buttons, state->buttonQueue, 48000 / 1000.0);
		processPatches(gb, input.patches);

		if (input.loadConfig.hasChanges()) {
			system.load(std::move(input.loadConfig));
		}

		if (input.requestReset) {
			system.reset();
		}
	}

	while (state->audioFrameCount < frameCount) {
		/*if (_state.linkTicksRemain <= 0) {
			if (!_state.serialQueue.empty()) {
				OffsetByte b = _state.serialQueue.front();
				_state.serialQueue.pop();

				for (int i = b.bitCount - 1; i >= 0; i--) {
					bool bit = (bool)((b.byte & (1 << i)) >> i);
					GB_serial_set_data_bit(_state.gb, bit);
				}
			}

			_state.linkTicksRemain += LINK_TICKS_MAX;
		}*/

		// Send button presses if required

		while (state->buttonQueue.size() && state->buttonQueue.front().offset <= state->audioFrameCount) {
			OffsetButton b = state->buttonQueue.front();
			state->buttonQueue.pop();

			GB_set_key_state(gb, (GB_key_t)b.button, b.down);
		}

		int ticks = GB_run(gb);
		delta += ticks;
		//_state.linkTicksRemain -= ticks;
	}

	size_t buttonRemain = state->buttonQueue.size();
	for (size_t i = 0; i < buttonRemain; i++) {
		OffsetButton button = state->buttonQueue.front();
		button.offset -= state->audioFrameCount;
		state->buttonQueue.push(button);
		state->buttonQueue.pop();
	}

	// If there are any serial/midi events that still haven't been processed, set their
	// offsets to 0 so they get processed immediately at the start of the next frame.
	/*size_t serialRemain = _state.serialQueue.size();
	for (size_t i = 0; i < serialRemain; i++) {
		OffsetByte b = _state.serialQueue.front();
		b.offset = 0;
		_state.serialQueue.push(b);
		_state.serialQueue.pop();
	}*/

	state->io = nullptr;
	state->audioFrameCount = 0;
}

void SameBoyManager::process(uint32 frameCount) {
	std::vector<SystemPtr>& systems = getSystems();

	if (systems.size()) {
		if (!_threadPool) {
			for (size_t i = 0; i < systems.size(); ++i) {
				std::shared_ptr<SameBoySystem> system = std::static_pointer_cast<SameBoySystem>(systems[i]);
				processSystem(*system, i);
			}
		} else {
			for (size_t i = 0; i < systems.size() - 1; ++i) {
				std::shared_ptr<SameBoySystem> system = std::static_pointer_cast<SameBoySystem>(systems[i]);
				_threadPool->enqueue([=]() { processSystem(*system, i); });
			}

			std::shared_ptr<SameBoySystem> system = std::static_pointer_cast<SameBoySystem>(systems.back());
			processSystem(*system, frameCount);

			_threadPool->wait(std::chrono::milliseconds::zero(), std::chrono::milliseconds::zero());
		}
	}
}
