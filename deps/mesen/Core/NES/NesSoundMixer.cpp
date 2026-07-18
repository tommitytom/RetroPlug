#include "pch.h"
#include "NES/NesSoundMixer.h"
#include "NES/NesConsole.h"
#include "NES/NesConstants.h"
#include "NES/NesTypes.h"
#include "Shared/Emulator.h"
#include "Shared/SettingTypes.h"
#include "Shared/Audio/SoundMixer.h"
#include "Utilities/Serializer.h"
#include "Utilities/Audio/blip_buf.h"

NesSoundMixer::NesSoundMixer(NesConsole* console)
{
	_clockRate = 0;
	_console = console;
	_mixer = console->GetEmulator()->GetSoundMixer();
	_outputBuffer = new int16_t[NesSoundMixer::MaxSamplesPerFrame];
	_blipBufLeft = blip_new(NesSoundMixer::MaxSamplesPerFrame);
	_blipBufRight = blip_new(NesSoundMixer::MaxSamplesPerFrame);
	_sampleRate = 96000;
}

NesSoundMixer::~NesSoundMixer()
{
	delete[] _outputBuffer;
	_outputBuffer = nullptr;

	blip_delete(_blipBufLeft);
	blip_delete(_blipBufRight);

	for(uint32_t i = 0; i < MaxCaptureStreams; i++) {
		if(_streamBlip[i]) {
			blip_delete(_streamBlip[i]);
			_streamBlip[i] = nullptr;
		}
	}
}

void NesSoundMixer::Serialize(Serializer& s)
{
	SV(_clockRate);
	SV(_sampleRate);

	if(!s.IsSaving()) {
		Reset();
		UpdateRates(true);
	}

	SVArray(_currentOutput, MaxChannelCount);
	SV(_previousOutputLeft);
	SV(_previousOutputRight);
}

void NesSoundMixer::Reset()
{
	_sampleCount = 0;

	_previousOutputLeft = 0;
	_previousOutputRight = 0;
	blip_clear(_blipBufLeft);
	blip_clear(_blipBufRight);

	// RetroPlug per-channel tap: a savestate load runs Reset — clear the stream blips + accumulators so
	// no stale pin state carries across. (No-op when capture is off.)
	for(uint32_t i = 0; i < _captureStreams; i++) {
		if(_streamBlip[i]) {
			blip_clear(_streamBlip[i]);
		}
		_streamPrev[i] = 0;
		_streamResampler[i].Reset();
		_streamResampler[i].SetSampleRates(_sampleRate, _captureHostRate);
		_streamBuf[i].clear();
	}

	_timestamps.clear();

	for(uint32_t i = 0; i < MaxChannelCount; i++) {
		_volumes[i] = 1.0;
		_panning[i] = 0;
	}
	memset(_channelOutput, 0, sizeof(_channelOutput));
	memset(_currentOutput, 0, sizeof(_currentOutput));

	UpdateRates(true);
}

void NesSoundMixer::PlayAudioBuffer(uint32_t time)
{
	EndFrame(time);

	// RetroPlug per-channel tap: drain the pin blips (96k) through their own resamplers into the host
	// mono rings, right after EndFrame — same cadence the mix drains below. No-op when capture is off.
	if(_captureStreams > 0) {
		CaptureStreams();
	}

	int16_t* out = _outputBuffer + (_sampleCount * 2);
	size_t sampleCount = blip_read_samples(_blipBufLeft, out, NesSoundMixer::MaxSamplesPerFrame, 1);

	if(_hasPanning) {
		blip_read_samples(_blipBufRight, out + 1, NesSoundMixer::MaxSamplesPerFrame, 1);
	} else {
		//Copy left channel to right channel (optimization - when no panning is used)
		for(size_t i = 0; i < sampleCount * 2; i += 2) {
			out[i + 1] = out[i];
		}
	}

	_sampleCount += sampleCount;

	if(_console->GetVsMainConsole()) {
		//Keep samples in buffer if this is the VS dualsystem sub console - the main console will read them and play them
		return;
	}

	NesConfig& cfg = _console->GetNesConfig();
	if(_console->GetVsSubConsole()) {
		ProcessVsDualSystemAudio();
	}

	switch(cfg.StereoFilter) {
		case StereoFilterType::None: break;
		case StereoFilterType::Delay: _stereoDelay.ApplyFilter(_outputBuffer, _sampleCount, _sampleRate, cfg.StereoDelay); break;
		case StereoFilterType::Panning: _stereoPanning.ApplyFilter(_outputBuffer, _sampleCount, cfg.StereoPanningAngle); break;
		case StereoFilterType::CombFilter: _stereoCombFilter.ApplyFilter(_outputBuffer, _sampleCount, _sampleRate, cfg.StereoCombFilterDelay, cfg.StereoCombFilterStrength); break;
	}

	_mixer->PlayAudioBuffer(_outputBuffer, (uint32_t)_sampleCount, 96000);
	_sampleCount = 0;

	UpdateRates(false);
}

void NesSoundMixer::ProcessVsDualSystemAudio()
{
	NesConfig& cfg = _console->GetNesConfig();

	//If this is a VS dualsystem game
	if(cfg.VsDualAudioOutput == VsDualOutputOption::SubSystemOnly) {
		//Mute the main system's sound
		memset(_outputBuffer, 0, _sampleCount * sizeof(int16_t));
	}

	NesSoundMixer* subMixer = _console->GetVsSubConsole()->GetSoundMixer();
	if(cfg.VsDualAudioOutput != VsDualOutputOption::MainSystemOnly) {
		size_t i;
		for(i = 0; i < _sampleCount && subMixer->_sampleCount; i++) {
			_outputBuffer[i * 2] += subMixer->_outputBuffer[i * 2];
			_outputBuffer[i * 2 + 1] += subMixer->_outputBuffer[i * 2 + 1];
		}

		if(i < subMixer->_sampleCount) {
			size_t samplesToCopy = subMixer->_sampleCount - i;
			memmove(subMixer->_outputBuffer, subMixer->_outputBuffer + i * 2, samplesToCopy * 2 * sizeof(int16_t));
			subMixer->_sampleCount = samplesToCopy;
		}
	} else {
		subMixer->_sampleCount = 0;
	}
}

void NesSoundMixer::SetRegion(ConsoleRegion region)
{
	UpdateRates(true);
}

void NesSoundMixer::SetLatencyMs(double ms)
{
	// RetroPlug: the flush window as a latency (ms). Store it; the actual cycle count depends on the
	// region CPU clock, so defer to UpdateCycleLength (which also re-runs on a clock change via UpdateRates).
	_latencyMs = ms;
	UpdateCycleLength();
}

void NesSoundMixer::UpdateCycleLength()
{
	// cycleLength (CPU cycles) = latencySeconds * cpuClock. Region-correct + sample-rate-independent.
	if(_clockRate == 0) {
		return;  // clock not known yet — the default _cycleLength holds until UpdateRates supplies one
	}
	double cycles = _latencyMs / 1000.0 * (double)_clockRate + 0.5;  // +0.5 = round-to-nearest
	uint32_t c = (cycles <= (double)MinCycleLength) ? MinCycleLength : (uint32_t)cycles;
	if(c < MinCycleLength) { c = MinCycleLength; }
	if(c > MaxCycleLength) { c = MaxCycleLength; }
	_cycleLength = c;
}

void NesSoundMixer::UpdateRates(bool forceUpdate)
{
	uint32_t clockRate = NesConstants::GetClockRate(_console->GetRegion());
	if(forceUpdate || _clockRate != clockRate) {
		_clockRate = clockRate;

		// RetroPlug: the flush window is stored as a latency (ms); its cycle count depends on this clock,
		// so recompute it whenever the region/clock changes.
		UpdateCycleLength();

		blip_set_rates(_blipBufLeft, _clockRate, _sampleRate);
		blip_set_rates(_blipBufRight, _clockRate, _sampleRate);

		// RetroPlug per-channel tap: the stream blips render at the SAME 96k as the mix blip (their own
		// Hermite resamplers take 96k→host), so re-rate them alongside on a region/clock change.
		for(uint32_t i = 0; i < _captureStreams; i++) {
			if(_streamBlip[i]) {
				blip_set_rates(_streamBlip[i], _clockRate, _sampleRate);
			}
		}
	}

	NesConfig& cfg = _console->GetNesConfig();
	bool hasPanning = false;
	for(uint32_t i = 0; i < MaxChannelCount; i++) {
		_volumes[i] = cfg.ChannelVolumes[i] / 100.0;
		_panning[i] = (cfg.ChannelPanning[i] + 100) / 100.0;
		if(_panning[i] != 1.0) {
			if(!_hasPanning) {
				blip_clear(_blipBufLeft);
				blip_clear(_blipBufRight);
			}
			hasPanning = true;
		}
	}
	_hasPanning = hasPanning;
}

double NesSoundMixer::GetChannelOutput(AudioChannel channel, bool forRightChannel)
{
	if(forRightChannel) {
		return _currentOutput[(int)channel] * _volumes[(int)channel] * _panning[(int)channel];
	} else {
		return _currentOutput[(int)channel] * _volumes[(int)channel] * (2.0 - _panning[(int)channel]);
	}
}

int16_t NesSoundMixer::GetOutputVolume(bool forRightChannel)
{
	double squareOutput = GetChannelOutput(AudioChannel::Square1, forRightChannel) + GetChannelOutput(AudioChannel::Square2, forRightChannel);
	double tndOutput = GetChannelOutput(AudioChannel::DMC, forRightChannel) + 2.7516713261 * GetChannelOutput(AudioChannel::Triangle, forRightChannel) + 1.8493587125 * GetChannelOutput(AudioChannel::Noise, forRightChannel);

	uint16_t squareVolume = (uint16_t)((95.88*5000.0) / (8128.0 / squareOutput + 100.0));
	uint16_t tndVolume = (uint16_t)((159.79*5000.0) / (22638.0 / tndOutput + 100.0));

	return (int16_t)(squareVolume + tndVolume +
		GetChannelOutput(AudioChannel::FDS, forRightChannel) * 20 +
		GetChannelOutput(AudioChannel::MMC5, forRightChannel) * 43 +
		GetChannelOutput(AudioChannel::Namco163, forRightChannel) * 20 +
		GetChannelOutput(AudioChannel::Sunsoft5B, forRightChannel) * 15 +
		GetChannelOutput(AudioChannel::VRC6, forRightChannel) * 5 +
		GetChannelOutput(AudioChannel::VRC7, forRightChannel));
}

void NesSoundMixer::GetStreamVolumes(uint16_t& square, uint16_t& tnd, int32_t& expansion)
{
	// RetroPlug per-channel tap: the same terms GetOutputVolume sums, exposed separately for the pin
	// streams. Kept in lockstep with GetOutputVolume by hand (that body stays verbatim so the mix is
	// byte-identical). Mono only (forRightChannel = false — the 2A03 pins are pre-panning).
	double squareOutput = GetChannelOutput(AudioChannel::Square1, false) + GetChannelOutput(AudioChannel::Square2, false);
	double tndOutput = GetChannelOutput(AudioChannel::DMC, false) + 2.7516713261 * GetChannelOutput(AudioChannel::Triangle, false) + 1.8493587125 * GetChannelOutput(AudioChannel::Noise, false);

	square = (uint16_t)((95.88*5000.0) / (8128.0 / squareOutput + 100.0));
	tnd = (uint16_t)((159.79*5000.0) / (22638.0 / tndOutput + 100.0));

	expansion = (int32_t)(
		GetChannelOutput(AudioChannel::FDS, false) * 20 +
		GetChannelOutput(AudioChannel::MMC5, false) * 43 +
		GetChannelOutput(AudioChannel::Namco163, false) * 20 +
		GetChannelOutput(AudioChannel::Sunsoft5B, false) * 15 +
		GetChannelOutput(AudioChannel::VRC6, false) * 5 +
		GetChannelOutput(AudioChannel::VRC7, false));
}

void NesSoundMixer::GetCoreChannelLevels(int16_t out[5])
{
	// RetroPlug §5b: the 5 core channels' raw pre-DAC linear levels (GetChannelOutput == _currentOutput at
	// unity volume/pan), each scaled so a full-scale channel lands near half int16 — Square1/Square2/
	// Triangle/Noise range 0-15 (×1024), DMC ranges 0-127 (×128). Bypassing the DAC, they do NOT re-sum.
	out[0] = (int16_t)(GetChannelOutput(AudioChannel::Square1, false) * 1024);
	out[1] = (int16_t)(GetChannelOutput(AudioChannel::Square2, false) * 1024);
	out[2] = (int16_t)(GetChannelOutput(AudioChannel::Triangle, false) * 1024);
	out[3] = (int16_t)(GetChannelOutput(AudioChannel::Noise, false) * 1024);
	out[4] = (int16_t)(GetChannelOutput(AudioChannel::DMC, false) * 128);
}

void NesSoundMixer::AddDelta(AudioChannel channel, uint32_t time, int16_t delta)
{
	if(delta != 0) {
		_timestamps.push_back(time);
		_channelOutput[(int)channel][time] += delta;
	}
}

void NesSoundMixer::EndFrame(uint32_t time)
{
	sort(_timestamps.begin(), _timestamps.end());
	_timestamps.erase(std::unique(_timestamps.begin(), _timestamps.end()), _timestamps.end());

	for(size_t i = 0, len = _timestamps.size(); i < len; i++) {
		uint32_t stamp = _timestamps[i];
		for(uint32_t j = 0; j < MaxChannelCount; j++) {
			_currentOutput[j] += _channelOutput[j][stamp];
		}

		int16_t currentOutput = GetOutputVolume(false) * 4;
		blip_add_delta(_blipBufLeft, stamp, (int)(currentOutput - _previousOutputLeft));
		_previousOutputLeft = currentOutput;

		// RetroPlug per-channel tap: feed each stream into its own blip at the same stamp as the mix's left
		// channel. Reads _currentOutput only; the mix above is untouched. Mode 1/2 (pins) = the two 2A03
		// pins + lumped expansion at the mix's *4 scale (+ slot 3 = the full mix scalar `currentOutput` for
		// the fidelity reference). Mode 3 (individual mono) = the 5 core channels' raw pre-DAC levels.
		if(_captureStreams > 0) {
			int16_t sv[MaxCaptureStreams] = {};
			if(_captureMode == 3) {
				GetCoreChannelLevels(sv);
			} else {
				uint16_t square, tnd;
				int32_t expansion;
				GetStreamVolumes(square, tnd, expansion);
				sv[0] = (int16_t)(square * 4);
				sv[1] = (int16_t)(tnd * 4);
				sv[2] = (int16_t)(expansion * 4);
				sv[3] = currentOutput;  // reference (mode 2)
			}
			for(uint32_t k = 0; k < _captureStreams; k++) {
				blip_add_delta(_streamBlip[k], stamp, (int)(sv[k] - _streamPrev[k]));
				_streamPrev[k] = sv[k];
			}
		}

		if(_hasPanning) {
			currentOutput = GetOutputVolume(true) * 4;
			blip_add_delta(_blipBufRight, stamp, (int)(currentOutput - _previousOutputRight));
			_previousOutputRight = currentOutput;
		}
	}

	blip_end_frame(_blipBufLeft, time);
	if(_hasPanning) {
		blip_end_frame(_blipBufRight, time);
	}
	for(uint32_t k = 0; k < _captureStreams; k++) {
		blip_end_frame(_streamBlip[k], time);
	}

	//Reset everything. Only the dirtied columns [0, time] were written this window (AddDelta stamps <= the
	//flush cycle `time`), so clear just that span per row — with _channelOutput now sized at MaxCycleLength,
	//a full sizeof() memset every flush would cost the max regardless of the (smaller) runtime window.
	_timestamps.clear();
	uint32_t clearLen = (time < MaxCycleLength) ? (time + 1) : MaxCycleLength;
	for(uint32_t j = 0; j < MaxChannelCount; j++) {
		memset(_channelOutput[j], 0, clearLen * sizeof(int16_t));
	}
}

void NesSoundMixer::CaptureStreams()
{
	// Each pin blip carries the same 96k frame count as the mix blip (same clock rate + blip_end_frame
	// time), and its own Hermite resampler (96k→host, fresh phase, dynamic-rate 1.0 headless) emits the
	// same host count as the mix ring — so the pins stay sample-aligned with the mix and re-sum to it.
	// Read the blip mono into the even lanes, mirror to the odd (the stereo HermiteResampler wants
	// interleaved L/R), resample, then keep the left lane of the host output.
	_streamScratch.resize((size_t)NesSoundMixer::MaxSamplesPerFrame * 2);
	_streamOutScratch.resize((size_t)NesSoundMixer::MaxSamplesPerFrame * 2);
	for(uint32_t k = 0; k < _captureStreams; k++) {
		int n96 = blip_read_samples(_streamBlip[k], _streamScratch.data(), NesSoundMixer::MaxSamplesPerFrame, 1);
		for(int i = 0; i < n96; i++) {
			_streamScratch[i * 2 + 1] = _streamScratch[i * 2];
		}
		uint32_t nHost = _streamResampler[k].Resample<false>(_streamScratch.data(), (uint32_t)n96, _streamOutScratch.data(), NesSoundMixer::MaxSamplesPerFrame);
		size_t base = _streamBuf[k].size();
		_streamBuf[k].resize(base + nHost);
		for(uint32_t i = 0; i < nHost; i++) {
			_streamBuf[k][base + i] = _streamOutScratch[i * 2];
		}
	}
}

void NesSoundMixer::SetChannelCapture(uint32_t mode, uint32_t hostRate)
{
	// Stream count per mode: 1 pins → 3, 2 pins+ref → 4, 3 individual-mono → 5, else off.
	const uint32_t want = mode == 1 ? 3u : mode == 2 ? 4u : mode == 3 ? 5u : 0u;
	_captureMode = want > 0 ? mode : 0u;
	// Free any streams no longer wanted (e.g. re-arming with fewer streams).
	for(uint32_t k = want; k < MaxCaptureStreams; k++) {
		if(_streamBlip[k]) {
			blip_delete(_streamBlip[k]);
			_streamBlip[k] = nullptr;
		}
		_streamBuf[k].clear();
	}
	_captureStreams = want;
	if(want > 0) {
		_captureHostRate = hostRate;
		for(uint32_t k = 0; k < want; k++) {
			if(!_streamBlip[k]) {
				_streamBlip[k] = blip_new(NesSoundMixer::MaxSamplesPerFrame);
			}
			blip_clear(_streamBlip[k]);
			if(_clockRate > 0) {
				blip_set_rates(_streamBlip[k], _clockRate, _sampleRate);
			}
			_streamResampler[k].Reset();
			_streamResampler[k].SetSampleRates(_sampleRate, hostRate);
			_streamPrev[k] = 0;
			_streamBuf[k].clear();
		}
	}
}

uint32_t NesSoundMixer::AvailableCaptureFrames() const
{
	// All captured streams advance in lockstep (identical rates + input counts), so stream 0's length is
	// the available frame count for every stream.
	return _captureStreams > 0 ? (uint32_t)_streamBuf[0].size() : 0;
}

uint32_t NesSoundMixer::DrainChannel(uint32_t stream, float* dest, uint32_t frameCount)
{
	if(stream >= _captureStreams) {
		return 0;
	}
	vector<int16_t>& buf = _streamBuf[stream];
	uint32_t take = std::min((uint32_t)buf.size(), frameCount);
	constexpr float scale = 1.0f / 32768.0f;
	for(uint32_t i = 0; i < take; i++) {
		dest[i] = buf[i] * scale;
	}
	buf.erase(buf.begin(), buf.begin() + take);
	return take;
}

