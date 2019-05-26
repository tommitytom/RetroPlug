/*
 
 This file is a part of liblsdj, a C library for managing everything
 that has to do with LSDJ, software for writing music (chiptune) with
 your gameboy. For more information, see:
 
 * https://github.com/stijnfrishert/liblsdj
 * http://www.littlesounddj.com
 
 --------------------------------------------------------------------------------
 
 MIT License
 
 Copyright (c) 2018 - 2019 Stijn Frishert
 
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

#ifndef LSDJ_COMPRESSION_H
#define LSDJ_COMPRESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "error.h"
#include "vio.h"

// Decompress blocks to a song buffer
void lsdj_decompress(lsdj_vio_t* rvio, lsdj_vio_t* wvio, long* firstBlockOffset, size_t blockSize, lsdj_error_t** error);
void lsdj_decompress_from_file(const char* path, lsdj_vio_t* wvio, long* firstBlockOffset, size_t blockSize, lsdj_error_t** error);

// Compress a song buffer to a set of blocks
/*! Returns the amount of blocks written */
unsigned int lsdj_compress(const unsigned char* data, unsigned int blockSize, unsigned char startBlock, unsigned int blockCount, lsdj_vio_t* wvio, lsdj_error_t** error);
unsigned int lsdj_compress_to_file(const unsigned char* data, unsigned int blockSize, unsigned char startBlock, unsigned int blockCount, const char* path, lsdj_error_t** error);
    
#ifdef __cplusplus
}
#endif

#endif
