#pragma once

#include "foundation/DataBuffer.h"

namespace fw {
	template<typename SampleType = f32>
	class AudioBufferT {
	private:
		std::vector<DataBuffer<SampleType>> _channels;
		uint32 _channelCount;
		uint32 _sampleCount;
		f32 _sampleRate = 44100.0f;

	public:
		using SampleTypeT = SampleType;

		AudioBufferT(uint32 channels = 2, uint32 samples = 0, f32 sampleRate = 44100.0f)
			: _channelCount(channels), _sampleCount(samples), _sampleRate(sampleRate) {
			resize(channels, samples);
		}

		AudioBufferT(const AudioBufferT& other)
			: _channelCount(other._channelCount), _sampleCount(other._sampleCount), _sampleRate(other._sampleRate) {
			_channels.resize(_channelCount);
			for (uint32 ch = 0; ch < _channelCount; ++ch) {
				_channels[ch] = DataBuffer<SampleType>(_sampleCount);
				std::memcpy(_channels[ch].data(), other._channels[ch].data(),
							_sampleCount * sizeof(SampleType));
			}
		}

		AudioBufferT(AudioBufferT&& other) noexcept
			: _channels(std::move(other._channels)),
			_channelCount(other._channelCount),
			_sampleCount(other._sampleCount),
			_sampleRate(other._sampleRate) {
			other._channelCount = 0;
			other._sampleCount = 0;
		}

		// Copy assignment
		AudioBufferT& operator=(const AudioBufferT& other) {
			if (this != &other) {
				_sampleRate = other._sampleRate;
				resize(other._channelCount, other._sampleCount);
				for (uint32 ch = 0; ch < _channelCount; ++ch) {
					std::memcpy(_channels[ch].data(), other._channels[ch].data(),
								_sampleCount * sizeof(SampleType));
				}
			}
			return *this;
		}

		// Move assignment
		AudioBufferT& operator=(AudioBufferT&& other) noexcept {
			if (this != &other) {
				_channels = std::move(other._channels);
				_channelCount = other._channelCount;
				_sampleCount = other._sampleCount;
				_sampleRate = other._sampleRate;
				other._channelCount = 0;
				other._sampleCount = 0;
			}
			return *this;
		}

		// Resize buffer
		void resize(uint32 newChannels, uint32 newSamples) {
			_channelCount = newChannels;
			_sampleCount = newSamples;
			_channels.clear();
			_channels.resize(_channelCount);

			for (uint32 ch = 0; ch < _channelCount; ++ch) {
				_channels[ch] = DataBuffer<SampleType>(newSamples);
			}
		}

		// Get channel data pointer (const)
		const SampleType* getReadPointer(uint32 channel) const {
			assert(channel < _channelCount);
			return _channels[channel].data();
		}

		// Get channel data pointer (non-const)
		SampleType* getWritePointer(uint32 channel) {
			assert(channel < _channelCount);
			return _channels[channel].data();
		}

		void setSampleRate(f32 sampleRate) {
			_sampleRate = sampleRate;
		}

		// Access operators
		const SampleType& operator()(uint32 channel, uint32 sample) const {
			return _channels[channel][sample];
		}

		SampleType& operator()(uint32 channel, uint32 sample) {
			return _channels[channel][sample];
		}

		// Clear all channels
		void clear() {
			for (uint32 ch = 0; ch < _channelCount; ++ch) {
				std::memset(_channels[ch].data(), 0, _sampleCount * sizeof(SampleType));
			}
		}

		// Clear specific channel
		void clearChannel(uint32 channel) {
			if (channel < _channelCount) {
				std::memset(_channels[channel].data(), 0, _sampleCount * sizeof(SampleType));
			}
		}

		// Clear range of samples in a channel
		void clearSamples(uint32 channel, uint32 startSample, uint32 _sampleCountToClear) {
			if (channel < _channelCount && startSample < _sampleCount) {
				uint32 samplesToZero = std::min(_sampleCountToClear, _sampleCount - startSample);
				std::memset(&_channels[channel][startSample], 0, samplesToZero * sizeof(SampleType));
			}
		}

		// Copy from another buffer
		void copyFrom(const AudioBufferT& source, uint32 srcChannel, uint32 destChannel) {
			if (destChannel < _channelCount && srcChannel < source._channelCount) {
				uint32 samplesToCopy = std::min(_sampleCount, source._sampleCount);
				std::memcpy(_channels[destChannel].data(),
							source._channels[srcChannel].data(),
							samplesToCopy * sizeof(SampleType));
			}
		}

		// Copy range of samples
		void copyFrom(const AudioBufferT& source, uint32 srcChannel, uint32 destChannel,
					uint32 startSample, uint32 _sampleCountToCopy) {
			if (destChannel < _channelCount && srcChannel < source._channelCount &&
				startSample < _sampleCount && startSample < source._sampleCount) {

				uint32 samplesToCopy = std::min({_sampleCountToCopy,
												_sampleCount - startSample,
												source._sampleCount - startSample});

				std::memcpy(&_channels[destChannel].get(startSample),
							&source._channels[srcChannel].get(startSample),
							samplesToCopy * sizeof(SampleType));
			}
		}

		// Add samples from another buffer (mixing)
		void addFrom(const AudioBufferT& source, uint32 srcChannel, uint32 destChannel,
					SampleType gain = SampleType(1)) {
			if (destChannel < _channelCount && srcChannel < source._channelCount) {
				uint32 samplesToAdd = std::min(_sampleCount, source._sampleCount);
				auto* dest = _channels[destChannel].data();
				const auto* src = source._channels[srcChannel].data();

				for (uint32 i = 0; i < samplesToAdd; ++i) {
					dest[i] += src[i] * gain;
				}
			}
		}

		void fromInterleaved(const SampleType* interleavedData, uint32 channels, uint32 samples) {
			resize(channels, samples);

			for (uint32 ch = 0; ch < _channelCount; ++ch) {
				SampleType* dest = _channels[ch].data();
				for (uint32 s = 0; s < _sampleCount; ++s) {
					dest[s] = interleavedData[s * _channelCount + ch];
				}
			}
		}

		// Convert to interleaved format
		void toInterleaved(SampleType* interleavedData, uint32 channels, uint32 samples) const {
			assert(channels == _channelCount && samples == _sampleCount);
			for (uint32 s = 0; s < _sampleCount; ++s) {
				for (uint32 ch = 0; ch < _channelCount; ++ch) {
					interleavedData[s * _channelCount + ch] = _channels[ch][s];
				}
			}
		}

		// Apply gain to channel
		void applyGain(uint32 channel, SampleType gain) {
			if (channel < _channelCount) {
				auto* data = _channels[channel].data();
				for (uint32 i = 0; i < _sampleCount; ++i) {
					data[i] *= gain;
				}
			}
		}

		void applyGain(SampleType gain) {
			for (uint32 ch = 0; ch < _channelCount; ++ch) {
				applyGain(ch, gain);
			}
		}

		// Getters
		uint32 getChannelCount() const { return _channelCount; }
		uint32 getSampleCount() const { return _sampleCount; }
		uint32 getSizeInBytes() const { return _channelCount * _sampleCount * sizeof(SampleType); }
		f32 getSampleRate() const { return _sampleRate; }

		bool isEmpty() const { return _channelCount == 0 || _sampleCount == 0; }

		// Iterator support for range-based loops on individual channels
		class ChannelIterator {
		private:
			SampleType* ptr;
		public:
			explicit ChannelIterator(SampleType* p) : ptr(p) {}
			SampleType& operator*() { return *ptr; }
			SampleType* operator->() { return ptr; }
			ChannelIterator& operator++() { ++ptr; return *this; }
			bool operator!=(const ChannelIterator& other) const { return ptr != other.ptr; }
		};

		ChannelIterator begin(uint32 channel) {
			return ChannelIterator(_channels[channel].data());
		}

		ChannelIterator end(uint32 channel) {
			return ChannelIterator(_channels[channel].data() + _sampleCount);
		}
	};

	using AudioBuffer = AudioBufferT<f32>;
}
