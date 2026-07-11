#include "instrument.h"

void lsdj_instrument_wave_set_volume(lsdj_song_t* song, uint8_t instrument, uint8_t volume)
{
    set_instrument_bits(song, instrument, 1, 0, 8, volume);
}

uint8_t lsdj_instrument_wave_get_volume(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 1, 0, 8);
}

void lsdj_instrument_wave_set_synth(lsdj_song_t* song, uint8_t instrument, uint8_t synth)
{
    if (lsdj_song_get_format_version(song) >= 16)
        set_instrument_bits(song, instrument, 3, 0, 8, (uint8_t)(synth << 4));
    else
        set_instrument_bits(song, instrument, 2, 4, 4, synth);
}

uint8_t lsdj_instrument_wave_get_synth(const lsdj_song_t* song, uint8_t instrument)
{
    if (lsdj_song_get_format_version(song) >= 16)
        return get_instrument_bits(song, instrument, 3, 4, 4);
    else
        return get_instrument_bits(song, instrument, 2, 4, 4);
}

void lsdj_instrument_wave_set_wave(lsdj_song_t* song, uint8_t instrument, uint8_t wave)
{
    set_instrument_bits(song, instrument, 3, 0, 8, wave);
}

uint8_t lsdj_instrument_wave_get_wave(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 3, 0, 8);
}

void lsdj_instrument_wave_set_play_mode(lsdj_song_t* song, uint8_t instrument, lsdj_wave_play_mode_t mode)
{
    if (lsdj_song_get_format_version(song) >= 10)
        set_instrument_bits(song, instrument, 9, 0, 2, ((((uint8_t)mode) + 1) & 0x3));
    else
        set_instrument_bits(song, instrument, 9, 0, 2, (uint8_t)mode);
}

lsdj_wave_play_mode_t lsdj_instrument_wave_get_play_mode(const lsdj_song_t* song, uint8_t instrument)
{
    if (lsdj_song_get_format_version(song) >= 10)
        return (lsdj_wave_play_mode_t)((get_instrument_bits(song, instrument, 9, 0, 2) - 1) & 0x3);
    else
        return (lsdj_wave_play_mode_t)get_instrument_bits(song, instrument, 9, 0, 2);
}

void lsdj_instrument_wave_set_length(lsdj_song_t* song, uint8_t instrument, uint8_t length)
{
	const uint8_t version = lsdj_song_get_format_version(song);
	if (version >= 7)
		set_instrument_bits(song, instrument, 10, 0, 4, 0xF - length);
	else if (version == 6)
		set_instrument_bits(song, instrument, 10, 0, 4, length);
	else
		set_instrument_bits(song, instrument, 14, 4, 4, length);

}

uint8_t lsdj_instrument_wave_get_length(const lsdj_song_t* song, uint8_t instrument)
{
	const uint8_t version = lsdj_song_get_format_version(song);

	if (version >= 7)
		return 0xF - get_instrument_bits(song, instrument, 10, 0, 4);
	else if (version == 6)
		return get_instrument_bits(song, instrument, 10, 0, 4);
	else
		return get_instrument_bits(song, instrument, 14, 4, 4);
}

void lsdj_instrument_wave_set_loop_pos(lsdj_song_t* song, uint8_t instrument, uint8_t pos)
{
    if (lsdj_song_get_format_version(song) >= 9)
        set_instrument_bits(song, instrument, 2, 0, 4, pos & 0xF);
    else
        set_instrument_bits(song, instrument, 2, 0, 4, (pos & 0xF) ^ 0x0F);
}

uint8_t lsdj_instrument_wave_get_loop_pos(const lsdj_song_t* song, uint8_t instrument)
{
    const uint8_t byte = get_instrument_bits(song, instrument, 2, 0, 4);
    
    if (lsdj_song_get_format_version(song) >= 9)
        return byte & 0xF;
    else
        return (byte & 0xF) ^ 0x0F;
}

void lsdj_instrument_wave_set_repeat(lsdj_song_t* song, uint8_t instrument, uint8_t repeat)
{
    if (lsdj_song_get_format_version(song) >= 9)
        set_instrument_bits(song, instrument, 2, 0, 4, (repeat & 0xF) ^ 0xF);
    else
        set_instrument_bits(song, instrument, 2, 0, 4, repeat & 0xF);
}

uint8_t lsdj_instrument_wave_get_repeat(const lsdj_song_t* song, uint8_t instrument)
{
    const uint8_t byte = get_instrument_bits(song, instrument, 2, 0, 4);
    
    if (lsdj_song_get_format_version(song) >= 9)
        return (byte & 0xF) ^ 0xF;
    else
        return byte & 0xF;
}

bool lsdj_instrument_wave_set_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed)
{
    const uint8_t version = lsdj_song_get_format_version(song);
    
    // Speed is stored as starting at 0, but displayed as starting at 1, so subtract 1
    speed -= 1;
    
    if (version >= 7)
        set_instrument_bits(song, instrument, 11, 0, 8, speed - 3);
    else if (version == 6)
        set_instrument_bits(song, instrument, 11, 0, 8, speed);
    else {
    	if (speed > 0x0F)
    		return false;

        set_instrument_bits(song, instrument, 14, 0, 4, speed);
    }

    return true;
}

uint8_t lsdj_instrument_wave_get_speed(const lsdj_song_t* song, uint8_t instrument)
{
	const uint8_t version = lsdj_song_get_format_version(song);
    
    // Read the speed value
    uint8_t speed = 0;
	if (version >= 7)
		speed = get_instrument_bits(song, instrument, 11, 0, 8) + 3;
	else if (version == 6)
		speed = get_instrument_bits(song, instrument, 11, 0, 8);
	else
		speed = get_instrument_bits(song, instrument, 14, 0, 4);
    
    // Speed is stored as starting at 0, but displayed as starting at 1, so add 1
    return speed + 1;
}
