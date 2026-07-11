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

#ifndef LSDJ_GROOVE_H
#define LSDJ_GROOVE_H

#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

//! The amount of grooves in a song
#define LSDJ_GROOVE_COUNT (0x1F)

//! The number of steps in a groove
#define LSDJ_GROOVE_LENGTH (16)

//! The value of an empty (unused) step
#define LSDJ_GROOVE_NO_VALUE (0)

//! Change the value of a step in a groove
/*! @param song The song to which the groove belongs
	@param groove The index of the groove (< LSDJ_GROOVE_COUNT)
	@param step The index of the step within the groove (< LSDJ_GROOVE_LENGTH)
	@param value The value to set at that step (or LSDJ_GROOVE_NO_VALUE) */
void lsdj_groove_set_step(lsdj_song_t* song, uint8_t groove, uint8_t step, uint8_t value);

//! Retrieve the value of a step in a groove
/*! @param song The song to which the groove belongs
	@param groove The index of the groove (< LSDJ_GROOVE_COUNT)
	@param step The index of the step within the groove (< LSDJ_GROOVE_LENGTH)
	@return The value at said step, or LSDJ_GROOVE_NO_VALUE */
uint8_t lsdj_groove_get_step(const lsdj_song_t* song, uint8_t groove, uint8_t step);
    
#ifdef __cplusplus
}
#endif

#endif
