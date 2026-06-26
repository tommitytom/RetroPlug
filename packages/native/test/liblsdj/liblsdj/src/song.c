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

#include "song.h"

#include <assert.h>
#include <stddef.h>

#include "song_offsets.h"

// --- Other macros --- //

#define BOOKMARK_PER_CHANNEL_COUNT (16)
#define NO_BOOKMARK_VALUE (0xFF)


// --- General --- //

bool are_bytes_rb(const uint8_t* data)
{
    return data[0] == 'r' && data[1] == 'b';
}

bool lsdj_song_is_likely_valid(const lsdj_song_t* song)
{
    return are_bytes_rb(&song->bytes[RB1_OFFSET]) ||
           are_bytes_rb(&song->bytes[RB2_OFFSET]) ||
           are_bytes_rb(&song->bytes[RB3_OFFSET]);
}


// --- Song Settings --- //

uint8_t lsdj_song_get_format_version(const lsdj_song_t* song)
{
    return song->bytes[FORMAT_VERSION_OFFSET];
}

bool lsdj_song_has_changed(const lsdj_song_t* song)
{
	return song->bytes[FILE_CHANGED_OFFSET] == 1;
}

bool lsdj_song_set_tempo(lsdj_song_t* song, unsigned short bpm)
{
    if (bpm < 40 || bpm > 295)
        return false;
    
    if (bpm > 255)
        song->bytes[TEMPO_OFFSET] = (uint8_t)(bpm - 256);
    else
        song->bytes[TEMPO_OFFSET] = (uint8_t)bpm;
    
    return true;
}

unsigned short lsdj_song_get_tempo(const lsdj_song_t* song)
{
	const uint8_t byte = song->bytes[TEMPO_OFFSET];
    
    if (byte < 40)
        return byte + 256;
    else
        return byte;
}

void lsdj_song_set_transposition(lsdj_song_t* song, uint8_t semitones)
{
	song->bytes[TRANSPOSITION_OFFSET] = semitones;
}

uint8_t lsdj_song_get_transposition(const lsdj_song_t* song)
{
	return song->bytes[TRANSPOSITION_OFFSET];
}

void lsdj_song_set_sync_mode(lsdj_song_t* song, lsdj_sync_mode_t mode)
{
	song->bytes[SYNC_MODE_OFFSET] = (uint8_t)mode;
}

lsdj_sync_mode_t lsdj_song_get_sync_mode(const lsdj_song_t* song)
{
	return (lsdj_sync_mode_t)song->bytes[SYNC_MODE_OFFSET];
}

void lsdj_song_set_drum_max(lsdj_song_t* song, uint8_t max)
{
	song->bytes[DRUM_MAX_OFFSET] = max;
}

uint8_t lsdj_song_get_drum_max(const lsdj_song_t* song)
{
	return song->bytes[DRUM_MAX_OFFSET];
}


// --- Editor Settings --- //

void lsdj_song_set_clone_mode(lsdj_song_t* song, lsdj_clone_mode_t clone)
{
	song->bytes[CLONE_MODE_OFFSET] = (uint8_t)clone;
}

lsdj_clone_mode_t lsdj_song_get_clone_mode(const lsdj_song_t* song)
{
	return (lsdj_clone_mode_t)song->bytes[CLONE_MODE_OFFSET];
}

void lsdj_song_set_font(lsdj_song_t* song, uint8_t font)
{
	song->bytes[FONT_OFFSET] = font;
}

uint8_t lsdj_song_get_font(const lsdj_song_t* song)
{
	return song->bytes[FONT_OFFSET];
}

void lsdj_song_set_color_palette(lsdj_song_t* song, uint8_t palette)
{
	song->bytes[COLOR_PALETTE_OFFSET] = palette;
}

uint8_t lsdj_song_get_color_palette(const lsdj_song_t* song)
{
	return song->bytes[COLOR_PALETTE_OFFSET];
}

void lsdj_song_set_key_delay(lsdj_song_t* song, uint8_t delay)
{
	song->bytes[KEY_DELAY_OFFSET] = delay;
}

uint8_t lsdj_song_get_key_delay(const lsdj_song_t* song)
{
	return song->bytes[KEY_DELAY_OFFSET];
}

void lsdj_song_set_key_repeat(lsdj_song_t* song, uint8_t repeat)
{
	song->bytes[KEY_REPEAT_OFFSET] = repeat;
}

uint8_t lsdj_song_get_key_repeat(const lsdj_song_t* song)
{
	return song->bytes[KEY_REPEAT_OFFSET];
}

void lsdj_song_set_prelisten(lsdj_song_t* song, bool prelisten)
{
	song->bytes[PRELISTEN_OFFSET] = prelisten ? 1 : 0;
}

bool lsdj_song_get_prelisten(const lsdj_song_t* song)
{
	return song->bytes[PRELISTEN_OFFSET] == 1;
}


// --- Clocks --- //

void lsdj_song_set_total_days(lsdj_song_t* song, uint8_t days)
{
	song->bytes[TOTAL_DAYS_OFFSET] = days;
}

uint8_t lsdj_song_get_total_days(const lsdj_song_t* song)
{
	return song->bytes[TOTAL_DAYS_OFFSET];
}

void lsdj_song_set_total_hours(lsdj_song_t* song, uint8_t hours)
{
	song->bytes[TOTAL_HOURS_OFFSET] = hours;
}

uint8_t lsdj_song_get_total_hours(const lsdj_song_t* song)
{
	return song->bytes[TOTAL_HOURS_OFFSET];
}

void lsdj_song_set_total_minutes(lsdj_song_t* song, uint8_t minutes)
{
	song->bytes[TOTAL_MINUTES_OFFSET] = minutes;
}

uint8_t lsdj_song_get_total_minutes(const lsdj_song_t* song)
{
	return song->bytes[TOTAL_MINUTES_OFFSET];
}

void lsdj_song_set_work_hours(lsdj_song_t* song, uint8_t hours)
{
	song->bytes[WORK_HOURS_OFFSET] = hours;
}

uint8_t lsdj_song_get_work_hours(const lsdj_song_t* song)
{
	return song->bytes[WORK_HOURS_OFFSET];
}

void lsdj_song_set_work_minutes(lsdj_song_t* song, uint8_t minutes)
{
	song->bytes[WORK_MINUTES_OFFSET] = minutes;
}

uint8_t lsdj_song_get_work_minutes(const lsdj_song_t* song)
{
	return song->bytes[WORK_MINUTES_OFFSET];
}


// --- Chains, Phrases //

void lsdj_row_set_chain(lsdj_song_t* song, uint8_t row, lsdj_channel_t channel, uint8_t chain)
{
	const size_t index = CHAIN_ASSIGNMENTS_OFFSET + row * LSDJ_CHANNEL_COUNT + channel;
	assert(index < CHAIN_ASSIGNMENTS_OFFSET + 1024);

    song->bytes[index] = chain;   
}

uint8_t lsdj_row_get_chain(const lsdj_song_t* song, uint8_t row, lsdj_channel_t channel)
{
	const size_t index = CHAIN_ASSIGNMENTS_OFFSET + row * LSDJ_CHANNEL_COUNT + channel;
	assert(index < CHAIN_ASSIGNMENTS_OFFSET + 1024);

    return song->bytes[CHAIN_ASSIGNMENTS_OFFSET + row * LSDJ_CHANNEL_COUNT + channel];
}

bool lsdj_song_set_row_bookmarked(lsdj_song_t* song, uint8_t row, lsdj_channel_t channel, bool bookmarked)
{
	if (lsdj_song_is_row_bookmarked(song, row, channel) == bookmarked)
		return true;

	for (size_t i = 0; i < BOOKMARK_PER_CHANNEL_COUNT; i++)
	{
		const size_t index = channel * BOOKMARK_PER_CHANNEL_COUNT + i;
		assert(index < 64);

		uint8_t* slot = &song->bytes[BOOKMARKS_OFFSET + index];

		if (bookmarked && *slot == NO_BOOKMARK_VALUE)
		{
			*slot = row;
			return true;
		} else if (!bookmarked && *slot == row) {
			*slot = NO_BOOKMARK_VALUE;
			return true;
		}
	}

	return false;
}

bool lsdj_song_is_row_bookmarked(const lsdj_song_t* song, uint8_t row, lsdj_channel_t channel)
{
	for (size_t i = 0; i < BOOKMARK_PER_CHANNEL_COUNT; i++)
	{
		const size_t index = channel * BOOKMARK_PER_CHANNEL_COUNT + i;
		assert(index < 64);

		if (song->bytes[BOOKMARKS_OFFSET + index] == row)
			return true;
	}

	return false;
}

