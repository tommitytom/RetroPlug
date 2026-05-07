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

#include "chain.h"

#include <assert.h>
#include <stddef.h>

#include "song_offsets.h"

bool lsdj_chain_is_allocated(const lsdj_song_t* song, uint8_t chain)
{
	const size_t index = chain / 8;
	assert(index < 16);

	const uint32_t mask = 1 << (chain - (index * 8));
    
	return (song->bytes[CHAIN_ALLOCATIONS_OFFSET + index] & mask) != 0;
}

void lsdj_chain_set_phrase(lsdj_song_t* song, uint8_t chain, uint8_t step, uint8_t phrase)
{
	const size_t index = (size_t)chain * LSDJ_CHAIN_LENGTH + step;
	assert(index < 2048);

	song->bytes[CHAIN_PHRASES_OFFSET + index] = phrase;
}

uint8_t lsdj_chain_get_phrase(const lsdj_song_t* song, uint8_t chain, uint8_t step)
{
	const size_t index = (size_t)chain * LSDJ_CHAIN_LENGTH + step;
	assert(index < 2048);

	return song->bytes[CHAIN_PHRASES_OFFSET + index];
}

void lsdj_chain_set_transposition(lsdj_song_t* song, uint8_t chain, uint8_t step, uint8_t transposition)
{
	const size_t index = (size_t)chain * LSDJ_CHAIN_LENGTH + step;
	assert(index < 2048);

	song->bytes[CHAIN_TRANSPOSITIONS_OFFSET + index] = transposition;
}

uint8_t lsdj_chain_get_transposition(const lsdj_song_t* song, uint8_t chain, uint8_t step)
{
	const size_t index = (size_t)chain * LSDJ_CHAIN_LENGTH + step;
	assert(index < 2048);

	return song->bytes[CHAIN_TRANSPOSITIONS_OFFSET + index];
}
