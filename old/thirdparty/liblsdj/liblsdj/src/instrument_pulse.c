#include "instrument.h"

void lsdj_instrument_pulse_set_pulse_width(lsdj_song_t* song, uint8_t instrument, lsdj_instrument_pulse_width_t pulseWidth)
{
	set_instrument_bits(song, instrument, 7, 6, 2, (uint8_t)pulseWidth);
}

lsdj_instrument_pulse_width_t lsdj_instrument_pulse_get_pulse_width(const lsdj_song_t* song, uint8_t instrument)
{
	return (lsdj_instrument_pulse_width_t)get_instrument_bits(song, instrument, 7, 6, 2);
}

void lsdj_instrument_pulse_set_length(lsdj_song_t* song, uint8_t instrument, uint8_t length)
{
	const bool unlimited = length == LSDJ_INSTRUMENT_PULSE_LENGTH_INFINITE;
	set_instrument_bits(song, instrument, 3, 6, 1, unlimited ? 0 : 1);

	if (unlimited)
		set_instrument_bits(song, instrument, 3, 0, 5, length);
}

uint8_t lsdj_instrument_pulse_get_length(const lsdj_song_t* song, uint8_t instrument)
{
	if (get_instrument_bits(song, instrument, 3, 6, 1) == 0)
		return LSDJ_INSTRUMENT_PULSE_LENGTH_INFINITE;
	else
		return (~get_instrument_bits(song, instrument, 3, 0, 5)) & 0x3F;
}

void lsdj_instrument_pulse_set_sweep(lsdj_song_t* song, uint8_t instrument, uint8_t sweep)
{
	set_instrument_bits(song, instrument, 4, 0, 8, sweep);
}

uint8_t lsdj_instrument_pulse_get_sweep(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 4, 0, 8);
}

void lsdj_instrument_pulse_set_pulse2_tune(lsdj_song_t* song, uint8_t instrument, uint8_t tune)
{
	set_instrument_bits(song, instrument, 2, 0, 8, tune);
}

uint8_t lsdj_instrument_pulse_get_pulse2_tune(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 2, 0, 8);
}

void lsdj_instrument_pulse_set_finetune(lsdj_song_t* song, uint8_t instrument, uint8_t finetune)
{
	set_instrument_bits(song, instrument, 7, 2, 4, finetune);
}

uint8_t lsdj_instrument_pulse_get_finetune(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 7, 2, 4);
}
