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

#include "phrase.h"

#include <assert.h>
#include <stddef.h>

#include "song_offsets.h"

#define PHRASE_SETTER(OFFSET, LENGTH, VALUE) \
const size_t index = phrase * LSDJ_PHRASE_LENGTH + step; \
assert(index <= LENGTH); \
song->bytes[OFFSET + index] = VALUE;

#define PHRASE_GETTER(OFFSET, LENGTH) \
const size_t index = phrase * LSDJ_PHRASE_LENGTH + step; \
assert(index <= LENGTH); \
return song->bytes[OFFSET + index];

bool lsdj_phrase_is_allocated(const lsdj_song_t* song, uint8_t phrase)
{
	const size_t index = phrase / 8;
	assert(index < 32);

	const size_t mask = 1 << (phrase - (index * 8));
    
	return (song->bytes[PHRASE_ALLOCATIONS_OFFSET + index] & mask) != 0;
}

void lsdj_phrase_set_note(lsdj_song_t* song, uint8_t phrase, uint8_t step, uint8_t note)
{
	PHRASE_SETTER(PHRASE_NOTES_OFFSET, 4080, note)
}

uint8_t lsdj_phrase_get_note(const lsdj_song_t* song, uint8_t phrase, uint8_t step)
{
	PHRASE_GETTER(PHRASE_NOTES_OFFSET, 4080)
}

void lsdj_phrase_set_instrument(lsdj_song_t* song, uint8_t phrase, uint8_t step, uint8_t instrument)
{
	PHRASE_SETTER(PHRASE_INSTRUMENTS_OFFSET, 4080, instrument)
}

uint8_t lsdj_phrase_get_instrument(const lsdj_song_t* song, uint8_t phrase, uint8_t step)
{
	PHRASE_GETTER(PHRASE_INSTRUMENTS_OFFSET, 4080)
}

bool lsdj_phrase_set_command(lsdj_song_t* song, uint8_t phrase, uint8_t step, lsdj_command_t command)
{
    if (lsdj_song_get_format_version(song) >= 8)
    {
        uint8_t byte = 0;
        if (command == LSDJ_COMMAND_B)
            byte = 1;
        else if (command > 1)
            byte = ((uint8_t)command) + 1;
        else
            byte = (uint8_t)command;
            
        const size_t index = phrase * LSDJ_PHRASE_LENGTH + step;
        assert(index <= 4080);
        song->bytes[PHRASE_COMMANDS_OFFSET + index] = byte;
    } else {
        if (command == LSDJ_COMMAND_B)
            return false;
        
        PHRASE_SETTER(PHRASE_COMMANDS_OFFSET, 4080, (uint8_t)command)
    }
    
    return true;
}

lsdj_command_t lsdj_phrase_get_command(const lsdj_song_t* song, uint8_t phrase, uint8_t step)
{
    if (lsdj_song_get_format_version(song) >= 8)
    {
        const size_t index = phrase * LSDJ_PHRASE_LENGTH + step;
        assert(index <= 4080);
        
        const uint8_t byte = song->bytes[PHRASE_COMMANDS_OFFSET + index];
        
        if (byte > 1)
            return (lsdj_command_t)(byte - 1);
        else if (byte == 1)
            return LSDJ_COMMAND_B;
        else
            return (lsdj_command_t)byte;
    } else {
        PHRASE_GETTER(PHRASE_COMMANDS_OFFSET, 4080)
    }
}

void lsdj_phrase_set_command_value(lsdj_song_t* song, uint8_t phrase, uint8_t step, uint8_t value)
{
	PHRASE_SETTER(PHRASE_COMMAND_VALUES_OFFSET, 4080, value)
}

uint8_t lsdj_phrase_get_command_value(const lsdj_song_t* song, uint8_t phrase, uint8_t step)
{
	PHRASE_GETTER(PHRASE_COMMAND_VALUES_OFFSET, 4080)
}
