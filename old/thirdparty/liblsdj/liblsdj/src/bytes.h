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

#ifndef LSDJ_BYTES_H
#define LSDJ_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//! Copy some bits over to another byte
/*! @param byte The byte which will be overwritten
	@param position the position (0-7) where `bits` should be inserted
	@param count the amount of bits (0-7) form `bits` that should be copied over */
uint8_t copy_bits(uint8_t byte, uint8_t position, uint8_t count, uint8_t bits);

//! Copy some bits over to another byte in-place
/*! @param byte The byte which will be overwritten
	@param position the position (0-7) where `bits` should be inserted
	@param count the amount of bits (0-7) form `bits` that should be copied over
	@note position + count should not go above 8 */
void copy_bits_in_place(uint8_t* byte, uint8_t position, uint8_t count, uint8_t bits);

//! Retrieve a bit string from a byte at (position - count)
/*! @param byte The byte to take bits from
	@param position The position (0-7)
	@param count The amount of bits to copy over (0-7)
	@note position + count should not go above 8 */
uint8_t get_bits(uint8_t byte, uint8_t position, uint8_t count);

//! Is a given byte a valid name character for LSDj?
/*! LSDj only supports 0-9, A-Z, x and space */
bool is_valid_name_char(char c);

//! Try to sanitize a full LSDj name
/*! LSDj only supports 0-9, A-Z, x and space
	@return false If the name couldn't be sanitized. Some characters might already have changed */
bool sanitize_name(char* name, size_t size);

#endif
