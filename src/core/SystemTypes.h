#pragma once

#include <vector>
#include "core/Forward.h"
#include "core/FixedQueue.h"
#include "core/MemoryAccessor.h"
#include "foundation/ButtonStream.h"
#include "foundation/Image.h"

namespace rp {
	struct TimedByte {
		uint8 byte = 0;
		uint32 audioFrameOffset = 0;
	};

	struct TimedButtonPress {
		uint32 button = 0;
		bool down = false;
		uint32 audioFrameOffset = 0;
	};

	struct SystemIo {
		SystemId systemId = -1;

		struct Input {
			FixedQueue<TimedByte, 16> serial;
			std::vector<fw::StreamButtonPress> buttons;
			std::vector<MemoryPatch> patches;

			void reset() {
				serial.reset();
				buttons.clear();
				patches.clear();
			}
		} input;

		struct Output {
			std::vector<TimedByte> serial;
			fw::ImagePtr video;
			fw::Float32BufferPtr audio;

			void reset() {
				serial.clear();
				video = nullptr;
				audio = nullptr;
			}
		} output;

		void merge(SystemIo& other) {
			while (other.input.serial.count()) {
				input.serial.tryPush(other.input.serial.pop());
			}

			for (const fw::StreamButtonPress& press : other.input.buttons) {
				input.buttons.push_back(press);
			}

			for (MemoryPatch& patch : other.input.patches) {
				input.patches.push_back(std::move(patch));
			}

			other.input.buttons.clear();
			other.input.patches.clear();

			if (other.output.video) {
				output.video = std::move(other.output.video);
			}

			if (other.output.serial.size()) {
				for (const TimedByte& b : other.output.serial) {
					output.serial.push_back(b);
				}

				// TODO: Sort?
			}
		}

		void reset() {
			input.reset();
			output.reset();
		}
	};

	using SystemIoPtr = std::shared_ptr<SystemIo>;
}
