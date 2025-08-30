/*
 * BiquadEffect Usage Example
 *
 * This file demonstrates how to use the BiquadEffect class for audio filtering.
 * The BiquadEffect implements a second-order IIR filter (biquad) that supports
 * various filter types including low-pass, high-pass, band-pass, and more.
 */

#include "core/audio/BiquadEffect.h"
#include "core/audio/EffectChain.h"
#include <memory>

namespace rp {

void biquadEffectUsageExample() {
	// Create a biquad effect instance
	auto biquadFilter = std::make_shared<BiquadEffect>();

	// Set sample rate (important to do this first!)
	biquadFilter->setSampleRate(44100.0f);

	// Example 1: Configure as a low-pass filter
	// Cuts frequencies above 1000 Hz with a moderate Q factor
	biquadFilter->configureLowPass(1000.0f, 0.707f);

	// Example 2: Manual configuration for high-pass filter
	biquadFilter->setFilterType(FilterType::HighPass);
	biquadFilter->setFrequency(200.0f);  // Cut frequencies below 200 Hz
	biquadFilter->setQ(1.0f);            // Sharper rolloff

	// Example 3: Peaking EQ for boosting mid frequencies
	biquadFilter->configurePeaking(2500.0f, 2.0f, 6.0f);  // +6dB boost at 2.5kHz

	// Example 4: Low shelf filter for bass adjustment
	biquadFilter->configureLowShelf(100.0f, 0.707f, -3.0f);  // -3dB cut below 100Hz

	// Example 5: High shelf filter for treble adjustment
	biquadFilter->configureHighShelf(8000.0f, 0.707f, 2.0f);  // +2dB boost above 8kHz

	// Example 6: Band-pass filter (useful for isolating frequency ranges)
	biquadFilter->configureBandPass(1000.0f, 5.0f);  // Narrow band around 1kHz

	// Example 7: Band-stop/notch filter (removes specific frequencies)
	biquadFilter->configureBandStop(60.0f, 10.0f);  // Remove 60Hz hum

	// Using with EffectChain
	EffectChain effectChain;
	effectChain.addEffect(biquadFilter);

	// To process audio (example with a mono buffer):
	// fw::MonoAudioBuffer audioBuffer(1024, 44100.0f);  // 1024 samples at 44.1kHz
	// effectChain.process(audioBuffer);

	// Reset filter state if needed (clears delay lines)
	biquadFilter->reset();
}

// Example: Creating a multi-band EQ using multiple biquad filters
void multiBandEQExample() {
	EffectChain eqChain;

	// Low shelf: gentle bass boost
	auto lowShelf = std::make_shared<BiquadEffect>();
	lowShelf->setSampleRate(44100.0f);
	lowShelf->configureLowShelf(100.0f, 0.707f, 2.0f);
	eqChain.addEffect(lowShelf);

	// Mid peaking: slight presence boost
	auto midPeak = std::make_shared<BiquadEffect>();
	midPeak->setSampleRate(44100.0f);
	midPeak->configurePeaking(3000.0f, 1.5f, 3.0f);
	eqChain.addEffect(midPeak);

	// High shelf: air boost
	auto highShelf = std::make_shared<BiquadEffect>();
	highShelf->setSampleRate(44100.0f);
	highShelf->configureHighShelf(10000.0f, 0.707f, 1.5f);
	eqChain.addEffect(highShelf);

	// Process audio through the EQ chain
	// fw::MonoAudioBuffer audioBuffer(1024, 44100.0f);
	// eqChain.process(audioBuffer);
}

// Example: Dynamic parameter changes (for automation or user controls)
void dynamicParameterExample(BiquadEffect& filter, float time) {
	// Sweep filter frequency over time (useful for filter sweeps)
	float frequency = 200.0f + (2000.0f * std::sin(time * 0.5f));  // Sweep between 200Hz and 2200Hz
	filter.setFrequency(frequency);

	// Dynamic Q changes
	float q = 0.5f + (2.0f * std::cos(time * 0.3f));  // Q between 0.5 and 2.5
	filter.setQ(q);
}

/*
 * Filter Type Guide:
 *
 * LowPass:   Allows frequencies below the cutoff to pass through
 *            Use for: Removing harsh highs, anti-aliasing, warmth
 *
 * HighPass:  Allows frequencies above the cutoff to pass through
 *            Use for: Removing rumble, cleaning up lows
 *
 * BandPass:  Allows frequencies around the center frequency to pass
 *            Use for: Isolating specific frequency ranges, telephone effect
 *
 * BandStop:  Removes frequencies around the center frequency (notch filter)
 *            Use for: Removing hum, feedback, specific problem frequencies
 *
 * Peak:      Boosts or cuts frequencies around the center frequency
 *            Use for: EQ, correcting frequency response
 *
 * LowShelf:  Boosts or cuts all frequencies below the cutoff equally
 *            Use for: Bass adjustment, low-end shaping
 *
 * HighShelf: Boosts or cuts all frequencies above the cutoff equally
 *            Use for: Treble adjustment, presence control
 *
 * AllPass:   Passes all frequencies but changes phase relationships
 *            Use for: Phase alignment, special effects
 *
 *
 * Parameter Guidelines:
 *
 * Frequency: The center/cutoff frequency in Hz
 *           - Audio range: 20 Hz to 20,000 Hz
 *           - Most musical content: 80 Hz to 8,000 Hz
 *
 * Q Factor:  Controls the sharpness of the filter
 *           - Low Q (0.1-1.0): Wide, gentle slope
 *           - Medium Q (1.0-5.0): Moderate slope
 *           - High Q (5.0+): Sharp, narrow slope
 *           - Butterworth response: Q = 0.707 (1/√2)
 *
 * Gain:     Amount of boost/cut in dB (for Peak/Shelf filters)
 *           - Typical range: -12 dB to +12 dB
 *           - Subtle changes: ±1-3 dB
 *           - Moderate changes: ±3-6 dB
 *           - Strong changes: ±6-12 dB
 */

}  // namespace rp
