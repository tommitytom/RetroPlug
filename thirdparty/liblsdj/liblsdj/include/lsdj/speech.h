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

#ifndef LSDJ_SPEECH_H_GUARD
#define LSDJ_SPEECH_H_GUARD

#ifdef __cplusplus
extern "C" {
#endif

#include "song.h"

//! The length in bytes of each word name
#define LSDJ_SPEECH_WORD_NAME_LENGTH (4)

//! The amount of words in the speech synthesizer
#define LSDJ_SPEECH_WORD_COUNT (42)

//! The amount of allophones available in each word
#define LSDJ_SPEECH_WORD_LENGTH (16)

//! The value of "no allophone" in a word
#define LSDJ_SPEECH_WORD_NO_ALLOPHONE_VALUE (0x00)

//! Change the name of a word for the speech synth
/*! @param song The song that contains the word names
	@param word The index of the word
	@param word The word, which is LSDJ_SPEECH_WORD_NAME_LENGTH big (non null-terminated) */
void lsdj_speech_set_word_name(lsdj_song_t* song, uint8_t word, const char* name);

//! Retrieve the name of a word for the speech synth
/*! @param song The song that contains the word names
	@param word The index of the word
	@return The word, which is LSDJ_SPEECH_WORD_NAME_LENGTH big (non null-terminated) */
const char* lsdj_speech_get_word_name(const lsdj_song_t* song, uint8_t word);

//! Change the allophone set to a specific index in a word
/*! @param song The song that contains the word names
	@param word The index of the word
	@param index The index of the allophone
	@param value The allophone, or LSDJ_SPEECH_WORD_NO_ALLOPHONE_VALUE */
void lsdj_speech_set_word_allophone(lsdj_song_t* song, uint8_t word, uint8_t allophone, uint8_t value);

//! Retrieve the allophone set to a specific index in a word
/*! @param song The song that contains the word names
	@param word The index of the word
	@param index The index of the allophone
	@return The allophone, or LSDJ_SPEECH_WORD_NO_ALLOPHONE_VALUE*/
uint8_t lsdj_speech_get_word_allophone(const lsdj_song_t* song, uint8_t word, uint8_t allophone);

//! Change the allophone duration set to a specific index in a word
/*! @param song The song that contains the word names
	@param word The index of the word
	@param index The index of the allophone
	@param duration The allophone duration */
void lsdj_speech_set_word_allophone_duration(lsdj_song_t* song, uint8_t word, uint8_t allophone, uint8_t duration);

//! Retrieve the allophone duration set to a specific index in a word
/*! @param song The song that contains the word names
	@param word The index of the word
	@param index The index of the allophone
	@return The allophone duration */
uint8_t lsdj_speech_get_word_allophone_duration(const lsdj_song_t* song, uint8_t word, uint8_t allophone);
    
#ifdef __cplusplus
}
#endif

#endif
