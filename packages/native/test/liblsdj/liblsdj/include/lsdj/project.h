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

#ifndef LSDJ_PROJECT_H
#define LSDJ_PROJECT_H

/* Projects are basically song buffers with a name and a version. They are
   used to represent the song slots in a save file (which you can load from
   LSDJ's load/sav/erase screen), as well as loaded in .lsdsng's, because
   they too contain a project name and version. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "allocator.h"
#include "error.h"
#include "song.h"
#include "vio.h"

//! The length of project names
#define LSDJ_PROJECT_NAME_LENGTH (8)

//! The maximum size of an lsdsng is a version byte +  project name + full song
#define LSDSNG_MAX_SIZE (1 + LSDJ_PROJECT_NAME_LENGTH + LSDJ_SONG_BYTE_COUNT)

//! Representation of a project within an LSDJ sav file, or an imported .lsdsng
typedef struct lsdj_project_t lsdj_project_t;


// --- Allocation --- //

//! Create a new project
/*! Creates a new project with an empty name, version #0 and zeroed out song

    @param project Pointer to the place where a project will be created
    @param allocator The allocator to use to create the sav (or NULL for default)
    @return Whether the new was successful
 
    @note Every call must be paired with an lsdj_project_free() */
lsdj_error_t lsdj_project_new(lsdj_project_t** project, const lsdj_allocator_t* allocator);

//! Copy a project into a new project
/*! Creates a new project and copies the data into it.

    @param source The project to copy from
    @param destination The pointer to point to the new project
    @param allocator The allocator (or null) used for memory (de)allocation
	@note Every call must be paired with an lsdj_project_free() */
lsdj_error_t lsdj_project_copy(const lsdj_project_t* source, lsdj_project_t** destination, const lsdj_allocator_t* allocator);

//! Frees a project from memory
/*! Call this when you no longer need a project. */
void lsdj_project_free(lsdj_project_t* project);


// --- Changing data --- //

//! Change the name of a project
/*! @param project The project to change the name for
    @param data A pointer to char data (maximum LSDJ_PROJECT_NAME_LENGTH) */
void lsdj_project_set_name(lsdj_project_t* project, const char* name);

//! Retrieve the name of this project
/*! @return data A pointer to char data of maximum LSDJ_PROJECT_NAME_LENGTH long (may not be null-terminated) */
const char* lsdj_project_get_name(const lsdj_project_t* project);

//! Retrieve the length of a project's name
/*! This won't ever be larger than LSDJ_PROJECT_NAME_LENGTH */
size_t lsdj_project_get_name_length(const lsdj_project_t* project);

//! Change the version number of a project
/*! @note This has nothing to do with your LSDj or format version, it's just a project version */
void lsdj_project_set_version(lsdj_project_t* project, uint8_t version);

//! Retrieve the version number of the project
/*! @note This has nothing to do with your LSDj or format version, it's just a project version */
uint8_t lsdj_project_get_version(const lsdj_project_t* project);

//! Copy a full song's byte data into the project
/*! A song buffer's data is copied into the project.
	This leaves the original song buffer intact */
void lsdj_project_set_song(lsdj_project_t* project, const lsdj_song_t* song);

//! Retrieve the song buffer for this project
/*! Song buffers contain the actual song data for a project.
    This funtion returns a mutable song. See lsdj_project_get_song_const() for immutable song retrieval */
lsdj_song_t* lsdj_project_get_song(lsdj_project_t* project);

//! Retrieve the song buffer for this project
/*! Song buffers contain the actual song data for a project.
    This funtion returns a const song. See lsdj_project_get_song() for mutable song retrieval */
const lsdj_song_t* lsdj_project_get_song_const(const lsdj_project_t* project);


// --- I/O --- //

//! Read an LSDJ Project from an .lsdsng I/O streeam
/*! This function uses liblsdj's virtual I/O system. There are other convenience functions to
	directly read from memory or file.

    @param rvio The virtual I/O stream to read from
    @param project The project will be put in the provided pointer
 @param allocator The allocator to create and free memory with for this project (optional)

	@return An error code (or success) */
lsdj_error_t lsdj_project_read_lsdsng(lsdj_vio_t* rvio, lsdj_project_t** project, const lsdj_allocator_t* allocator);

//! Read an LSDJ Project from an .lsdsng file
/*! @param path The path to the lsdsng file to read
    @param project The project will be put in the provided pointer
    @param allocator The allocator to create and free memory with for this project (optional)

    @return An error code (or success) */
lsdj_error_t lsdj_project_read_lsdsng_from_file(const char* path, lsdj_project_t** project, const lsdj_allocator_t* allocator);

//! Read an LSDJ Project from an .lsdsng in memory
/*! @param data Points to the memory to read from
    @param size The amount of bytes of the memory to read from
    @param project The project will be put in the provided pointer
    @param allocator The allocator to create and free memory with for this project (optional)

    @return An error code (or success) */
lsdj_error_t lsdj_project_read_lsdsng_from_memory(const uint8_t* data, size_t size, lsdj_project_t** project, const lsdj_allocator_t* allocator);
    
//! Find out whether given data is likely a valid lsdsng
/*! @note This is not a 100% guarantee that the data will load, we're just checking some heuristics.
    In fact, I wouldn't call this trustworthy at all, but there's little we can do due to the .lsdsng format */
bool lsdj_project_is_likely_valid_lsdsng(lsdj_vio_t* vio);

//! Find out whether a file is likely a valid lsdsng
/*! @note This is not a 100% guarantee that the data will load, we're just checking some heuristics.
    In fact, I wouldn't call this trustworthy at all, but there's little we can do due to the .lsdsng format */
bool lsdj_project_is_likely_valid_lsdsng_file(const char* path);

//! Find out whether a memory address likely contains a valid lsdsng
/*! @note This is not a 100% guarantee that the data will load, we're just checking some heuristics.
    In fact, I wouldn't call this trustworthy at all, but there's little we can do due to the .lsdsng format */
bool lsdj_project_is_likely_valid_lsdsng_memory(const uint8_t* data, size_t size);
    
//! Write a project to an .lsdsng I/O stream
/*! This function uses liblsdj's virtual I/O system. There are other convenience functions to
	directly write to memory or file.
 
    @param project The project to be written to stream
    @param vio The virtual stream into which the project is written
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)

	@return Whether the write was successful */
lsdj_error_t lsdj_project_write_lsdsng(const lsdj_project_t* project, lsdj_vio_t* vio, size_t* writeCounter);

//! Write a project to file
/*! @param project The project to be written to file
    @param path The path to the file where the lsdsng should be written on disk
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)

    @return Whether the write was successful */
lsdj_error_t lsdj_project_write_lsdsng_to_file(const lsdj_project_t* project, const char* path, size_t* writeCounter);

//! Write a project to a memory
/*! @param project The project to be written to memory
    @param data Pointer to the write buffer, should be at least LSDSNG_MAX_SIZE in size
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)

    @return Whether the write was successful */
lsdj_error_t lsdj_project_write_lsdsng_to_memory(const lsdj_project_t* project, uint8_t* data, size_t* writeCounter);
    
#ifdef __cplusplus
}
#endif

#endif
