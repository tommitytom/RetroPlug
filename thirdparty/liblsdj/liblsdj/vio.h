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

#ifndef LSDJ_VIO_H
#define LSDJ_VIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

// Function pointer types used for virtual I/O
typedef size_t (*lsdj_vio_read_t)(void* ptr, size_t size, void* user_data);
typedef size_t (*lsdj_vio_write_t)(const void* ptr, size_t size, void* user_data);
typedef long (*lsdj_vio_tell_t)(void* user_data);
typedef long (*lsdj_vio_seek_t)(long offset, int whence, void* user_data);

typedef struct
{
    lsdj_vio_read_t read;
    lsdj_vio_write_t write;
    lsdj_vio_tell_t tell;
    lsdj_vio_seek_t seek;
    void* user_data;
} lsdj_vio_t;
    
// Structure used for virtual I/O into memory
/* You probably won't ever have to use this yourself, *read/write_from_memory() functions
   exist for more data structures, wrapping around this system. */
typedef struct
{
    unsigned char* begin;
    unsigned char* cur;
    size_t size;
} lsdj_memory_data_t;
    
// Functions for virtual I/O into files
size_t lsdj_fread(void* ptr, size_t size, void* user_data);
size_t lsdj_fwrite(const void* ptr, size_t size, void* user_data);
long lsdj_ftell(void* user_data);
long lsdj_fseek(long offset, int whence, void* user_data);

// Functions for virtual I/O into memory
size_t lsdj_mread(void* ptr, size_t size, void* user_data);
size_t lsdj_mwrite(const void* ptr, size_t size, void* user_data);
long lsdj_mtell(void* user_data);
long lsdj_mseek(long offset, int whence, void* user_data);
    
#ifdef __cplusplus
}
#endif

#endif
