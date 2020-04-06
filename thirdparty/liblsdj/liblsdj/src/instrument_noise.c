#include "instrument.h"

void lsdj_instrument_noise_set_length(lsdj_song_t* song, uint8_t instrument, uint8_t length)
{
	const bool unlimited = length == LSDJ_INSTRUMENT_NOISE_LENGTH_INFINITE;
	set_instrument_bits(song, instrument, 3, 6, 1, unlimited ? 0 : 1);

	if (unlimited)
		set_instrument_bits(song, instrument, 3, 0, 5, length);
}

uint8_t lsdj_instrument_noise_get_length(const lsdj_song_t* song, uint8_t instrument)
{
	if (get_instrument_bits(song, instrument, 3, 6, 1) == 0)
		return LSDJ_INSTRUMENT_NOISE_LENGTH_INFINITE;
	else
		return (~get_instrument_bits(song, instrument, 3, 0, 5)) & 0x3F;
}

void lsdj_instrument_noise_set_shape(lsdj_song_t* song, uint8_t instrument, uint8_t shape)
{
	set_instrument_bits(song, instrument, 4, 0, 8, shape);
}

uint8_t lsdj_instrument_noise_get_shape(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 4, 0, 8);
}

void lsdj_instrument_noise_set_stability(lsdj_song_t* song, uint8_t instrument, lsdj_noise_stability_t stability)
{
	set_instrument_bits(song, instrument, 2, 0, 1, (uint8_t)stability);
}

lsdj_noise_stability_t lsdj_instrument_noise_get_stability(const lsdj_song_t* song, uint8_t instrument)
{
	return (lsdj_noise_stability_t)get_instrument_bits(song, instrument, 2, 0, 1);
}