#pragma once

#include <span>

#include "foundation/DataBuffer.h"

namespace fw {
	using AudioSampleT = f32;

	template <const uint32 _ChannelCount, typename T = AudioSampleT>
	class AudioFrame {
	private:
		T _samples[_ChannelCount];

	public:
		constexpr static const uint32 ChannelCount = _ChannelCount;

		AudioFrame() {}
		AudioFrame(const AudioFrame& other) { *this = other; }
		AudioFrame(const std::array<T, _ChannelCount>& samples) { *this = samples; }

		constexpr T& operator[](uint32 channelIdx) { return _samples[channelIdx]; }
		constexpr const T& operator[](uint32 channelIdx) const { return _samples[channelIdx]; }

		constexpr AudioFrame& operator=(const AudioFrame& other) {
			for (size_t i = 0; i < _ChannelCount; ++i) {
				_samples[i] = other._samples[i];
			}

			return *this;
		}

		constexpr AudioFrame& operator=(const std::array<T, _ChannelCount>& samples) {
			for (size_t i = 0; i < _ChannelCount; ++i) {
				_samples[i] = samples[i];
			}

			return *this;
		}

		constexpr AudioFrame operator+(const AudioFrame& other) {
			AudioFrame out = *this;
			out += other;
			return out;
		}

		constexpr AudioFrame& operator+=(const AudioFrame& other) {
			for (size_t i = 0; i < _ChannelCount; ++i) {
				_samples[i] += other._samples[i];
			}

			return *this;
		}

		constexpr AudioFrame operator*(const AudioFrame& other) {
			AudioFrame out = *this;
			out *= other;
			return out;
		}

		constexpr AudioFrame& operator*=(const AudioFrame& other) {
			for (size_t i = 0; i < _ChannelCount; ++i) {
				_samples[i] *= other._samples[i];
			}

			return *this;
		}

		constexpr AudioFrame operator*(f32 v) {
			AudioFrame out = *this;
			out *= v;
			return out;
		}

		constexpr AudioFrame& operator*=(f32 v) {
			for (size_t i = 0; i < _ChannelCount; ++i) {
				_samples[i] *= v;
			}

			return *this;
		}

		constexpr T l() const { return _samples[0]; }
		constexpr T r() const { return _samples[1]; }

		constexpr T* raw() { return _samples; }
		constexpr const T* raw() const { return _samples; }
	};

	template <typename T = AudioSampleT>
	class TypedAudioBuffer {
	private:
		DataBuffer<T> _frames;
		f32 _sampleRate = 44100.0f;
		uint32 _frameCount = 0;
		uint32 _channelCount = 0;

	public:
		struct Frame {
			T* samples;
			uint32 channelCount;

			T l() const { assert(channelCount > 0); return samples[0]; }
			T r() const { assert(channelCount > 1); return samples[1]; }
			T at(uint32 channel) const { assert(channel < channelCount); return samples[channel]; }
		};

		TypedAudioBuffer() {}
		TypedAudioBuffer(uint32 frameCount, uint32 channelCount, f32 sampleRate)
			: _frameCount(frameCount), _channelCount(channelCount), _sampleRate(sampleRate), _frames(frameCount* channelCount) {}

		uint32 getFrameCount() const {
			return _frameCount;
		}

		T getSample(uint32 frame, uint32 channel) const {
			assert(channel < _channelCount);
			assert(frame < _frames.size() / _channelCount);
			return _frames[frame * _channelCount + channel];
		}

		DataBuffer<T>& getBuffer() {
			return _frames;
		}

		const DataBuffer<T>& getBuffer() const {
			return _frames;
		}

		Frame getFrame(uint32 frame) {
			uint32 offset = frame * _channelCount;
			assert(offset < _frames.size());
			assert(offset + _channelCount <= _frames.size());

			return Frame{
				.samples = _frames.data() + offset,
				.channelCount = _channelCount
			};
		}
	};

	template <const uint32 _ChannelCount, typename T = AudioSampleT>
	class InterleavedAudioBuffer {
	public:
		constexpr static const uint32 ChannelCount = _ChannelCount;
		using Frame = AudioFrame<_ChannelCount, T>;

	private:
		DataBuffer<Frame> _frames;
		f32 _sampleRate = 48000.0f;

	public:
		InterleavedAudioBuffer() {}
		InterleavedAudioBuffer(uint32 frameCount, f32 sampleRate = 48000) : _frames(frameCount), _sampleRate(sampleRate) {}
		InterleavedAudioBuffer(const DataBuffer<Frame>& other, f32 sampleRate = 48000) : _frames(other), _sampleRate(sampleRate) {}
		InterleavedAudioBuffer(DataBuffer<Frame>&& other, f32 sampleRate = 48000) : _frames(std::move(other)), _sampleRate(sampleRate) {}
		InterleavedAudioBuffer(const InterleavedAudioBuffer& other) { *this = other; }
		InterleavedAudioBuffer(InterleavedAudioBuffer&& other) noexcept { *this = std::move(other); }
		InterleavedAudioBuffer(Frame* frames, uint32 frameCount, f32 sampleRate = 48000, bool ownsData = false) : _frames(frames, frameCount, ownsData), _sampleRate(sampleRate) {}
		InterleavedAudioBuffer(T* samples, uint32 frameCount, f32 sampleRate = 48000, bool ownsData = false) : _frames((Frame*)samples, frameCount, ownsData), _sampleRate(sampleRate) {}
		~InterleavedAudioBuffer() = default;

		void resize(uint32 frameCount) {
			_frames.resize(frameCount);
		}

		void clear() {
			_frames.clear();
		}

		InterleavedAudioBuffer slice(uint32 offset, uint32 size) {
			return InterleavedAudioBuffer(_frames.slice(offset, size), _sampleRate);
		}

		InterleavedAudioBuffer ref() {
			return slice(0, (uint32)_frames.size());
		}

		InterleavedAudioBuffer clone() const {
			return InterleavedAudioBuffer(_frames.clone(), _sampleRate);
		}

		void setSample(uint32 frame, uint32 channel, T value) {
			assert(frame < _frames.size());
			assert(channel < ChannelCount);
			_frames[frame][channel] = value;
		}

		T getSample(uint32 frame, uint32 channel) const {
			assert(frame < _frames.size());
			assert(channel < ChannelCount);
			return _frames[frame][channel];
		}

		void setFrame(uint32 frame, const Frame& other) {
			assert(frame < _frames.size());
			_frames[frame] = other;
		}

		Frame& operator[](uint32 frame) {
			assert(frame < _frames.size());
			return _frames[frame];
		}

		const Frame& operator[](uint32 frame) const {
			assert(frame < _frames.size());
			return _frames[frame];
		}

		T* getSamples() {
			return (T*)_frames.data();
		}

		const T* getSamples() const {
			return (const T*)_frames.data();
		}

		DataBuffer<T> getSampleBuffer() {
			return DataBuffer<T>(getSamples(), getSampleCount());
		}

		const DataBuffer<T> getSampleBuffer() const {
			return DataBuffer<T>(getSamples(), getSampleCount());
		}

		DataBuffer<Frame>& getBuffer() {
			return _frames;
		}

		const DataBuffer<Frame>& getBuffer() const {
			return _frames;
		}

		uint32 getFrameCount() const {
			return (uint32)_frames.size();
		}

		uint32 getSampleCount() const {
			return (uint32)_frames.size() * ChannelCount;
		}

		bool isEmpty() const {
			return _frames.size() == 0;
		}

		f32 getSampleRate() const {
			return _sampleRate;
		}

		InterleavedAudioBuffer& operator=(const DataBuffer<Frame>& other) {
			_frames = other;
			return *this;
		}

		InterleavedAudioBuffer& operator=(DataBuffer<Frame>&& other) {
			_frames = std::move(other);
			return *this;
		}

		InterleavedAudioBuffer& operator=(const InterleavedAudioBuffer& other) {
			_frames = other._frames;
			_sampleRate = other._sampleRate;
			return *this;
		}

		InterleavedAudioBuffer& operator=(InterleavedAudioBuffer&& other) noexcept {
			_frames = std::move(other._frames);
			_sampleRate = other._sampleRate;
			return *this;
		}
	};

	using AudioBuffer = TypedAudioBuffer<AudioSampleT>;
	using MonoAudioBuffer = InterleavedAudioBuffer<1>;
	using StereoAudioBuffer = InterleavedAudioBuffer<2>;
}
