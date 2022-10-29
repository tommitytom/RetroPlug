#pragma once

#include <memory>
#include <queue>
#include <variant>
#include <vector>

#include <entt/core/type_info.hpp>
#include <moodycamel/readerwriterqueue.h>
#include <moodycamel/concurrentqueue.h>

#include "foundation/DataBuffer.h"
#include "foundation/Image.h"
#include "foundation/Types.h"

#include "core/ButtonStream.h"
#include "core/FixedQueue.h"
#include "core/Forward.h"
#include "core/MemoryAccessor.h"

namespace rp {
	using SystemType = entt::id_type;
	const size_t MAX_SYSTEM_COUNT = 4;

	struct SystemStateOffsets {
		int32 coreState = -1;
		int32 dma = -1;
		int32 mbc = -1;
		int32 hram = -1;
		int32 timing = -1;
		int32 apu = -1;
		int32 rtc = -1;
		int32 video = -1;
	};

	struct TimedByte {
		uint32 audioFrameOffset;
		uint8 byte;
	};

	struct TimedButtonPress {
		uint32 button;
		bool down;
		uint32 audioFrameOffset = 0;
	};

	struct LoadConfig {
		fw::Uint8BufferPtr romBuffer;
		fw::Uint8BufferPtr sramBuffer;
		fw::Uint8BufferPtr stateBuffer;
		bool reset = false;

		void merge(LoadConfig& other) {
			if (other.romBuffer) {
				romBuffer = std::move(other.romBuffer);
			}

			if (other.sramBuffer) {
				sramBuffer = std::move(other.sramBuffer);
			}

			if (other.stateBuffer) {
				stateBuffer = std::move(other.stateBuffer);
			}

			if (other.reset) {
				other.reset = reset;
			}
		}

		bool hasChanges() const {
			return romBuffer || sramBuffer || stateBuffer || reset;
		}
	};

	struct SystemIo {
		SystemId systemId;

		struct Input {
			FixedQueue<TimedByte, 16> serial;
			std::vector<ButtonStream<8>> buttons;
			std::vector<MemoryPatch> patches;
			bool requestReset = false;
			LoadConfig loadConfig;

			void reset() {
				serial.reset();
				buttons.clear();
				patches.clear();
				requestReset = false;
				loadConfig = LoadConfig();
			}
		} input;

		struct Output {
			std::vector<TimedByte> serial;
			fw::ImagePtr video;
			fw::Float32BufferPtr audio;
			
			fw::Uint8BufferPtr state;

			void reset() {
				serial.clear();
				video = nullptr;
				audio = nullptr;
				state = nullptr;
			}
		} output;

		void merge(SystemIo& other) { 
			input.requestReset |= other.input.requestReset;

			if (other.input.loadConfig.romBuffer) {
				input.loadConfig.romBuffer = std::move(other.input.loadConfig.romBuffer);
			}
			
			while (other.input.serial.count()) {
				input.serial.tryPush(other.input.serial.pop());
			}

			for (ButtonStream<8>& buttonStream : other.input.buttons) {
				input.buttons.push_back(buttonStream);
			}

			for (MemoryPatch& patch : other.input.patches) {
				input.patches.push_back(std::move(patch));
			}

			other.input.buttons.clear();
			other.input.patches.clear();

			if (other.output.video) {
				output.video = std::move(other.output.video);
			}

			if (other.output.state) {
				output.state = std::move(other.output.state);
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

	using SystemIoPtr = std::unique_ptr<SystemIo>;

	struct IoMessageBus {
		moodycamel::ReaderWriterQueue<SystemIoPtr> uiToAudio;
		moodycamel::ReaderWriterQueue<SystemIoPtr> audioToUi;
		moodycamel::ConcurrentQueue<SystemIoPtr> allocator;

		SystemIoPtr alloc(SystemId systemId) {
			SystemIoPtr io;
			if (allocator.try_dequeue(io)) {
				io->systemId = systemId;
				return io;
			}

			return nullptr;
		}

		void dealloc(SystemIoPtr&& io) {
			io->reset();
			allocator.enqueue(std::move(io));
		}
	};

	const int32 SRAM_COPY_INTERVAL = 2048;

	enum class AccessType {
		Unknown,
		Read,
		Write,
		ReadWrite
	};

	class SystemBase {
	protected:
		SystemIoPtr _stream;
		fw::DimensionT<uint32> _resolution;

	private:
		SystemId _id;
		SystemType _type;
		int32 _nextStateCopy = 0;
		int32 _stateCopyInterval = 0;

		std::array<bool, ButtonType::MAX> _buttonState = { false };

	public:
		SystemBase(SystemId id, SystemType type): _id(id), _type(type) {}

		const std::array<bool, ButtonType::MAX>& getButtonState() const {
			return _buttonState;
		}

		virtual MemoryAccessor getMemory(MemoryType type, AccessType access) { return MemoryAccessor(); }

		virtual bool load(LoadConfig&& loadConfig) { return false; }

		virtual void reset() {}

		virtual void process(uint32 frameCount) {}

		virtual void setSampleRate(uint32 sampleRate) {}

		virtual bool saveState(fw::Uint8Buffer& target) { return false; }

		virtual bool saveSram(fw::Uint8Buffer& target) { return false; }

		virtual std::string getRomName() { return std::string(); }

		virtual bool isProxy() const { return false; }

		virtual void setGameLink(bool gameLink) {}

		virtual bool getGameLink() { return false; }
		
		virtual void addLinkTarget(SystemBase* system) {}

		virtual void removeLinkTarget(SystemBase* system) {}

		void setStateCopyInterval(int32 interval) {
			_stateCopyInterval = interval;
		}

		void processStateCopy(uint32 frameCount) {
			if (_stateCopyInterval > 0) {
				_nextStateCopy -= frameCount;

				if (_stream && _nextStateCopy <= 0) {
					_stream->output.state = std::make_shared<fw::Uint8Buffer>();
					saveState(*_stream->output.state);

					_nextStateCopy = _stateCopyInterval;
				}
			}
		}

		void setButtonState(ButtonType::Enum button, bool down) {
			_buttonState[button] = down;

			if (_stream) {
				_stream->input.buttons.push_back(ButtonStream<8> {
					.presses = { (int)button, down },
					.pressCount = 1
				});
			}
		}

		fw::DimensionT<uint32> getResolution() const {
			return _resolution;
		}

		void setStream(SystemIoPtr&& stream) {
			_stream = std::move(stream);
		}

		SystemIoPtr& getStream() {
			return _stream;
		}

		SystemId getId() const {
			return _id;
		}

		SystemType getBaseType() const {
			return _type;
		}

		virtual SystemType getType() const {
			return getBaseType();
		}

		friend class SystemOrchestrator;
	};

	template <typename T>
	class System : public SystemBase {
	public:
		System(SystemId id): SystemBase(id, entt::type_id<T>().index()) {}
	};

	using SystemPtr = std::shared_ptr<SystemBase>;

	struct OrchestratorChange {
		SystemPtr add;
		SystemPtr swap;
		SystemPtr replace;
		SystemId remove = INVALID_SYSTEM_ID;
		SystemId reset = INVALID_SYSTEM_ID;
		SystemId gameLink = INVALID_SYSTEM_ID;
	};

	struct OrchestratorMessageBus {
		moodycamel::ReaderWriterQueue<OrchestratorChange> uiToAudio;
		moodycamel::ReaderWriterQueue<OrchestratorChange> audioToUi;
	};
}
