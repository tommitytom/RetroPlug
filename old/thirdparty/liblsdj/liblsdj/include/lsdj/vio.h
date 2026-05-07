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

#ifndef LSDJ_VIO_H
#define LSDJ_VIO_H

/*! VIO is the Virtual I/O system LibLSDj uses to read data from *anywhere*
	By filling in the lsdj_vio_t struct, users are able to provide their own
	read/write functions + custom data.

	This way, all functions that need to read or write data can so agnostic
	about where they are actually reading/writing. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

//! The signature of a virtual I/O read function
typedef size_t (*lsdj_vio_read_t)(void* ptr, size_t size, void* userData);

//! The signature of a virtual I/O write function
typedef size_t (*lsdj_vio_write_t)(const void* ptr, size_t size, void* userData);

//! The signature of a virtual I/O tell function
typedef long (*lsdj_vio_tell_t)(void* userData);

//! The signature of a virtual I/O seek function
typedef long (*lsdj_vio_seek_t)(long offset, int whence, void* userData);

typedef struct
{
	//! This function is called to read data
    lsdj_vio_read_t read;

    //! This function is called to write data
    lsdj_vio_write_t write;

    //! This function is called to retrieve the current stream position
    lsdj_vio_tell_t tell;

    //! This function is called to change the current stream position
    lsdj_vio_seek_t seek;

    //! Custom data necessary for the functions to do their work
    void* userData;
} lsdj_vio_t;

//! Read bytes from virtual I/O
/*! @param counter If given, the amount of bytes read is _added_ to this value
    @return Whether the read was fully successful */
bool lsdj_vio_read(lsdj_vio_t* vio, void* ptr, size_t size, size_t* counter);

//! Read a single byte from virtual I/O
/*! @param counter If given, the amount of bytes read is _added_ to this value
    @return Whether the read was fully successful */
bool lsdj_vio_read_byte(lsdj_vio_t* vio, uint8_t* value, size_t* counter);

//! Write bytes to virtual I/O
/*! @param counter If given, the amount of bytes written is _added_ to this value
    @return Whether the write was fully successful */
bool lsdj_vio_write(lsdj_vio_t* vio, const void* ptr, size_t size, size_t* counter);

//! Write a single byte to virtual I/O
/*! @param counter If given, the amount of bytes written is _added_ to this value
    @return Whether the write was fully successful */
bool lsdj_vio_write_byte(lsdj_vio_t* vio, uint8_t value, size_t* counter);

//! Write a series of bytes repeatedly to a stream
/*! @param count The amount of times the contents of ptr should be written
    @param counter If given, the amount of bytes written is _added_ to this value
    @return Whether the write was fully successful */
bool lsdj_vio_write_repeat(lsdj_vio_t* vio, const void* ptr, size_t size, size_t count, size_t* counter);

//! Retrieve the current position in the stream
long lsdj_vio_tell(lsdj_vio_t* vio);

//! Retrieve the current position in the stream
bool lsdj_vio_seek(lsdj_vio_t* vio, long offset, int whence);


// --- File --- //
    
//! Virtual I/O read function for file access
size_t lsdj_fread(void* ptr, size_t size, void* userData);

//! Virtual I/O write function for file access
size_t lsdj_fwrite(const void* ptr, size_t size, void* userData);

//! Virtual I/O tell function for file access
long lsdj_ftell(void* userData);

//! Virtual I/O seek function for file access
long lsdj_fseek(long offset, int whence, void* userData);

//! Convenience function for filling a vio for file access
lsdj_vio_t lsdj_create_file_vio(FILE* file);


// --- Memory --- //

//! Structure used for virtual I/O into memory
/*! You probably won't ever have to use this yourself, *read/write_from_memory() functions
	exist for more data structures, wrapping around this system. */
typedef struct
{
    uint8_t* begin;
    uint8_t* cur;
    size_t size;
} lsdj_memory_access_state_t;

//! Virtual I/O read function for memory access
size_t lsdj_mread(void* ptr, size_t size, void* userData);

//! Virtual I/O write function for memory access
size_t lsdj_mwrite(const void* ptr, size_t size, void* userData);

//! Virtual I/O tell function for memory access
long lsdj_mtell(void* userData);

//! Virtual I/O seek function for memory access
long lsdj_mseek(long offset, int whence, void* userData);

//! Convenience function for filling a vio for memory access
lsdj_vio_t lsdj_create_memory_vio(lsdj_memory_access_state_t* state);
    
#ifdef __cplusplus
}
#endif

#endif
