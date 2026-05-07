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

#include "synth.h"

#include <assert.h>

#include "bytes.h"
#include "song_offsets.h"

void lsdj_synth_set_wave_overwritten(lsdj_song_t* song, uint8_t synth, bool overwritten)
{
	const size_t index = synth / 8;
	assert(index < 2);

	return copy_bits_in_place(&song->bytes[SYNTH_OVERWRITES_OFFSET + index], (uint8_t)(synth - (index * 8)), 1, overwritten ? 1 : 0);
}

bool lsdj_synth_is_wave_overwritten(const lsdj_song_t* song, uint8_t synth)
{
	const size_t index = synth / 8;
	assert(index < 2);

	return get_bits(song->bytes[SYNTH_OVERWRITES_OFFSET + index], (uint8_t)(synth - (index * 8)), 1) != 0;
}

void set_synth_byte(lsdj_song_t* song, uint8_t synth, uint8_t byte, uint8_t value)
{
	const size_t index = synth * LSDJ_SYNTH_BYTE_COUNT + byte;
	assert(index < 256);

	song->bytes[SYNTH_PARAMS_OFFSET + index] = value;
}

const uint8_t get_synth_byte(const lsdj_song_t* song, uint8_t synth, uint8_t byte)
{
	const size_t index = synth * LSDJ_SYNTH_BYTE_COUNT + byte;
	assert(index < 256);

	return song->bytes[SYNTH_PARAMS_OFFSET + index];
}

void lsdj_synth_set_waveform(lsdj_song_t* song, uint8_t synth, lsdj_synth_waveform_t waveform)
{
	set_synth_byte(song, synth, 0, (uint8_t)waveform);
}

lsdj_synth_waveform_t lsdj_synth_get_waveform(const lsdj_song_t* song, uint8_t synth)
{
	return (lsdj_synth_waveform_t)get_synth_byte(song, synth, 0);
}

void lsdj_synth_set_filter(lsdj_song_t* song, uint8_t synth, lsdj_synth_filter_t filter)
{
	set_synth_byte(song, synth, 1, (uint8_t)filter);
}

lsdj_synth_filter_t lsdj_synth_get_filter(const lsdj_song_t* song, uint8_t synth)
{
	return (lsdj_synth_filter_t)get_synth_byte(song, synth, 1);
}

void lsdj_synth_set_distortion(lsdj_song_t* song, uint8_t synth, lsdj_synth_distortion_t distortion)
{
	set_synth_byte(song, synth, 3, (uint8_t)distortion);
}

lsdj_synth_distortion_t lsdj_synth_get_distortion(const lsdj_song_t* song, uint8_t synth)
{
	return (lsdj_synth_distortion_t)get_synth_byte(song, synth, 3);
}

void lsdj_synth_set_phase_compression(lsdj_song_t* song, uint8_t synth, lsdj_synth_phase_compression_t compression)
{
	set_synth_byte(song, synth, 4, (uint8_t)compression);
}

lsdj_synth_phase_compression_t lsdj_synth_get_phase_compression(const lsdj_song_t* song, uint8_t synth)
{
	return (lsdj_synth_phase_compression_t)get_synth_byte(song, synth, 4);
}

void lsdj_synth_set_volume_start(lsdj_song_t* song, uint8_t synth, uint8_t volume)
{
	set_synth_byte(song, synth, 5, volume);
}

uint8_t lsdj_synth_get_volume_start(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 5);
}

void lsdj_synth_set_volume_end(lsdj_song_t* song, uint8_t synth, uint8_t volume)
{
	set_synth_byte(song, synth, 9, volume);
}

uint8_t lsdj_synth_get_volume_end(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 9);
}

void lsdj_synth_set_resonance_start(lsdj_song_t* song, uint8_t synth, uint8_t resonance)
{
    if (lsdj_song_get_format_version(song) >= 5)
    {
        const int byte = (get_synth_byte(song, synth, 2) & 0x0F) | ((resonance & 0x0F) << 4);
        set_synth_byte(song, synth, 2, (uint8_t)byte);
    } else {
        set_synth_byte(song, synth, 2, resonance & 0x0F);
    }
}

uint8_t lsdj_synth_get_resonance_start(const lsdj_song_t* song, uint8_t synth)
{
    const uint8_t byte = get_synth_byte(song, synth, 2);
    
    if (lsdj_song_get_format_version(song) >= 5)
        return (byte & 0xF0) >> 4;
    else
        return byte & 0x0F;
}

bool lsdj_synth_set_resonance_end(lsdj_song_t* song, uint8_t synth, uint8_t resonance)
{
	if (lsdj_song_get_format_version(song) >= 5)
	{
		const int byte = (get_synth_byte(song, synth, 2) & 0xF0) | (resonance & 0x0F);
		set_synth_byte(song, synth, 2, (uint8_t)byte);
		return true;
	} else {
		return false;
	}
}

uint8_t lsdj_synth_get_resonance_end(const lsdj_song_t* song, uint8_t synth)
{
    return get_synth_byte(song, synth, 2) & 0x0F;
}

void lsdj_synth_set_cutoff_start(lsdj_song_t* song, uint8_t synth, uint8_t cutoff)
{
	set_synth_byte(song, synth, 6, cutoff);
}

uint8_t lsdj_synth_get_cutoff_start(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 6);
}

void lsdj_synth_set_cutoff_end(lsdj_song_t* song, uint8_t synth, uint8_t cutoff)
{
	set_synth_byte(song, synth, 10, cutoff);
}

uint8_t lsdj_synth_get_cutoff_end(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 10);
}

void lsdj_synth_set_vshift_start(lsdj_song_t* song, uint8_t synth, uint8_t vshift)
{
	set_synth_byte(song, synth, 8, vshift);
}

uint8_t lsdj_synth_get_vshift_start(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 8);
}

void lsdj_synth_set_vshift_end(lsdj_song_t* song, uint8_t synth, uint8_t vshift)
{
	set_synth_byte(song, synth, 12, vshift);
}

uint8_t lsdj_synth_get_vshift_end(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 12);
}

void lsdj_synth_set_limit_start(lsdj_song_t* song, uint8_t synth, uint8_t limit)
{
	const int byte = (get_synth_byte(song, synth, 13) & 0x0F) | (0xF - ((limit & 0x0F) << 4));
	set_synth_byte(song, synth, 13, (uint8_t)byte);
}

uint8_t lsdj_synth_get_limit_start(const lsdj_song_t* song, uint8_t synth)
{
	return 0xF - ((get_synth_byte(song, synth, 13) & 0xF0) >> 4);
}

void lsdj_synth_set_limit_end(lsdj_song_t* song, uint8_t synth, uint8_t limit)
{
	const int byte = (get_synth_byte(song, synth, 13) & 0xF0) | (0xF - (limit & 0x0F));
	set_synth_byte(song, synth, 13, (uint8_t)byte);
}

uint8_t lsdj_synth_get_limit_end(const lsdj_song_t* song, uint8_t synth)
{
	return 0xF - (get_synth_byte(song, synth, 13) & 0x0F);
}

void lsdj_synth_set_phase_start(lsdj_song_t* song, uint8_t synth, uint8_t phase)
{
	set_synth_byte(song, synth, 7, phase);
}

uint8_t lsdj_synth_get_phase_start(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 7);
}

void lsdj_synth_set_phase_end(lsdj_song_t* song, uint8_t synth, uint8_t phase)
{
	set_synth_byte(song, synth, 11, phase);
}

uint8_t lsdj_synth_get_phase_end(const lsdj_song_t* song, uint8_t synth)
{
	return get_synth_byte(song, synth, 11);
}
