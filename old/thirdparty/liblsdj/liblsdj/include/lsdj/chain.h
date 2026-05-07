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

#ifndef LSDJ_CHAIN_H
#define LSDJ_CHAIN_H

#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

//! The amount of chains in a song
#define LSDJ_CHAIN_COUNT (0x7F)

//! The number of steps in a chain
#define LSDJ_CHAIN_LENGTH (16)

//! The value of "no phrase" at a given step
#define LSDJ_CHAIN_NO_PHRASE (0xFF)

//! Is a chain with a given index in use?
/*! @param song The song that contains the chain (< LSDJ_CHAIN_COUNT)
	@param chain The index of the chain to check for usage */
bool lsdj_chain_is_allocated(const lsdj_song_t* song, uint8_t chain);

//! Change the phrase set to a step in a chain
/*! @param song The song that contains the chain
	@param chain The index of the chain (< LSDJ_CHAIN_COUNT)
	@param step The step within the chain (< LSDJ_CHAIN_LENGTH)
	@param phrase The phrase to set to this step, or LSDJ_CHAIN_NO_PHRASE */
void lsdj_chain_set_phrase(lsdj_song_t* song, uint8_t chain, uint8_t step, uint8_t phrase);

//! Retrieve the phrase set to a step in a chain
/*! @param song The song that contains the chain
	@param chain The index of the chain (< LSDJ_CHAIN_COUNT)
	@param step The step within the chain (< LSDJ_CHAIN_LENGTH)
	@return the phrase index at said step, or LSDJ_CHAIN_NO_PHRASE */
uint8_t lsdj_chain_get_phrase(const lsdj_song_t* song, uint8_t chain, uint8_t step);

//! Change the transposition set to a step in a chain
/*! @param song The song that contains the chain
	@param chain The index of the chain (< LSDJ_CHAIN_COUNT)
	@param step The step within the chain (< LSDJ_CHAIN_LENGTH)
	@param transposition The transposition to set to this step */
void lsdj_chain_set_transposition(lsdj_song_t* song, uint8_t chain, uint8_t step, uint8_t transposition);

//! Retrieve the transposition set to a step in a chain
/*! @param song The song that contains the chain
	@param chain The index of the chain (< LSDJ_CHAIN_COUNT)
	@param step The step within the chain (< LSDJ_CHAIN_LENGTH) */
uint8_t lsdj_chain_get_transposition(const lsdj_song_t* song, uint8_t chain, uint8_t step);
    
#ifdef __cplusplus
}
#endif

#endif
