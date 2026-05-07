#include "instrument.h"

void lsdj_instrument_kit_set_volume(lsdj_song_t* song, uint8_t instrument, uint8_t volume)
{
    lsdj_instrument_wave_set_volume(song, instrument, volume);
}

uint8_t lsdj_instrument_kit_get_volume(const lsdj_song_t* song, uint8_t instrument)
{
    return lsdj_instrument_wave_get_volume(song, instrument);
}

void lsdj_instrument_kit_set_pitch(lsdj_song_t* song, uint8_t instrument, uint8_t pitch)
{
	set_instrument_bits(song, instrument, 8, 0, 8, pitch);
}

uint8_t lsdj_instrument_kit_get_pitch(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 8, 0, 8);
}

void lsdj_instrument_kit_set_half_speed(lsdj_song_t* song, uint8_t instrument, bool halfSpeed)
{
	set_instrument_bits(song, instrument, 2, 6, 1, halfSpeed ? 1 : 0);
}

bool lsdj_instrument_kit_get_half_speed(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 2, 6, 1) == 1;
}

void lsdj_instrument_kit_set_distortion_mode(lsdj_song_t* song, uint8_t instrument, lsdj_kit_distortion_mode_t distortion)
{
	set_instrument_bits(song, instrument, 10, 0, 2, (uint8_t)distortion);
}

lsdj_kit_distortion_mode_t lsdj_instrument_kit_get_distortion_mode(const lsdj_song_t* song, uint8_t instrument)
{
	return (lsdj_kit_distortion_mode_t)get_instrument_bits(song, instrument, 10, 0, 2);
}

void lsdj_instrument_kit_set_kit1(lsdj_song_t* song, uint8_t instrument, uint8_t kit)
{
	set_instrument_bits(song, instrument, 2, 0, 5, kit);
}

uint8_t lsdj_instrument_kit_get_kit1(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 2, 0, 5);
}

void lsdj_instrument_kit_set_kit2(lsdj_song_t* song, uint8_t instrument, uint8_t kit)
{
	set_instrument_bits(song, instrument, 9, 0, 5, kit);
}

uint8_t lsdj_instrument_kit_get_kit2(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 9, 0, 5);
}

void lsdj_instrument_kit_set_offset1(lsdj_song_t* song, uint8_t instrument, uint8_t offset)
{
	set_instrument_bits(song, instrument, 12, 0, 8, offset);
}

uint8_t lsdj_instrument_kit_get_offset1(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 12, 0, 8);
}

void lsdj_instrument_kit_set_offset2(lsdj_song_t* song, uint8_t instrument, uint8_t offset)
{
	set_instrument_bits(song, instrument, 13, 0, 8, offset);
}

uint8_t lsdj_instrument_kit_get_offset2(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 13, 0, 8);
}

void lsdj_instrument_kit_set_length1(lsdj_song_t* song, uint8_t instrument, uint8_t length)
{
	set_instrument_bits(song, instrument, 3, 0, 8, length);
}

uint8_t lsdj_instrument_kit_get_length1(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 3, 0, 8);
}

void lsdj_instrument_kit_set_length2(lsdj_song_t* song, uint8_t instrument, uint8_t length)
{
	set_instrument_bits(song, instrument, 13, 0, 8, length);
}

uint8_t lsdj_instrument_kit_get_length2(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 13, 0, 8);
}

void lsdj_instrument_kit_set_loop1(lsdj_song_t* song, uint8_t instrument, lsdj_kit_loop_mode_t loop)
{
	set_instrument_bits(song, instrument, 2, 7, 1, loop == LSDJ_INSTRUMENT_KIT_LOOP_ATTACK ? 1 : 0);
	set_instrument_bits(song, instrument, 5, 6, 1, loop == LSDJ_INSTRUMENT_KIT_LOOP_ON ? 1 : 0);
}

lsdj_kit_loop_mode_t lsdj_instrument_kit_get_loop1(const lsdj_song_t* song, uint8_t instrument)
{
	if (get_instrument_bits(song, instrument, 2, 7, 1) == 1)
		return LSDJ_INSTRUMENT_KIT_LOOP_ATTACK;
	else
		return get_instrument_bits(song, instrument, 5, 6, 1);
}

void lsdj_instrument_kit_set_loop2(lsdj_song_t* song, uint8_t instrument, lsdj_kit_loop_mode_t loop)
{
	set_instrument_bits(song, instrument, 9, 7, 1, loop == LSDJ_INSTRUMENT_KIT_LOOP_ATTACK ? 1 : 0);
	set_instrument_bits(song, instrument, 5, 5, 1, loop == LSDJ_INSTRUMENT_KIT_LOOP_ON ? 1 : 0);
}

lsdj_kit_loop_mode_t lsdj_instrument_kit_get_loop2(const lsdj_song_t* song, uint8_t instrument)
{
	if (get_instrument_bits(song, instrument, 9, 7, 1) == 1)
		return LSDJ_INSTRUMENT_KIT_LOOP_ATTACK;
	else
		return get_instrument_bits(song, instrument, 5, 5, 1);
}
