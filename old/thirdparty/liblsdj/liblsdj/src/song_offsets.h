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

#ifndef LSDJ_SONG_OFFSETS_H
#define LSDJ_SONG_OFFSETS_H

// --- Bank 0 --- //

#define PHRASE_NOTES_OFFSET					(0x0000)
#define BOOKMARKS_OFFSET					(0x0FF0)
// Empty									(0x1030)
#define GROOVES_OFFSET						(0x1090)
#define CHAIN_ASSIGNMENTS_OFFSET			(0x1290)
#define TABLE_ENVELOPES_OFFSET				(0x1690)
#define WORDS_OFFSET						(0x1890)
#define WORD_NAMES_OFFSET					(0x1DD0)
#define RB1_OFFSET							(0x1E78)
#define INSTRUMENT_NAMES_OFFSET				(0x1E7A)
// Empty									(0x1FBA)


// --- Bank 1 --- //

// Empty									(0x2000)
#define TABLE_ALLOCATION_TABLE_OFFSET		(0x2020)
#define INSTRUMENT_ALLOCATION_TABLE_OFFSET	(0x2040)
#define CHAIN_PHRASES_OFFSET				(0x2080)
#define CHAIN_TRANSPOSITIONS_OFFSET			(0x2880)
#define INSTRUMENT_PARAMS_OFFSET			(0x3080)
#define TABLE_TRANSPOSITION_OFFSET			(0x3480)
#define TABLE_COMMAND1_OFFSET				(0x3680)
#define TABLE_COMMAND1_VALUE_OFFSET			(0x3880)
#define TABLE_COMMAND2_OFFSET				(0x3A80)
#define TABLE_COMMAND2_VALUE_OFFSET			(0x3C80)
#define RB2_OFFSET							(0x3E80)
#define PHRASE_ALLOCATIONS_OFFSET			(0x3E82)
#define CHAIN_ALLOCATIONS_OFFSET			(0x3EA2)
#define SYNTH_PARAMS_OFFSET					(0x3EB2)
#define WORK_HOURS_OFFSET					(0x3FB2)
#define WORK_MINUTES_OFFSET					(0x3FB3)
#define TEMPO_OFFSET						(0x3FB4)
#define TRANSPOSITION_OFFSET				(0x3FB5)
#define TOTAL_DAYS_OFFSET					(0x3FB6)
#define TOTAL_HOURS_OFFSET					(0x3FB7)
#define TOTAL_MINUTES_OFFSET				(0x3FB8)
#define TOTAL_TIME_CHECKSUM_OFFSET			(0x3FB9)
#define KEY_DELAY_OFFSET					(0x3FBA)
#define KEY_REPEAT_OFFSET					(0x3FBB)
#define FONT_OFFSET							(0x3FBC)
#define SYNC_MODE_OFFSET					(0x3FBD)
#define COLOR_PALETTE_OFFSET				(0x3FBE)
// Empty									(0x3FBF)
#define CLONE_MODE_OFFSET					(0x3FC0)
#define FILE_CHANGED_OFFSET					(0x3FC1)
#define POWER_SAVE_OFFSET					(0x3FC2)
#define PRELISTEN_OFFSET					(0x3FC3)
#define SYNTH_OVERWRITES_OFFSET				(0x3FC4)
// Empty									(0x3FC6)
#define DRUM_MAX_OFFSET						(0x3FD0)
// Empty									(0x3FD1)


// --- Bank 2 --- //

#define PHRASE_COMMANDS_OFFSET				(0x4000)
#define PHRASE_COMMAND_VALUES_OFFSET		(0x4FF0)
// Empty									(0x5FE0)


// --- Bank 3 --- //

#define WAVES_OFFSET						(0x6000)
#define PHRASE_INSTRUMENTS_OFFSET			(0x7000)
#define RB3_OFFSET							(0x7FF0)
// Empty 									(0x7FF2)
#define FORMAT_VERSION_OFFSET				(0x7FFF)


#endif