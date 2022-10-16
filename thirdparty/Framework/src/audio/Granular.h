#pragma once

#include <spdlog/spdlog.h>
#include <list>
#include "audio/AudioBuffer.h"
#include "foundation/InterpolationUtil.h"
#include <iostream>

namespace fw {
	namespace Envelopes {
		using Func = AudioSampleT(*)(AudioSampleT);

		AudioSampleT none(AudioSampleT ratio) { return 1.0f; }

		AudioSampleT hann(AudioSampleT ratio) { return 0.5f * (1.0f - cosf(PI2 * ratio)); }

		// https://sigpack.sourceforge.net/group__window.html#gac45c43222276f80ff62befffbc1c4e20
		AudioSampleT hamming(AudioSampleT ratio) { return 0.5f * (1.0f - cosf(PI2 * ratio)); }
	}

	void generateWindow(Envelopes::Func func, StereoAudioBuffer& target) {
		f32 w = (f32)(target.getFrameCount() - 1);

		for (uint32 i = 0; i < target.getFrameCount(); ++i) {
			f32 v = func((f32)i / w);
			target[i][0] = v;
			target[i][1] = v;
		}
	}

	StereoAudioBuffer generateWindow(Envelopes::Func func, uint32 grainSize) {
		StereoAudioBuffer target(grainSize);
		generateWindow(func, target);
		return target;
	}

	StereoAudioBuffer generateLinearEnvelope(uint32 attack, uint32 sustain, uint32 release) {
		uint32 size = attack + sustain + release;
		StereoAudioBuffer target(size);

		for (uint32 i = 0; i < attack; ++i) {
			f32 v = (f32)i / (f32)attack;
			target[i][0] = v;
			target[i][1] = v;
		}

		for (uint32 i = attack; i < attack + sustain; ++i) {
			target[i][0] = 1.0f;
			target[i][1] = 1.0f;
		}

		for (uint32 i = 0; i < release; ++i) {
			f32 v = 1.0f - (f32)i / (f32)release;
			target[attack + sustain + i][0] = v;
			target[attack + sustain + i][1] = v;
		}

		return target;
	}

	struct Grain {
		StereoAudioBuffer buffer;
		StereoAudioBuffer window;

		f32 position = 0.0f;
		f32 speed = 1.0f;
		int32 delay = 0;

		int32 processedFrames = 0;
		bool requiresInterpolation = false;
	};

	class GrainStream {
	private:
		std::list<Grain> _grains;

	public:
		void add(Grain&& grain) {
			assert(grain.buffer.getFrameCount() > 0);
			assert(grain.window.getFrameCount() > 0);

			grain.requiresInterpolation = grain.speed != 1.0f || grain.position != 0.0f;

			_grains.push_back(std::move(grain));
		}

		StereoAudioBuffer::Frame next() {
			StereoAudioBuffer::Frame ret;
			ret[0] = 0.0f;
			ret[1] = 0.0f;

			if (_grains.size()) {
				for (auto it = _grains.begin(); it != _grains.end();) {
					Grain& grain = *it;
					StereoAudioBuffer::Frame frame;

					if (grain.delay == 0) {
						if (!grain.requiresInterpolation) {
							frame = grain.buffer[grain.processedFrames];
						} else {
							frame = interpolateValue(grain.buffer, grain.position);
							grain.position += grain.speed;
						}

						ret += frame * grain.window[grain.processedFrames];

						if (++grain.processedFrames < (int32)grain.window.getFrameCount()) {
							++it;
						} else {
							it = _grains.erase(it);
						}
					} else {
						--grain.delay;
						++it;
					}
				}
			} else {
				spdlog::info("no grains!");
			}

			return ret;
		}

		void process(StereoAudioBuffer& target) {
			for (uint32 i = 0; i < target.getFrameCount(); ++i) {
				target[i] += next();
			}
		}

		const std::list<Grain>& getGrains() const {
			return _grains;
		}

	private:
		template <const uint32 ChannelCount>
		static InterleavedAudioBuffer<ChannelCount>::Frame interpolateValue(const InterleavedAudioBuffer<ChannelCount>& buffer, f32 offset) {
			return std::array<AudioSampleT, 2>{
				InterpolationUtil::interpolateValue(buffer.getSamples(), buffer.getFrameCount(), offset, ChannelCount),
				InterpolationUtil::interpolateValue(buffer.getSamples() + 1, buffer.getFrameCount(), offset, ChannelCount)
			};
		}
	};

	class GranularTimeStretch {
	private:
		GrainStream _grains;
		StereoAudioBuffer _input;

		uint32 _grainSize = 1200;
		f32 _overlap = 0;
		f32 _playbackPos = 0;

		f32 _stretch = 1.0f;
		f32 _speed = 1.0f;

		uint32 _nextGrain = 0;
		uint32 _hop = 1200;
		uint32 _overlapFrames = 0;
		uint32 _sustainFrames = 1200;

		bool _loop = true;
		bool _finishing = false;

		f32 _sampleRate = 0;

	public:
		GranularTimeStretch(f32 sampleRate = 48000.0f): _sampleRate(sampleRate) {}
		GranularTimeStretch(StereoAudioBuffer&& input, f32 sampleRate) : _input(std::move(input)), _sampleRate(sampleRate) {}
		~GranularTimeStretch() = default;

		void updateCoeffs() {
			_overlapFrames = (uint32)(_grainSize * _overlap);
			_sustainFrames = _grainSize - (_overlapFrames * 2);
			_hop = _grainSize - _overlapFrames;
		}

		void setOverlap(f32 overlap) {
			_overlap = MathUtil::clamp(overlap, 0.0f, 0.5f);
			updateCoeffs();
		}

		void setLoop(bool loop) {
			_loop = loop;
		}

		void setInput(StereoAudioBuffer&& input) {
			_input = std::move(input);
		}

		void setPitch(f32 pitch) {
			if (pitch > 0) {
				_speed = 1.0f + pitch;
			} else {
				_speed = 1.0f / (1.0f + fabs(pitch));
			}
		}

		void setGrainSize(f32 grainSize) {
			_grainSize = (uint32)(grainSize * (_sampleRate / 1000.0f));
			updateCoeffs();
		}

		void setStretch(f32 stretch) {
			assert(stretch >= 1.0f);
			_stretch = stretch;
		}

		StereoAudioBuffer::Frame next() {
			if (_nextGrain == 0 && !_finishing) {
				spawnGrain();
			} else {
				_nextGrain--;
			}

			return _grains.next();
		}

		void process(StereoAudioBuffer& target) {
			for (uint32 i = 0; i < target.getFrameCount(); ++i) {
				target[i] += next();
			}
		}

		bool hasFinished() const {
			return _finishing && _grains.getGrains().empty();
		}

	private:
		void spawnGrain() {
			uint32 grainSize = _grainSize;
			uint32 overlap = _overlapFrames;
			uint32 sustain = _sustainFrames;
			uint32 hop = _hop;

			if ((int32)_playbackPos + grainSize > _input.getFrameCount()) {
				grainSize = _input.getFrameCount() - (int32)_playbackPos;
				overlap = (uint32)(grainSize * _overlap);
				sustain = grainSize - (overlap * 2);
				hop = grainSize - overlap;
			}

			_grains.add(Grain{
				.buffer = _input.ref(),
				.window = generateLinearEnvelope(overlap, sustain, overlap),
				.position = _playbackPos,
				.speed = _speed
			});

			_playbackPos = _playbackPos + (f32)hop * (1.0f / _stretch);

			if (_loop) {
				_playbackPos = fmod(_playbackPos, (f32)_input.getFrameCount());
			} else {
				if (_playbackPos >= (f32)_input.getFrameCount()) {
					_finishing = true;
				}
			}

			_nextGrain = hop - 1;
		}
	};
}
