#pragma once

#include <spdlog/spdlog.h>

#include "core/AudioState.h"
#include "core/Proxies.h"
#include "util/CircularBuffer.h"
#include "util/DataBuffer.h"
#include "SystemManager.h"

namespace rp {
	class AudioStreamSystem final : public System<AudioStreamSystem> {
	private:
		SystemType _targetType;

		uint64 _lastFrameOffset = 0;
		uint64 _processedFrameCount = 0;
		uint64 _delay = 4800;

		CircularBuffer<f32> _circularBuffer;
		bool _isPlaying = false;

	public:
		AudioStreamSystem(SystemId id): System<AudioStreamSystem>(id), _circularBuffer(48000 * 4) {
			_circularBuffer.setWritePosition(_delay);
		}

		void setDesc(SystemType targetType) {
			_targetType = targetType;
		}

		void process(uint32 frameCount) override {
			SystemIo* io = getStream().get();

			if (io) {
				for (AudioChunk& chunk : io->input.audioChunks) {
					if (chunk.offset < _lastFrameOffset) {
						spdlog::warn("Audio stream underrun by {} frames", _lastFrameOffset - chunk.offset);
					} else if (chunk.offset > _lastFrameOffset) {
						spdlog::warn("Audio stream overrun by {} frames", chunk.offset - _lastFrameOffset);
					}

					_lastFrameOffset = chunk.offset + chunk.buffer->size() / 2;
					_circularBuffer.write(*chunk.buffer);
				}

				io->output.audio = std::make_shared<Float32Buffer>(frameCount * 2);
				io->output.audio->clear();

				_circularBuffer.read(*io->output.audio);
			}

			_processedFrameCount += frameCount;
		}
	};

	using AudioPlayerSystemPtr = std::shared_ptr<AudioStreamSystem>;
}
