#pragma once

#include <list>
#include <memory>
#include "platform/Types.h"
#include "util/DataBuffer.h"

using namespace rp;

class AudioBufferAccessor {
private:
	Float32BufferPtr _buffer;
	uint32 _frameCount = 0;
	uint32 _channelCount = 0;

	f32* _data = nullptr;

public:
	AudioBufferAccessor() = default;
	AudioBufferAccessor(Float32BufferPtr buffer, uint32 channelCount, uint32 position, uint32 frameCount) {
		_buffer = buffer;
		_data = _buffer->data() + position * channelCount;
		_frameCount = frameCount;
		_channelCount = channelCount;
	}

	uint32 getFrameCount() const {
		return _frameCount;
	}

	void clear() {
		memset(_data, 0, _frameCount * _channelCount * sizeof(f32));
	}

	f32& getSample(uint32 channel, uint32 idx) {
		return _data[_channelCount * idx + channel];
	}

	void setSample(uint32 channel, uint32 idx, f32 value) {
		_data[_channelCount * idx + channel] = value;
	}

	f32 readAt(uint32 channel, f32 position) {
		uint32 p1 = ((uint32)position) % _frameCount;
		uint32 p2 = (p1 + 1) % _frameCount;
		f32 frac = position - floor(position);
		
		return _data[_channelCount * p1 + channel] * (1.0f - frac) + _data[_channelCount * p2 + channel] * frac;
	}
};

enum class EnvelopeType {
	None,
	Hanning
};

struct EnvelopeGenerator {
	EnvelopeType type;
};

struct Grain {
	AudioBufferAccessor audio;
	EnvelopeType ampEnvelope = EnvelopeType::None;
	f32 playbackPosition = 0.0f;
	f32 playbackSpeed = 1.0f;
};
using GrainPtr = std::unique_ptr<Grain>;

#include <assert.h>

enum class InterpolationType {
	Linear,
	Bicubic
};

class GrainStream {
private:
	std::list<GrainPtr> _playing;

public:
	void addGrain(GrainPtr&& grain) {
		_playing.push_back(std::move(grain));
	}

	f32 processEnvelope(EnvelopeType type, f32 position) {
		switch (type) {
		case EnvelopeType::None: return 1.0f;
		case EnvelopeType::Hanning: return 0.5f * (1.0f - cos(2.0f * PI * position));
		}

		return 1.0f;
	}

	bool processGrain(Grain& grain, AudioBufferAccessor& target) {
		f32 grainFrameCount = (f32)grain.audio.getFrameCount();

		for (size_t i = 0; i < target.getFrameCount(); ++i) {
			if (grain.playbackPosition >= 0) {
				f32 grainFrac = grain.playbackPosition / grainFrameCount;
				f32 env = processEnvelope(grain.ampEnvelope, grainFrac);

				target.getSample(0, i) += grain.audio.readAt(0, grain.playbackPosition) * env;
				target.getSample(1, i) += grain.audio.readAt(1, grain.playbackPosition) * env;
			}	

			grain.playbackPosition += grain.playbackSpeed;

			if (grain.playbackPosition >= grainFrameCount) {
				return true;
			}
		}

		return false;
	}

	void process(AudioBufferAccessor& target) {
		target.clear();
		std::list<GrainPtr>::iterator it = _playing.begin();

		while (it != _playing.end()) {
			if (processGrain(*it->get(), target)) {
				it = _playing.erase(it);
			} else {
				it++;
			}
		}
	}
};
