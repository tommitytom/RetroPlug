#pragma once

#include <vector>

#include "foundation/Types.h"
#include "foundation/Math.h"

namespace fw {
	class SineOsc {
	private:
		f32 _frequency = 200.0f;
		f32 _phase = 0.0f;
		f32 _step = 0.0f;
		f32 _sampleRate = 48000;

	public:
		SineOsc(f32 freq, f32 sampleRate) : _frequency(freq), _sampleRate(sampleRate) { updateStep(); }
		~SineOsc() = default;

		void setFreq(f32 freq) {
			_frequency = freq;
			updateStep();
		}

		void setSampleRate(f32 sr) {
			_sampleRate = sr;
			updateStep();
		}

		f32 next() {
			f32 val = sin(_phase);
			_phase = fmod(_phase + _step, PI2);
			return val;
		}

	private:
		void updateStep() {
			_step = _frequency / _sampleRate;
		}
	};

	class SineBankOsc {
	private:
		std::vector<std::pair<SineOsc, f32>> _oscilators;

	public:
		SineBankOsc(size_t count, f32 sampleRate) {
			_oscilators.reserve(count);

			for (size_t i = 0; i < count; ++i) {
				_oscilators.push_back({ SineOsc(0.0f, sampleRate), 0.0f });
			}
		}

		void setOscSettings(size_t idx, f32 freq, f32 amp) {
			_oscilators[idx].first.setFreq(freq);
			_oscilators[idx].second = amp;
		}

		void setSampleRate(f32 sampleRate) {
			for (size_t i = 0; i < _oscilators.size(); ++i) {
				_oscilators[i].first.setSampleRate(sampleRate);
			}
		}

		f32 next() {
			f32 val = 0.0f;
			for (std::pair<SineOsc, f32>& osc : _oscilators) { val += osc.first.next() * osc.second; }
			return val;
		}

		size_t getOscCount() const {
			return _oscilators.size();
		}
	};
}
