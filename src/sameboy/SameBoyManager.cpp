#include "SameBoyManager.h"

#include <memory>
#include <iostream>
#include <entt/core/utility.hpp>

extern "C" {
#include <gb.h>
}

#include "util/GameboyUtil.h"

using namespace rp;

//const size_t CHANNEL_COUNT = 2;

const size_t LINK_TICKS_MAX = 3907;

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
			std::visit(entt::overloaded{
				[&](uint8 val) { target[patch.offset] = val; },
				[&](uint16 val) { memcpy(target + patch.offset, &val, sizeof(uint16)); },
				[&](uint32 val) { memcpy(target + patch.offset, &val, sizeof(uint32)); },
				[&](const fw::Uint8Buffer& val) { memcpy(target + patch.offset, val.data(), val.size()); },
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

void processSerial(FixedQueue<TimedByte, 16>& source, std::queue<TimedByte>& target, f32 timeScale) {
	while (source.count()) {
		target.push(source.pop());
	}
}

std::string SameBoyManager::getRomName(const fw::Uint8Buffer& romData) {
	return GameboyUtil::getRomName((const char*)romData.data());
}

void SameBoyManager::process(uint32 frameCount) {
	std::vector<SystemPtr>& systems = getSystems();

	for (size_t i = 0; i < systems.size(); ++i) {
		std::shared_ptr<SameBoySystem> system = std::static_pointer_cast<SameBoySystem>(systems[i]);

		//FixedQueue<TimedButtonPress, 16>* buttons = nullptr;
		SameBoySystem::State* state = &system->getState();
		state->io = system->getStream().get();

		int delta = 0;
		GB_gameboy_t* gb = state->gb;

		if (state->io) {
			SystemIo::Input& input = state->io->input;
			f32 timeScale = (f32)system->getSampleRate() / 1000.0f;

			processSerial(input.serial, state->serialQueue, timeScale);
			processButtons(input.buttons, state->buttonQueue, timeScale);
			processPatches(gb, input.patches);

			if (input.loadConfig.hasChanges()) {
				system->load(std::move(input.loadConfig));
			}

			if (input.requestReset) {
				system->reset();
			}
		}

		while (state->audioFrameCount < frameCount) {
			if (state->linkTicksRemain <= 0) {
				auto serial = state->serialQueue;

				if (!serial.empty()) {
					TimedByte b = serial.front();
					serial.pop();

					for (int i = 8 - 1; i >= 0; i--) {
						bool bit = (bool)((b.byte & (1 << i)) >> i);
						GB_serial_set_data_bit(state->gb, bit);
					}
				}

				state->linkTicksRemain += LINK_TICKS_MAX;
			}

			// Send button presses if required

			while (state->buttonQueue.size() && state->buttonQueue.front().offset <= state->audioFrameCount) {
				OffsetButton b = state->buttonQueue.front();
				state->buttonQueue.pop();

				GB_set_key_state(gb, (GB_key_t)b.button, b.down);
			}

			int ticks = GB_run(gb);
			delta += ticks;
			state->linkTicksRemain -= ticks;
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
		size_t serialRemain = state->serialQueue.size();
		for (size_t i = 0; i < serialRemain; i++) {
			TimedByte b = state->serialQueue.front();
			state->serialQueue.pop();

			b.audioFrameOffset = 0;
			state->serialQueue.push(b);
		}

		state->io = nullptr;
		state->audioFrameCount = 0;
	}
}
