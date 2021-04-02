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

#ifndef LSDJ_PHRASE_H
#define LSDJ_PHRASE_H

#include "command.h"
#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

//! The amount of phrases in a song
#define LSDJ_PHRASE_COUNT (0xFF)

//! The number of steps in a phrase
#define LSDJ_PHRASE_LENGTH (16)

//! The value of "no note" at a given step
#define LSDJ_PHRASE_NO_NOTE (0)

	//! The value of "no instrument" at a given step
#define LSDJ_PHRASE_NO_INSTRUMENT (0xFF)

//! Is a phrase with a given index in use?
/*! @param song The song that contains the phrase (< LSDJ_PHRASE_COUNT)
	@param phrase The index of the phrase to check for usage */
bool lsdj_phrase_is_allocated(const lsdj_song_t* song, uint8_t phrase);

//! Change the note at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH)
	@param note The note to set to this step, or LSDJ_PHRASE_NO_NOTE */
void lsdj_phrase_set_note(lsdj_song_t* song, uint8_t phrase, uint8_t step, uint8_t note);

//! Retrieve the note at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH)
	@return The note at said step, or LSDJ_PHRASE_NO_NOTE */
uint8_t lsdj_phrase_get_note(const lsdj_song_t* song, uint8_t phrase, uint8_t step);

//! Change the instrument at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH)
	@param instrument The instrument to set to this step or LSDJ_PHRASE_NO_INSTRUMENT */
void lsdj_phrase_set_instrument(lsdj_song_t* song, uint8_t phrase, uint8_t step, uint8_t instrument);

//! Retrieve the instrument at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH)
	@return The instrument at said step, or LSDJ_PHRASE_NO_INSTRUMENT  */
uint8_t lsdj_phrase_get_instrument(const lsdj_song_t* song, uint8_t phrase, uint8_t step);

//! Change the command at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH)
	@param command The command to set to this step
    @note Versions earlier than 7.1.0 (fmt v8 don't support the B command
    @return false if the command is not supported in your LSDj version */
bool lsdj_phrase_set_command(lsdj_song_t* song, uint8_t phrase, uint8_t step, lsdj_command_t command);

//! Retrieve the command at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH) */
lsdj_command_t lsdj_phrase_get_command(const lsdj_song_t* song, uint8_t phrase, uint8_t step);

//! Change the command value at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH)
	@param value The command value to set to this step */
void lsdj_phrase_set_command_value(lsdj_song_t* song, uint8_t phrase, uint8_t step, uint8_t value);

//! Retrieve the command value at a step in a phrase
/*! @param song The song that contains the phrase
	@param phrase The index of the phrase (< LSDJ_PHRASE_COUNT)
	@param step The step within the phrase (< LSDJ_PHRASE_LENGTH) */
uint8_t lsdj_phrase_get_command_value(const lsdj_song_t* song, uint8_t phrase, uint8_t step);
    
#ifdef __cplusplus
}
#endif

#endif
