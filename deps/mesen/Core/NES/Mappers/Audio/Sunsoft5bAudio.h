#pragma once
#include "pch.h"
#include "NES/NesConsole.h"
#include "NES/APU/NesApu.h"
#include "NES/APU/BaseExpansionAudio.h"
#include "NES/NesConstants.h"
#include "NES/NesExpansionAudioState.h"
#include "Utilities/Serializer.h"

class Sunsoft5bAudio : public BaseExpansionAudio
{
private:
	uint8_t _volumeLut[0x10] = {};
	uint8_t _envVolumeLut[0x20] = {};   //5-bit envelope levels, 1.5 dB/step; [31] == _volumeLut[15]
	uint8_t _currentRegister = 0;
	uint8_t _registers[0x10] = {};
	int16_t _lastOutput = 0;
	int16_t _timer[3] = {};
	uint8_t _toneStep[3] = {};
	bool _processTick = false;

	uint32_t _noiseLfsr = 1;    //17-bit; never let it reach 0 or it locks up
	int32_t _noiseTimer = 0;

	int32_t _envTimer = 0;
	uint8_t _envStep = 0;       //0..31 within the current ramp
	bool _envAttack = false;    //ramp direction: level rises with the step while true
	bool _envHolding = false;   //ramp finished and frozen (Hold, or Continue=0)

	uint16_t GetPeriod(int channel)
	{
		return _registers[channel * 2] | (_registers[channel * 2 + 1] << 8);
	}

	uint16_t GetEnvelopePeriod()
	{
		return _registers[0x0B] | (_registers[0x0C] << 8);
	}

	uint8_t GetNoisePeriod()
	{
		return _registers[6];
	}

	uint8_t GetVolume(int channel)
	{
		return _volumeLut[_registers[8 + channel] & 0x0F];
	}
	
	bool IsEnvelopeEnabled(int channel)
	{
		return (_registers[8 + channel] & 0x10) == 0x10;
	}

	bool IsToneEnabled(int channel)
	{
		return ((_registers[7] >> channel) & 0x01) == 0x00;
	}

	bool IsNoiseEnabled(int channel)
	{
		return ((_registers[7] >> (channel + 3)) & 0x01) == 0x00;
	}
	
	void UpdateChannel(int channel)
	{
		_timer[channel]--;
		if(_timer[channel] <= 0) {
			_timer[channel] = GetPeriod(channel);
			_toneStep[channel] = (_toneStep[channel] + 1) & 0x0F;
		}
	}

	//"Frequency = Clock / (32 * Period)", i.e. one new random bit every 32*period clocks. UpdateChannel runs
	//every 2 clocks (the _processTick divide-by-2), so the LFSR advances every 16*period of those.
	void UpdateNoise()
	{
		_noiseTimer--;
		if(_noiseTimer <= 0) {
			uint8_t period = GetNoisePeriod() & 0x1F;
			_noiseTimer = 16 * (period ? period : 1);
			//17-bit LFSR, taps at bits 16 and 13 (shifted right here, so bits 0 and 3); output is bit 0.
			_noiseLfsr = (_noiseLfsr >> 1) | (((_noiseLfsr ^ (_noiseLfsr >> 3)) & 0x01) << 16);
		}
	}

	//Bit 0 of the LFSR, or a hard 0 when emulating the Everdrive N8 Pro's 5B core instead of the chip.
	//
	//Measured on the N8 (EverMIDI, capture ch5): enabling noise on a sounding channel drops it from
	//-34.09 dBFS to -81.32 (the noise floor), reversibly, at EVERY noise period across the full range. The
	//mixer below explains that exactly - the chip ANDs tone with noise, so a noise signal stuck at 0 gates
	//the channel to silence. A working generator would rasp, never mute. So the N8 produces no noise, and
	//software written against that cartridge (EverMIDI) hears silence where the chip would rasp.
	//
	//What the measurement CANNOT distinguish from outside: a noise generator stuck low versus a mixer that
	//mis-decodes the noise-enable bit and kills tone. Both look identical. Noise-on with TONE DISABLED would
	//separate them, which EverMIDI has no CC for, and mapper 69 is not in krikzz's published FPGA sources.
	bool GetNoiseOutput()
	{
		if(!_console->GetNesConfig().Sunsoft5bNoiseEnabled) {
			return false;
		}
		return (_noiseLfsr & 0x01) != 0;
	}

	//"Frequency = Clock / (16 * Period)" is the STEP rate, and the ramp is a 5-bit series of 32 levels.
	void UpdateEnvelope()
	{
		_envTimer--;
		if(_envTimer > 0) {
			return;
		}
		uint16_t period = GetEnvelopePeriod();
		_envTimer = 8 * (period ? period : 1);   //16*period clocks, at one call per 2 clocks

		if(_envHolding) {
			return;
		}
		_envStep++;
		if(_envStep <= 31) {
			return;
		}

		//A ramp finished. Continue(3) / Attack(2) / Alternate(1) / Hold(0) decide what follows.
		uint8_t shape = _registers[0x0D] & 0x0F;
		if(!(shape & 0x08)) {
			//Continue=0 (shapes 0-7): a single ramp, then silence, whichever way it ran.
			_envHolding = true;
			_envStep = 31;
			_envAttack = false;   //level = 31-31 = 0
			return;
		}
		if(shape & 0x02) {
			_envAttack = !_envAttack;   //Alternate
		}
		if(shape & 0x01) {
			_envHolding = true;         //Hold: freeze at this ramp's final level
			_envStep = 31;
		} else {
			_envStep = 0;
		}
	}

	//5-bit envelope level: rising while attacking, falling otherwise.
	uint8_t GetEnvelopeLevel()
	{
		return _envAttack ? _envStep : (31 - _envStep);
	}

	//The channel's amplitude: the shared envelope when bit 4 is set, else the fixed 4-bit volume.
	uint8_t GetAmplitude(int channel)
	{
		return IsEnvelopeEnabled(channel) ? _envVolumeLut[GetEnvelopeLevel()] : GetVolume(channel);
	}

	void UpdateOutputLevel()
	{
		int16_t summedOutput = 0;
		bool noiseOut = GetNoiseOutput();
		for(int i = 0; i < 3; i++) {
			//"A bit of 0 enables the noise/tone [...] If both bits are 1, the channel outputs a constant
			//signal at the specified volume. If both bits are 0, the result is the logical and of noise
			//and tone." So each half is OR'd with its own disable bit, and the two are AND'ed.
			bool tone = !IsToneEnabled(i) || (_toneStep[i] < 0x08);
			bool noise = !IsNoiseEnabled(i) || noiseOut;
			if(tone && noise) {
				summedOutput += GetAmplitude(i);
			}
		}

		_console->GetApu()->AddExpansionAudioDelta(AudioChannel::Sunsoft5B, summedOutput - _lastOutput);
		_lastOutput = summedOutput;
	}

protected:
	void Serialize(Serializer& s) override
	{
		BaseExpansionAudio::Serialize(s);

		SVArray(_timer, 3);
		SVArray(_registers, 0x10);
		SVArray(_toneStep, 3);
		SV(_currentRegister); SV(_lastOutput); SV(_processTick);
		SV(_noiseLfsr); SV(_noiseTimer);
		SV(_envTimer); SV(_envStep); SV(_envAttack); SV(_envHolding);
	}

	void ClockAudio() override
	{
		if(_processTick) {
			for(int i = 0; i < 3; i++) {
				UpdateChannel(i);
			}
			UpdateNoise();
			UpdateEnvelope();
			UpdateOutputLevel();
		}
		_processTick = !_processTick;
	}

public:
	Sunsoft5bAudio(NesConsole* console) : BaseExpansionAudio(console)
	{
		memset(_timer, 0, sizeof(_timer));
		memset(_registers, 0, sizeof(_registers));
		memset(_toneStep, 0, sizeof(_toneStep));
		_currentRegister = 0;
		_lastOutput = 0;
		_processTick = false;

		double output = 1.0;
		_volumeLut[0] = 0;
		for(int i = 1; i < 0x10; i++) {
			//+1.5 dB 2x for every 1 step in volume
			output *= 1.1885022274370184377301224648922;
			output *= 1.1885022274370184377301224648922;

			_volumeLut[i] = (uint8_t)output;
		}

		//The envelope's 5-bit ramp is the same curve at 1.5 dB per step, so level 31 lands exactly on the
		//4-bit level 15 (_volumeLut[i] == _envVolumeLut[2*i + 1]).
		double envOutput = 1.0;
		_envVolumeLut[0] = 0;
		for(int i = 1; i < 0x20; i++) {
			_envVolumeLut[i] = (uint8_t)envOutput;
			envOutput *= 1.1885022274370184377301224648922;
		}
	}

	void WriteRegister(uint16_t addr, uint8_t value)
	{
		switch(addr & 0xE000) {
			case 0xC000:
				_currentRegister = value;
				break;

			case 0xE000:
				if(_currentRegister <= 0x0F) {
					_registers[_currentRegister] = value;
					if(_currentRegister == 0x0D) {
						//Writing the shape RESTARTS the envelope - which is how a note-on retriggers it.
						_envStep = 0;
						_envHolding = false;
						_envAttack = (value & 0x04) != 0;
						_envTimer = 0;
					}
				}
				break;
		}
	}

	NesExpansionAudioState GetState()
	{
		NesExpansionAudioState state;
		state.chip = "s5b";
		// The PSG tone timer is clocked at CPU/2 (the _processTick divide-by-2) and
		// the square completes one cycle every 16 toneStep advances, so the output
		// pitch is clk / (32 * period). period==0 -> undefined (report 0, not inf).
		double clk = NesConstants::GetClockRate(NesApu::GetApuRegion(_console));
		for(int ch = 0; ch < 3; ch++) {
			NesExpansionAudioChannel c;
			uint16_t period = GetPeriod(ch);
			// A channel is audible if EITHER generator feeds it (both disable bits set = a constant level).
			c.Enabled     = IsToneEnabled(ch) || IsNoiseEnabled(ch);
			// In envelope mode the 4-bit field is not the volume (bit 4 is the mode flag and the low nibble
			// reads 0), so report the envelope's current level instead of a misleading 0.
			c.Volume      = IsEnvelopeEnabled(ch) ? (uint8_t)(GetEnvelopeLevel() >> 1) : (uint8_t)(_registers[8 + ch] & 0x0F);
			c.OutputLevel = GetAmplitude(ch);            // LUT amplitude, envelope-aware
			c.Period      = period;                      // 12-bit tone period
			c.Frequency   = period ? clk / (32.0 * period) : 0.0;
			state.channels.push_back(c);
		}
		return state;
	}
};