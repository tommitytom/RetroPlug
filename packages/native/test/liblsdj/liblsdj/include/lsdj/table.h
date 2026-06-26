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

#ifndef LSDJ_TABLE_H
#define LSDJ_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "command.h"
#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LSDJ_TABLE_COUNT (0x20)
#define LSDJ_TABLE_LENGTH (0x10)

//! Returns whether a table is in use
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT */
bool lsdj_table_is_allocated(const lsdj_song_t* song, uint8_t table);

//! Change the envelope value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH
	@param value The value to write into the slot */
void lsdj_table_set_envelope(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value);

//! Return the envelope value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH */
uint8_t lsdj_table_get_envelope(const lsdj_song_t* song, uint8_t table, uint8_t step);

//! Change the transposition value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH
	@param value The value to write into the slot */
void lsdj_table_set_transposition(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value);

//! Return the transposition value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH */
uint8_t lsdj_table_get_transposition(const lsdj_song_t* song, uint8_t table, uint8_t step);

//! Change the command at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH
	@param command The command to write into the slot
    @note Versions earlier than 7.1.0 (fmt v8 don't support the B command
    @return false if the command is not supported in your LSDj version */
bool lsdj_table_set_command1(lsdj_song_t* song, uint8_t table, uint8_t step, lsdj_command_t command);

//! Return the command at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH */
lsdj_command_t lsdj_table_get_command1(const lsdj_song_t* song, uint8_t table, uint8_t step);

//! Change the command value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH
	@param value The value to write into the slot */
void lsdj_table_set_command1_value(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value);

//! Return the command value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH */
uint8_t lsdj_table_get_command1_value(const lsdj_song_t* song, uint8_t table, uint8_t step);

//! Change the command at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH
	@param command The command to write into the slot
    @note Versions earlier than 7.1.0 (fmt v8 don't support the B command
    @return false if the command is not supported in your LSDj version */
bool lsdj_table_set_command2(lsdj_song_t* song, uint8_t table, uint8_t step, lsdj_command_t command);

//! Return the command at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH */
lsdj_command_t lsdj_table_get_command2(const lsdj_song_t* song, uint8_t table, uint8_t step);

//! Change the command value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH
	@param value The value to write into the slot */
void lsdj_table_set_command2_value(lsdj_song_t* song, uint8_t table, uint8_t step, uint8_t value);

//! Return the command value at a slot in a table
/*! @param table The index of the table, at maximum LSDJ_TABLE_COUNT
	@param row The row, at maximum LSDJ_TABLE_LENGTH */
uint8_t lsdj_table_get_command2_value(const lsdj_song_t* song, uint8_t table, uint8_t step);
    
#ifdef __cplusplus
}
#endif

#endif
