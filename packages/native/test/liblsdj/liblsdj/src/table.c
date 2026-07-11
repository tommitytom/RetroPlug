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

#include "table.h"

#include <assert.h>
#include <stddef.h>

#include "song_offsets.h"

#define ALLOCATION_TABLE_LENGTH (0x32)
#define CONTENT_LENGTH (512)

#define TABLE_SETTER(OFFSET, LENGTH, VALUE) \
const size_t index = table * LSDJ_TABLE_LENGTH + step; \
assert(index <= LENGTH); \
song->bytes[OFFSET + index] = VALUE;

#define TABLE_GETTER(OFFSET, LENGTH) \
const size_t index = table * LSDJ_TABLE_LENGTH + step; \
assert(index <= LENGTH); \
return song->bytes[OFFSET + index];

bool lsdj_table_is_allocated(const lsdj_song_t* song, uint8_t table)
{
    const size_t index = TABLE_ALLOCATION_TABLE_OFFSET + table;
    assert(index <= TABLE_ALLOCATION_TABLE_OFFSET + ALLOCATION_TABLE_LENGTH);

    return song->bytes[index];
}

void lsdj_table_set_envelope(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value)
{
    TABLE_SETTER(TABLE_ENVELOPES_OFFSET, CONTENT_LENGTH, value);
}

uint8_t lsdj_table_get_envelope(const lsdj_song_t* song, uint8_t table, uint8_t step)
{
	TABLE_GETTER(TABLE_ENVELOPES_OFFSET, CONTENT_LENGTH);
}

void lsdj_table_set_transposition(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value)
{
	TABLE_SETTER(TABLE_TRANSPOSITION_OFFSET, CONTENT_LENGTH, value);
}

uint8_t lsdj_table_get_transposition(const lsdj_song_t* song, uint8_t table, uint8_t step)
{
	TABLE_GETTER(TABLE_TRANSPOSITION_OFFSET, CONTENT_LENGTH);
}

bool lsdj_table_set_command1(lsdj_song_t* song, uint8_t table, uint8_t step, lsdj_command_t command)
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
            
        const size_t index = table * LSDJ_TABLE_LENGTH + step;
        assert(index <= CONTENT_LENGTH);
        song->bytes[TABLE_COMMAND1_OFFSET + index] = byte;
    } else {
        if (command == LSDJ_COMMAND_B)
            return false;
        
        TABLE_SETTER(TABLE_COMMAND1_OFFSET, CONTENT_LENGTH, (uint8_t)command)
    }
    
    return true;
}

lsdj_command_t lsdj_table_get_command1(const lsdj_song_t* song, uint8_t table, uint8_t step)
{
	if (lsdj_song_get_format_version(song) >= 8)
    {
        const size_t index = table * LSDJ_TABLE_LENGTH + step;
        assert(index <= TABLE_COMMAND1_OFFSET);
        
        const uint8_t byte = song->bytes[TABLE_COMMAND1_OFFSET + index];
        
        if (byte > 1)
            return (lsdj_command_t)(byte - 1);
        else if (byte == 1)
            return LSDJ_COMMAND_B;
        else
            return (lsdj_command_t)byte;
    } else {
        TABLE_GETTER(TABLE_COMMAND1_OFFSET, CONTENT_LENGTH)
    }
}

void lsdj_table_set_command1_value(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value)
{
	TABLE_SETTER(TABLE_COMMAND1_VALUE_OFFSET, CONTENT_LENGTH, value);
}

uint8_t lsdj_table_get_command1_value(const lsdj_song_t* song, uint8_t table, uint8_t step)
{
	TABLE_GETTER(TABLE_COMMAND1_VALUE_OFFSET, CONTENT_LENGTH);
}

bool lsdj_table_set_command2(lsdj_song_t* song, uint8_t table, uint8_t step, lsdj_command_t command)
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
            
        const size_t index = table * LSDJ_TABLE_LENGTH + step;
        assert(index <= CONTENT_LENGTH);
        song->bytes[TABLE_COMMAND2_OFFSET + index] = byte;
    } else {
        if (command == LSDJ_COMMAND_B)
            return false;
        
        TABLE_SETTER(TABLE_COMMAND2_OFFSET, CONTENT_LENGTH, (uint8_t)command)
    }
    
    return true;
}

lsdj_command_t lsdj_table_get_command2(const lsdj_song_t* song, uint8_t table, uint8_t step)
{
    if (lsdj_song_get_format_version(song) >= 8)
    {
        const size_t index = table * LSDJ_TABLE_LENGTH + step;
        assert(index <= TABLE_COMMAND2_OFFSET);
        
        const uint8_t byte = song->bytes[TABLE_COMMAND2_OFFSET + index];
        
        if (byte > 1)
            return (lsdj_command_t)(byte - 1);
        else if (byte == 1)
            return LSDJ_COMMAND_B;
        else
            return (lsdj_command_t)byte;
    } else {
        TABLE_GETTER(TABLE_COMMAND2_OFFSET, CONTENT_LENGTH)
    }
}

void lsdj_table_set_command2_value(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value)
{
	TABLE_SETTER(TABLE_COMMAND2_VALUE_OFFSET, CONTENT_LENGTH, value);
}

uint8_t lsdj_table_get_command2_value(const lsdj_song_t* song, uint8_t table, uint8_t step)
{
	TABLE_GETTER(TABLE_COMMAND2_VALUE_OFFSET, CONTENT_LENGTH);
}
