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

#ifndef LSDJ_WAVE_H
#define LSDJ_WAVE_H

#include <stdbool.h>

#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

//! The amount of waves in a song
#define LSDJ_WAVE_COUNT (0xFF)

	//! The amount of waves per synth
#define LSDJ_WAVE_PER_SYNTH_COUNT (0xF)

//! The number of bytes a wave takes
/*! Do note that each step is represented by 4 bits, so the step count is twice this */
#define LSDJ_WAVE_BYTE_COUNT (16)

//! The number of steps in a wave
/*! Do note that each step is represented by 4 bits, so the byte count is half this */
#define LSDJ_WAVE_STEP_COUNT (LSDJ_WAVE_BYTE_LENGTH * 2)

//! Change the bytes that represent a wave
/*! @param song The song that contains the wave
	@param wave The index of the wave (< LSDJ_WAVE_COUNT)
	@param data Pointer to the data to copy over in the wave, should be of size LSDJ_WAVE_BYTE_COUNT */
void lsdj_wave_set_bytes(lsdj_song_t* song, uint8_t wave, const uint8_t* data);

//! Clear a wave so that it contains a silent table
/*! @param song The song that contains the wave
	@param wave The index of the wave (< LSDJ_WAVE_COUNT) */
void lsdj_wave_set_silent(lsdj_song_t* song, uint8_t wave);

//! Retrieve a pointer to the bytes that make up a wave
/*! @param song The song that contains the wave
    @param wave The index of the wave (< LSDJ_WAVE_COUNT)
    This funtion returns mutable memory. See lsdj_wave_get_bytes_const() for const wave retrieval
    @return A pointer to the byte array describing the wave (LSDJ_WAVE_BYTE_COUNT in size) */
uint8_t* lsdj_wave_get_bytes(lsdj_song_t* song, uint8_t wave);

//! Retrieve a pointer to the bytes that make up a wave
/*! @param song The song that contains the wave
	@param wave The index of the wave (< LSDJ_WAVE_COUNT)
    This funtion returns const memory. See lsdj_wave_get_bytes() for mutable wave retrieval
	@return A pointer to the byte array describing the wave (LSDJ_WAVE_BYTE_COUNT in size) */
const uint8_t* lsdj_wave_get_bytes_const(const lsdj_song_t* song, uint8_t wave);

//! Is a given wave the default wave?
/*! @return Whether the wave is the default one */
bool lsdj_wave_is_default(const lsdj_song_t* song, uint8_t wave);

    
#ifdef __cplusplus
}
#endif

#endif
