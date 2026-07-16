#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"
#include "Utilities/Audio/blip_buf.h"
#include "Utilities/Audio/HermiteResampler.h"
#include "Utilities/Audio/StereoDelayFilter.h"
#include "Utilities/Audio/StereoPanningFilter.h"
#include "Utilities/Audio/StereoCombFilter.h"
#include "NesTypes.h"

class NesConsole;
class SoundMixer;
class EmuSettings;
enum class ConsoleRegion;

class NesSoundMixer : public ISerializable
{
public:
	static constexpr uint32_t CycleLength = 2500;
	static constexpr uint32_t BitsPerSample = 16;

private:
	static constexpr uint32_t MaxSampleRate = 96000;
	static constexpr uint32_t MaxSamplesPerFrame = MaxSampleRate / 60 * 4 * 2; //x4 to allow CPU overclocking up to 10x, x2 for panning stereo
	static constexpr uint32_t MaxChannelCount = 11;

	NesConsole* _console = nullptr;
	SoundMixer* _mixer = nullptr;

	StereoPanningFilter _stereoPanning;
	StereoDelayFilter _stereoDelay;
	StereoCombFilter _stereoCombFilter;

	int16_t _previousOutputLeft = 0;
	int16_t _previousOutputRight = 0;

	vector<uint32_t> _timestamps;
	int16_t _channelOutput[MaxChannelCount][CycleLength] = {};
	int16_t _currentOutput[MaxChannelCount] = {};

	blip_t* _blipBufLeft = nullptr;
	blip_t* _blipBufRight = nullptr;
	int16_t* _outputBuffer = nullptr;
	size_t _sampleCount = 0;
	double _volumes[MaxChannelCount] = {};
	double _panning[MaxChannelCount] = {};

	uint32_t _sampleRate = 0;
	uint32_t _clockRate = 0;

	bool _hasPanning = false;

	// --- RetroPlug per-channel capture — spec/10 §5 (pins) + §5b (individual mono) --------------------
	// OFF by default → the mix path above is byte-identical and pays zero cost. When MesenNesSystem
	// enables it, EndFrame ALSO feeds separate stream blips, each mirroring the mix's clock→96k-blip→
	// Hermite→host path so they stay sample-aligned with each other. The mode (== channelExportMode):
	//   1 StereoModPins: [0] Pulse (squareVolume), [1] TND (tndVolume), [2] Expansion (six terms) — 3
	//                    streams that provably re-sum to the mix.
	//   2 Pins+Reference: the above + [3] the full mix scalar through the SAME path — fidelity-test only,
	//                    making Σ(pins) == reference exact within one instance (no cross-resampler drift).
	//   3 IndividualMono: the 5 core channels [0] Square1 [1] Square2 [2] Triangle [3] Noise [4] DMC as
	//                    raw pre-DAC linear levels (per-channel scaled) — for isolation; these DON'T sum.
	// The tap only READS _currentOutput; the mixed _blipBuf*/_previousOutput*/_channelOutput/_timestamps
	// are never touched.
	static constexpr uint32_t MaxCaptureStreams = 5;  // 3 pins (+ ref), or 5 core mono channels
	uint32_t _captureMode = 0;                        // 0 = off, else the active channelExportMode
	uint32_t _captureStreams = 0;                     // stream count for _captureMode (0/3/4/5)
	uint32_t _captureHostRate = 0;
	blip_t* _streamBlip[MaxCaptureStreams] = {};
	int16_t _streamPrev[MaxCaptureStreams] = {};
	HermiteResampler _streamResampler[MaxCaptureStreams];
	vector<int16_t> _streamScratch;               // interleaved 96k read + expand scratch
	vector<int16_t> _streamOutScratch;            // interleaved host resample scratch
	vector<int16_t> _streamBuf[MaxCaptureStreams];// host-rate mono ring (drained by MesenNesSystem)

	__forceinline double GetChannelOutput(AudioChannel channel, bool forRightChannel);
	__forceinline int16_t GetOutputVolume(bool forRightChannel);
	// Split the mono (forRightChannel=false) mix into its two hardware pins + the lumped expansion term,
	// using the SAME math as GetOutputVolume (whose body is left untouched to keep the mix byte-identical).
	__forceinline void GetStreamVolumes(uint16_t& square, uint16_t& tnd, int32_t& expansion);
	// The 5 core channels' raw pre-DAC linear levels, each per-channel-scaled to ~half int16 (§5b). These
	// bypass the non-linear DAC, so they do NOT re-sum to the mix.
	__forceinline void GetCoreChannelLevels(int16_t out[5]);
	void CaptureStreams();                        // drain the stream blips → host mono rings
	void EndFrame(uint32_t time);

	void ProcessVsDualSystemAudio();

	void UpdateRates(bool forceUpdate);
	
public:
	NesSoundMixer(NesConsole* console);
	virtual ~NesSoundMixer();

	void SetRegion(ConsoleRegion region);
	void Reset();

	void PlayAudioBuffer(uint32_t cycle);
	void AddDelta(AudioChannel channel, uint32_t time, int16_t delta);

	// RetroPlug per-channel capture (spec/10 §5/§5b). `mode` is the channelExportMode: 0 = off, 1 = pins,
	// 2 = pins + reference (fidelity test), 3 = 5 individual core mono channels. `hostRate` is passed in
	// (the caller's sample rate) so the mixer needn't reach into settings. AvailableCaptureFrames reports
	// host frames ready (all captured streams stay equal length); DrainChannel pops `frameCount` mono float
	// samples from stream k (int16→float ÷32768, erase-from-front), like MesenAudioDevice::drain.
	void SetChannelCapture(uint32_t mode, uint32_t hostRate);
	uint32_t AvailableCaptureFrames() const;
	uint32_t DrainChannel(uint32_t stream, float* dest, uint32_t frameCount);

	void Serialize(Serializer& s) override;
};
