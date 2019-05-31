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

#ifndef LSDJ_PROJECT_H
#define LSDJ_PROJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "error.h"
#include "song.h"
#include "vio.h"

// The length of project names
#define LSDJ_PROJECT_NAME_LENGTH (8)

#define LSDSNG_MAX_SIZE (LSDJ_SONG_DECOMPRESSED_SIZE + LSDJ_PROJECT_NAME_LENGTH + 1)

// Representation of a project within an LSDJ sav file
typedef struct lsdj_project_t lsdj_project_t;

// Create/free projects
lsdj_project_t* lsdj_project_new(lsdj_error_t** error);
void lsdj_project_free(lsdj_project_t* project);
    
// Deserialize a project from LSDSNG
lsdj_project_t* lsdj_project_read_lsdsng(lsdj_vio_t* vio, lsdj_error_t** error);
lsdj_project_t* lsdj_project_read_lsdsng_from_file(const char* path, lsdj_error_t** error);
lsdj_project_t* lsdj_project_read_lsdsng_from_memory(const unsigned char* data, size_t size, lsdj_error_t** error);
    
// Write a project to an lsdsng file
// Returns the number of bytes written
size_t lsdj_project_write_lsdsng(const lsdj_project_t* project, lsdj_vio_t* vio, lsdj_error_t** error);
size_t lsdj_project_write_lsdsng_to_file(const lsdj_project_t* project, const char* path, lsdj_error_t** error);
size_t lsdj_project_write_lsdsng_to_memory(const lsdj_project_t* project, unsigned char* data, size_t size, lsdj_error_t** error);

// Change data in a project
void lsdj_project_set_name(lsdj_project_t* project, const char* data, size_t size);
void lsdj_project_get_name(const lsdj_project_t* project, char* data, size_t size);
void lsdj_project_set_version(lsdj_project_t* project, unsigned char version);
unsigned char lsdj_project_get_version(const lsdj_project_t* project);
void lsdj_project_set_song(lsdj_project_t* project, lsdj_song_t* song);
lsdj_song_t* lsdj_project_get_song(const lsdj_project_t* project);
    
#ifdef __cplusplus
}
#endif

#endif
