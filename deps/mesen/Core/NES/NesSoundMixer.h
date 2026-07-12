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

	// --- RetroPlug per-channel (stereo-mod pin) capture — spec/10 §5 ---------------------------------
	// OFF by default → the mix path above is byte-identical and pays zero cost. When MesenNesSystem
	// enables it (channelExportMode == StereoModPins), EndFrame ALSO feeds separate stream blips —
	// [0] Pulse (squareVolume), [1] TND (tndVolume), [2] Expansion (the six expansion terms) — each
	// mirroring the mix's clock→96k-blip→Hermite→host path so they stay sample-aligned with each other and
	// provably re-sum. An optional [3] Reference stream (the full mix scalar through the SAME path) is
	// captured for the fidelity test only (withReference) — it makes Σ(pins) == reference exact within one
	// instance, without cross-instance/cross-resampler drift. The tap only READS _currentOutput; the mixed
	// _blipBuf*/_previousOutput*/_channelOutput/_timestamps are never touched.
	static constexpr uint32_t MaxCaptureStreams = 4;  // 3 pins (+ optional mix reference)
	uint32_t _captureStreams = 0;                     // 0 = off, 3 = pins, 4 = pins + reference
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

	// RetroPlug per-channel capture (spec/10 §5). Enable/disable the stereo-mod pin tap; `hostRate` is
	// passed in (the caller's sample rate) so the mixer needn't reach into settings. `withReference` adds a
	// 4th stream (the full mix scalar through the same path) for the fidelity test. AvailableCaptureFrames
	// reports host frames ready (all captured streams stay equal length); DrainChannel pops `frameCount`
	// mono float samples from stream k (int16→float ÷32768, erase-from-front), like MesenAudioDevice::drain.
	void SetChannelCapture(bool enabled, uint32_t hostRate, bool withReference = false);
	uint32_t AvailableCaptureFrames() const;
	uint32_t DrainChannel(uint32_t stream, float* dest, uint32_t frameCount);

	void Serialize(Serializer& s) override;
};
