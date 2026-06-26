/*
 
 This file is a part of liblsdj, a C library for managing everything
 that has to do with LSDJ, software for writing music (chiptune) with
 your gameboy. For more information, see:
 
 * https://github.com/stijnfrishert/liblsdj
 * http://www.littlesounddj.com
 
 --------------------------------------------------------------------------------
 
 MIT License
 
 Copyright (c) 2018 - 2020 Stijn Frishert
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 
 */

#include "instrument.h"

#include <assert.h>
#include <string.h>

#include "bytes.h"
#include "song_offsets.h"

bool lsdj_instrument_is_allocated(const lsdj_song_t* song, uint8_t instrument)
{
    const size_t index = INSTRUMENT_ALLOCATION_TABLE_OFFSET + (size_t)instrument;
    assert(index <= INSTRUMENT_ALLOCATION_TABLE_OFFSET + 64);

    return song->bytes[index];
}

void lsdj_instrument_set_name(lsdj_song_t* song, uint8_t instrument, const char* name)
{
    const size_t index = INSTRUMENT_NAMES_OFFSET + (size_t)instrument * LSDJ_INSTRUMENT_NAME_LENGTH;
    assert(index < INSTRUMENT_NAMES_OFFSET + 320);

    char* dst = (char*)(&song->bytes[index]);
    strncpy(dst, name, LSDJ_INSTRUMENT_NAME_LENGTH);
    sanitize_name(dst, LSDJ_INSTRUMENT_NAME_LENGTH);
}

const char* lsdj_instrument_get_name(const lsdj_song_t* song, uint8_t instrument)
{
    const size_t index = INSTRUMENT_NAMES_OFFSET + (size_t)instrument * LSDJ_INSTRUMENT_NAME_LENGTH;
    assert(index < INSTRUMENT_NAMES_OFFSET + 320);

    return (const char*)(&song->bytes[index]);
}

void set_instrument_bits(lsdj_song_t* song, uint8_t instrument, uint8_t byte, uint8_t position, uint8_t count, uint8_t value)
{
	const size_t index = (size_t)instrument * LSDJ_INSTRUMENT_BYTE_COUNT + byte;
	assert(index < 1024);

	copy_bits_in_place(
		&song->bytes[INSTRUMENT_PARAMS_OFFSET + index],
		position, count, value
	);
}

uint8_t get_instrument_bits(const lsdj_song_t* song, uint8_t instrument, uint8_t byte, uint8_t position, uint8_t count)
{
	const size_t index = (size_t)instrument * LSDJ_INSTRUMENT_BYTE_COUNT + byte;
	assert(index < 1024);
    const size_t offset = INSTRUMENT_PARAMS_OFFSET + index;

    const uint8_t* ptr = song->bytes + offset;
 	return (uint8_t)(get_bits(*ptr, position, count) >> position);
}

void lsdj_instrument_set_type(lsdj_song_t* song, uint8_t instrument, lsdj_instrument_type_t type)
{
	set_instrument_bits(song, instrument, 0, 0, 8, (uint8_t)type);
}

lsdj_instrument_type_t lsdj_instrument_get_type(const lsdj_song_t* song, uint8_t instrument)
{
	return (uint8_t)get_instrument_bits(song, instrument, 0, 0, 8);
}

void lsdj_instrument_set_envelope(lsdj_song_t* song, uint8_t instrument, uint8_t envelope)
{
    set_instrument_bits(song, instrument, 1, 0, 8, envelope);
}

uint8_t lsdj_instrument_get_envelope(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 1, 0, 8);
}

void lsdj_instrument_adsr_set_initial_level(lsdj_song_t* song, uint8_t instrument, uint8_t level)
{
    set_instrument_bits(song, instrument, 1, 4, 4, level);
}

uint8_t lsdj_instrument_adsr_get_initial_level(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 1, 4, 4);
}

void lsdj_instrument_adsr_set_attack_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed)
{
    set_instrument_bits(song, instrument, 1, 0, 3, speed);
}

uint8_t lsdj_instrument_adsr_get_attack_speed(const lsdj_song_t* song, uint8_t instrument)
{
    if (lsdj_song_get_format_version(song) >= 13)
        return get_instrument_bits(song, instrument, 1, 0, 4);
    else
        return get_instrument_bits(song, instrument, 1, 0, 3);
}

void lsdj_instrument_adsr_set_attack_level(lsdj_song_t* song, uint8_t instrument, uint8_t level)
{
    set_instrument_bits(song, instrument, 9, 4, 4, level);
}

uint8_t lsdj_instrument_adsr_get_attack_level(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 9, 4, 4);
}

void lsdj_instrument_adsr_set_decay_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed)
{
    set_instrument_bits(song, instrument, 9, 0, 3, speed);
}

uint8_t lsdj_instrument_adsr_get_decay_speed(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 9, 0, 3);
}

void lsdj_instrument_adsr_set_sustain_level(lsdj_song_t* song, uint8_t instrument, uint8_t level)
{
    set_instrument_bits(song, instrument, 0xA, 4, 4, level);
}

uint8_t lsdj_instrument_adsr_get_sustain_level(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 0xA, 4, 4);
}

void lsdj_instrument_adsr_set_release_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed)
{
    set_instrument_bits(song, instrument, 0xA, 0, 3, speed);
}

uint8_t lsdj_instrument_adsr_get_release_speed(const lsdj_song_t* song, uint8_t instrument)
{
    return get_instrument_bits(song, instrument, 0xA, 0, 3);
}






void lsdj_instrument_set_panning(lsdj_song_t* song, uint8_t instrument, lsdj_panning_t panning)
{
	set_instrument_bits(song, instrument, 7, 0, 2, (uint8_t)panning);
}

lsdj_panning_t lsdj_instrument_get_panning(const lsdj_song_t* song, uint8_t instrument)
{
	return (lsdj_panning_t)get_instrument_bits(song, instrument, 7, 0, 2);
}

void lsdj_instrument_set_transpose(lsdj_song_t* song, uint8_t instrument, bool transpose)
{
	set_instrument_bits(song, instrument, 5, 5, 1, transpose ? 0 : 1);
}

bool lsdj_instrument_get_transpose(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 5, 5, 1) == 0;
}

void lsdj_instrument_enable_table(lsdj_song_t* song, uint8_t instrument, bool enabled)
{
	set_instrument_bits(song, instrument, 6, 5, 1, enabled ? 1 : 0);
}

bool lsdj_instrument_is_table_enabled(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 6, 5, 1) == 1;
}

void lsdj_instrument_set_table(lsdj_song_t* song, uint8_t instrument, uint8_t table)
{
	set_instrument_bits(song, instrument, 6, 0, 4, table);
}

uint8_t lsdj_instrument_get_table(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 6, 0, 4);
}

void lsdj_instrument_set_table_mode(lsdj_song_t* song, uint8_t instrument, lsdj_instrument_table_mode mode)
{
	set_instrument_bits(song, instrument, 5, 3, 1, mode == LSDJ_INSTRUMENT_TABLE_STEP ? 1 : 0);
}

lsdj_instrument_table_mode lsdj_instrument_get_table_mode(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 5, 3, 1) == 1 ? LSDJ_INSTRUMENT_TABLE_STEP : LSDJ_INSTRUMENT_TABLE_PLAY;
}

void lsdj_instrument_set_vibrato_direction(lsdj_song_t* song, uint8_t instrument, lsdj_vibrato_direction_t direction)
{
	set_instrument_bits(song, instrument, 5, 0, 1, (uint8_t)direction);
}

lsdj_vibrato_direction_t lsdj_instrument_get_vibrato_direction(const lsdj_song_t* song, uint8_t instrument)
{
	return (lsdj_vibrato_direction_t)get_instrument_bits(song, instrument, 5, 0, 1);
}

bool lsdj_instrument_set_vibrato_shape_and_plv_speed(lsdj_song_t* song, uint8_t instrument, lsdj_vibrato_shape_t shape, lsdj_plv_speed_t speed)
{
	if (lsdj_song_get_format_version(song) >= 4)
	{
		set_instrument_bits(song, instrument, 5, 1, 2, (uint8_t)shape);

		set_instrument_bits(song, instrument, 5, 7, 1, speed == LSDJ_INSTRUMENT_PLV_STEP ? 1 : 0);
		set_instrument_bits(song, instrument, 5, 4, 1, speed == LSDJ_INSTRUMENT_PLV_TICK ? 1 : 0);

		return true;
	} else {

		switch (speed)
		{
		case LSDJ_INSTRUMENT_PLV_FAST:
			if (shape == LSDJ_INSTRUMENT_VIBRATO_TRIANGLE)
			{
				set_instrument_bits(song, instrument, 5, 1, 2, 0x0);
				return true;
			} else {
				return false;
			}

		case LSDJ_INSTRUMENT_PLV_TICK:
			switch (shape)
			{
				case LSDJ_INSTRUMENT_VIBRATO_SAWTOOTH: set_instrument_bits(song, instrument, 5, 1, 2, 0x1); return true;
				case LSDJ_INSTRUMENT_VIBRATO_TRIANGLE: set_instrument_bits(song, instrument, 5, 1, 2, 0x2); return true;
				case LSDJ_INSTRUMENT_VIBRATO_SQUARE: set_instrument_bits(song, instrument, 5, 1, 2, 0x3); return true;
				default: return false;
			}

		default:
			return false;
		}

	}
}

lsdj_vibrato_shape_t lsdj_instrument_get_vibrato_shape(const lsdj_song_t* song, uint8_t instrument)
{
	if (lsdj_song_get_format_version(song) >= 4)
	{
		switch (get_instrument_bits(song, instrument, 5, 1, 2))
        {
            case 0: return LSDJ_INSTRUMENT_VIBRATO_TRIANGLE;
            case 1: return LSDJ_INSTRUMENT_VIBRATO_SAWTOOTH;
            case 2: return LSDJ_INSTRUMENT_VIBRATO_SQUARE;
            default: assert(false); return LSDJ_INSTRUMENT_VIBRATO_TRIANGLE;
        }
    } else {
		switch (get_instrument_bits(song, instrument, 5, 1, 2))
        {
            case 0: return LSDJ_INSTRUMENT_VIBRATO_TRIANGLE;
            case 1: return LSDJ_INSTRUMENT_VIBRATO_SAWTOOTH;
            case 2: return LSDJ_INSTRUMENT_VIBRATO_TRIANGLE;
            case 3: return LSDJ_INSTRUMENT_VIBRATO_SQUARE;
			default: assert(false); return LSDJ_INSTRUMENT_VIBRATO_TRIANGLE;
        }
	}
}

lsdj_plv_speed_t lsdj_instrument_get_plv_speed(const lsdj_song_t* song, uint8_t instrument)
{
	if (lsdj_song_get_format_version(song) >= 4)
	{
		const uint8_t byte = get_instrument_bits(song, instrument, 5, 0, 8);

		if (byte & 0x80)
			return LSDJ_INSTRUMENT_PLV_STEP;
		else if (byte & 0x10)
			return LSDJ_INSTRUMENT_PLV_TICK;
		else
			return LSDJ_INSTRUMENT_PLV_FAST;

	} else {
		switch (get_instrument_bits(song, instrument, 5, 1, 2))
        {
            case 0: return LSDJ_INSTRUMENT_PLV_FAST;

            case 1:
            case 2:
            case 3: return LSDJ_INSTRUMENT_PLV_TICK;
			default: assert(false); return LSDJ_INSTRUMENT_PLV_FAST;
        }
	}
}

void lsdj_instrument_set_command_rate(lsdj_song_t* song, uint8_t instrument, uint8_t rate)
{
	set_instrument_bits(song, instrument, 8, 0, 8, rate);
}

uint8_t lsdj_instrument_get_command_rate(const lsdj_song_t* song, uint8_t instrument)
{
	return get_instrument_bits(song, instrument, 8, 0, 8);
}
