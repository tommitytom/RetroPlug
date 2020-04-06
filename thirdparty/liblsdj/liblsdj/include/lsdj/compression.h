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

#ifndef LSDJ_COMPRESSION_H
#define LSDJ_COMPRESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "error.h"
#include "vio.h"

//! The block size used for the (de)crompression algorithm
#define LSDJ_BLOCK_SIZE (0x200)

//! The maximum amount of blocks stored in a sav
#define LSDJ_BLOCK_COUNT (191)

//! Represents the absence of a next block index
#define LSDJ_NO_NEXT_BLOCK_INDEX (0xFFFF)

//! Represents the end of a file "block index"
#define LSDJ_END_OF_FILE_BLOCK_INDEX (0xFF)


// --- Decompression --- //

//! Decompress memory blocks according to the LSDJ compression spec
/*! This algorithm is used to store songs in the project slots in a sav,
    as well as in an .lsdsng file.

    See https://littlesounddj.fandom.com/wiki/File_Management_Structure for more info

    @param rvio The stream to read compressed bytes from
    @param readCounter The amount of bytes read is _added_ to this value
    @param wvio The stream to write decompressed bytes to
    @param writeCounter The amount of bytes written is _added_ to this value
    @param firstBlockPosition The position in rvio at which the very first block starts
    @param followBlockJumps If true, new block positions are read and jumped to relative to firstBlockPosition. Otherwise, the algo just moves to the next block

    @return An error code representing success or failure */
lsdj_error_t lsdj_decompress(lsdj_vio_t* rvio, size_t* readCounter,
                             lsdj_vio_t* wvio, size_t* writeCounter,
                             long firstBlockPosition,
                             bool followBlockJumps);

//! Decompress a single block in the LSDj compression algorithm
/*! Decompresses one block, and returns the id of the next block supposedly

    @note You probably don't need this, but rather lsdj_decompress()

    See https://littlesounddj.fandom.com/wiki/File_Management_Structure for more info

    @param rvio The stream to read compressed bytes from
    @param readCounter The amount of bytes read is _added_ to this value
    @param wvio The stream to write decompressed bytes to
    @param writeCounter The amount of bytes written is _added_ to this value
    @param nextBlockIndex The next block that is supposed to be read after this one (or

    @return An error code representing success or failure */
lsdj_error_t lsdj_decompress_block(lsdj_vio_t* rvio, size_t* readCounter,
                                   lsdj_vio_t* wvio, size_t* writeCounter,
                                   unsigned short* nextBlockIndex);

//! Decompress a single step in the LSDj compression algorithm
/*! Reads one byte and then performs a decompression based on the value

    @note You probably don't need this, but rather lsdj_decompress()

    See https://littlesounddj.fandom.com/wiki/File_Management_Structure for more info

    @param rvio The stream to read compressed bytes from
    @param readCounter The amount of bytes read is _added_ to this value
    @param wvio The stream to write decompressed bytes to
    @param writeCounter The amount of bytes written is _added_ to this value
    @param nextBlockIndex if a block jump was encountered, this is the next block to jump to (or LSDJ_NO_NEXT_BLOCK_INDEX)

    @return An error code representing success or failure */
lsdj_error_t lsdj_decompress_step(lsdj_vio_t* rvio, size_t* readCounter,
                                  lsdj_vio_t* wvio, size_t* writeCounter,
                                  unsigned short* nextBlockIndex);


// --- Compression --- //

//! Compress memory blocks according to the LSDJ compression spec
/*! This algorithm is used to store songs in the project slots in a sav,
    as well as in an .lsdsng file.
 
    This function pads 0's at the end to reach a block boundary.

    @param data The data that will be compressed into blocks
    @param wvio The virtual I/O to be written to. Make sure you have at least about data size / LSDJ_BLOCK_SIZE space
    @param blockOffset The offset to the block jump ids that will be written
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)
 
    @return An error code representing success or failure

    @todo Should the first argument be an lsdj_vio_t* rvio? */
lsdj_error_t lsdj_compress(const uint8_t* data, lsdj_vio_t* wvio, unsigned int blockOffset, size_t* writeCounter);
    
#ifdef __cplusplus
}
#endif

#endif
