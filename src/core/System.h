#pragma once

#include <memory>
#include <queue>
#include <variant>
#include <vector>

#include <entt/core/type_info.hpp>
#include <entt/core/memory.hpp>
#include <magic_enum.hpp>
#include <moodycamel/readerwriterqueue.h>
#include <moodycamel/concurrentqueue.h>
#include <spdlog/spdlog.h>

#include "foundation/DataBuffer.h"
#include "foundation/Image.h"
#include "foundation/Types.h"

#include "core/ButtonStream.h"
#include "core/FixedQueue.h"
#include "core/Forward.h"
#include "core/MemoryAccessor.h"
#include "core/ProjectState.h"

namespace rp {
	struct SystemStateOffset {
		uint32 offset = 0;
		uint32 size = 0;
	};

	using SystemStateOffsets = std::array<SystemStateOffset, magic_enum::enum_count<MemoryType>()>;

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
		SystemDesc desc;

		fw::Uint8BufferPtr romBuffer;
		fw::Uint8BufferPtr sramBuffer;
		fw::Uint8BufferPtr stateBuffer;
		SaveStateType stateType = SaveStateType::None;
		bool reset = false;
	};

	struct SystemIo {
		SystemId systemId;

		struct Input {
			FixedQueue<TimedByte, 16> serial;
			std::vector<ButtonStream<8>> buttons;
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

	template <typename T>
	class ConcurrentPoolAllocator {
	private:
		moodycamel::ConcurrentQueue<std::unique_ptr<T>> _pool;

	public:
		ConcurrentPoolAllocator(size_t poolSize = 32) : _pool(poolSize) {
			for (size_t i = 0; i < poolSize; ++i) {
				_pool.enqueue(std::make_unique<T>());
			}
		}

		~ConcurrentPoolAllocator() {}

		std::shared_ptr<T> allocate() {
			std::shared_ptr<T> item = tryAllocate();
			if (item) {
				return item;
			}

			return std::make_shared<T>();
		}

		std::shared_ptr<T> tryAllocate() {
			std::unique_ptr<T> item;
			if (_pool.try_dequeue(item)) {
				return std::shared_ptr<T>(item.release(), [&](T* ptr) {
					if (!_pool.enqueue(std::unique_ptr<T>(ptr))) {
						spdlog::error("Failed to return item to pool");
					}
				});
			}

			return nullptr;
		}
	};

	using SystemIoPtr = std::shared_ptr<SystemIo>;

	struct IoMessageBus {
		ConcurrentPoolAllocator<SystemIo> allocator;

		IoMessageBus(): allocator(MAX_IO_STREAMS) {}

		SystemIoPtr alloc(SystemId systemId) {
			SystemIoPtr io = allocator.allocate();
			if (io) {
				io->reset();
				io->systemId = systemId;
			} else {
				spdlog::error("IO alloc failed");
			}

			return io;
		}

		/*void dealloc(SystemIoPtr&& io) {
			io->reset();
			io = nullptr;
			//allocator.enqueue(std::move(io));
		}*/
	};

	const int32 SRAM_COPY_INTERVAL = 2048;

	enum class AccessType {
		Unknown,
		Read,
		Write,
		ReadWrite
	};

	class System {
	protected:
		SystemIoPtr _stream;
		fw::DimensionU32 _resolution;
		fw::Image _frameBuffer;

	private:
		SystemId _id = INVALID_SYSTEM_ID;
		SystemType _type = INVALID_SYSTEM_TYPE;
		SystemDesc _desc;

		std::array<bool, ButtonType::MAX> _buttonState = { false };
		std::vector<ModelPtr> _models;
		std::vector<SystemServicePtr> _services;

	public:
		System(SystemType type): _type(type) {}
		~System() = default;

		virtual MemoryAccessor getMemory(MemoryType type, AccessType access) { return MemoryAccessor(); }

		void addService(SystemServicePtr service) {
			_services.push_back(service);
		}

		std::vector<SystemServicePtr>& getServices() {
			return _services;
		}

		const std::vector<SystemServicePtr>& getServices() const {
			return _services;
		}

		virtual bool load(LoadConfig&& loadConfig) { return false; }

		virtual void reset() {}

		virtual void process(uint32 frameCount) {}

		virtual void setSampleRate(uint32 sampleRate) {}

		virtual void setStateBuffer(fw::Uint8Buffer&& buffer) {}

		virtual bool saveState(fw::Uint8Buffer& target) { return false; }

		virtual bool saveSram(fw::Uint8Buffer& target) { return false; }

		virtual std::string getRomName() { return std::string(); }

		virtual bool isProxy() const { return false; }

		virtual void setGameLink(bool gameLink) {}

		virtual bool getGameLink() { return false; }

		virtual void addLinkTarget(System* system) {}

		virtual void removeLinkTarget(System* system) {}

		virtual SystemStateOffsets getStateOffsets() const { return SystemStateOffsets(); }

		void setButtonState(ButtonType::Enum button, bool down) {
			_buttonState[button] = down;

			if (_stream) {
				_stream->input.buttons.push_back(ButtonStream<8> {
					.presses = StreamButtonPress{ .button = (int)button, .down = down, .duration = 0 },
					.pressCount = 1
				});
			}
		}

		fw::DimensionU32 getResolution() const {
			return _resolution;
		}

		void setIo(const SystemIoPtr& io) {
			_stream = std::move(io);
		}

		void acquireIo(SystemIoPtr&& io) {
			if (_stream) {
				_stream->merge(*io);
			} else {
				_stream = std::move(io);
			}
		}

		SystemIoPtr releaseIo() {
			return std::move(_stream);
		}

		bool hasIo() const {
			return _stream != nullptr;
		}

		SystemIoPtr getIo() {
			return _stream;
		}

		void setId(SystemId systemId) {
			_id = systemId;
		}

		SystemId getId() const {
			return _id;
		}

		virtual SystemType getTargetType() const {
			return getType();
		}

		SystemType getType() const {
			return _type;
		}

		const SystemDesc& getDesc() const {
			return _desc;
		}

		void setDesc(SystemDesc&& desc) {
			_desc = std::move(desc);
		}

		void setDesc(const SystemDesc& desc) {
			_desc = desc;
		}

		const std::array<bool, ButtonType::MAX>& getButtonState() const {
			return _buttonState;
		}

		const fw::Image& getFrameBuffer() const {
			return _frameBuffer;
		}
	};

	using SystemPtr = std::shared_ptr<System>;
}
