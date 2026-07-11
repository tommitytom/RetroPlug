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

#ifndef LSDJ_SAV_H
#define LSDJ_SAV_H

/*! lsdj_sav_t is the root structure of an LSDj save file. In essence, it
	represents a working memory song and projects slots (which contain songs
	themselves).

	The songs in an lsdj_sav_t are stored in their uncompressed, raw byte state.
	This means that you'll need to parse them using song functions yourself,
	but if you just want to move projects around you can do that in their raw
	buffer state (and saves you a lot of parsing); it's is also more future proof
	than always parsing every song. */

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

#include <stdbool.h>

#include "error.h"
#include "project.h"
#include "song.h"
#include "vio.h"

//! The amount of project slots in an LSDj sav state
#define LSDJ_SAV_PROJECT_COUNT (32)

//! A value that means none of the sav project slots is active
#define LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX (0xFF)

//! The place in sav memory where the header starts 
#define LSDJ_SAV_HEADER_POSITION LSDJ_SONG_BYTE_COUNT

//! The size of a full sav file
#define LSDJ_SAV_SIZE (0x20000)
    
//! A structure representing a full LSDj sav state
typedef struct lsdj_sav_t lsdj_sav_t;


// --- ALLOCATION --- //

//! Create a new save
/*! The working memory song buffer is zeroed out, and the sav contains no
	songs in the project slots.
 
    @param sav Pointer to the place where a sav will be created
    @param allocator The allocator to use to create the sav (or NULL for default)
    @return Whether the new was successful
 
	@note Every call must be paired with an lsdj_sav_free() */
lsdj_error_t lsdj_sav_new(lsdj_sav_t** sav, const lsdj_allocator_t* allocator);

//! Copy a save into a new save
/*! Creates a new save and copies the data into it
	
    @param source The sav to copy from
    @param destination The pointer to point to the new sav
    @param allocator The allocator (or null) used for memory (de)allocation
    @note Every call must be paired with an lsdj_sav_free() */
lsdj_error_t lsdj_sav_copy(const lsdj_sav_t* source, lsdj_sav_t** destination, const lsdj_allocator_t* allocator);

//! Frees a sav from memory
/*! Call this when you no longer need a sav. */
void lsdj_sav_free(lsdj_sav_t* sav);


// --- CHANGING DATA --- //

//! Change the working memory song buffer
/*! The song buffer's data is copied into the sav. This leaves the original intact */
void lsdj_sav_set_working_memory_song(lsdj_sav_t* sav, const lsdj_song_t* song);

//! Copy the song buffer from a given project into the working memory
/*! This effectively loads a project, and sets the current index to reflect that
	@return false if the index is out of bounds, or the slot is empty */
lsdj_error_t lsdj_sav_set_working_memory_song_from_project(lsdj_sav_t* sav, uint8_t index);

//! Change the working memory song buffer to the default song
lsdj_error_t lsdj_sav_reset_working_memory_song(lsdj_sav_t* sav);
    
//! Retrieve the working memory song buffer from a sav
/*! This funtion returns a mutable song. See lsdj_sav_get_working_memory_song_const() for immutable song retrieval */
lsdj_song_t* lsdj_sav_get_working_memory_song(lsdj_sav_t* sav);

//! Retrieve the working memory song buffer from a sav
/*! This funtion returns a const song. See lsdj_sav_get_working_memory_song() for mutable song retrieval */
const lsdj_song_t* lsdj_sav_get_working_memory_song_const(const lsdj_sav_t* sav);
    
//! Change which project slot is referenced by the working memory song
/*! Indices start at 0.
	Use LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX if none of the project slots should be the active one. */
void lsdj_sav_set_active_project_index(lsdj_sav_t* sav, uint8_t index);
    
//! Retrieve the index of the project slot the working memory song represents
/*! Indices start at 0.
	If the working memory doesn't represent any project, this is LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX */
uint8_t lsdj_sav_get_active_project_index(const lsdj_sav_t* sav);
    
//! Create a new project from the working memory song
/*! Copies the working memory song into a new project. If the song represents a project in one of
    the slots, its name and version are also copied over
 
    @note Remember to call lsdj_project_free() after you're done with the result */
lsdj_error_t lsdj_project_new_from_working_memory_song(const lsdj_sav_t* sav, lsdj_project_t** project, const lsdj_allocator_t* allocator);

//! Copy a project into one of the project slots
/*! This copies data from the parameter project into the sav, without taking over ownership
	of the original project parameter.
 
	@param project The project to move, or NULL is the slot should be freed up
    @return Whether the copy was successful */
lsdj_error_t lsdj_sav_set_project_copy(lsdj_sav_t* sav, uint8_t index, const lsdj_project_t* project, const lsdj_allocator_t* allocator);
    
//! Move a project into one of the project slots
/*! This moves the parameter project into the sav, taking over ownership. You don't have
	to call lsdj_project_free() afterwards anymore.
 
	@param project The project to move, or NULL is the slot should be freed up */
void lsdj_sav_set_project_move(lsdj_sav_t* sav, uint8_t index, lsdj_project_t* project);

//! Free up one of the project slots in a sav
void lsdj_sav_erase_project(lsdj_sav_t* sav, uint8_t index);
    
//! Retrieve one of the projects
/*! The index should be < LSDJ_SAV_PROJECT_COUNT
    This funtion returns a mutable project. See lsdj_sav_get_project_const() for immutable project retrieval
	@return NULL if the project slot is empty */
lsdj_project_t* lsdj_sav_get_project(lsdj_sav_t* sav, uint8_t index);

//! Retrieve one of the projects
/*! The index should be < LSDJ_SAV_PROJECT_COUNT
    This funtion returns a const project. See lsdj_sav_get_project() for mutable project retrieval
    @return NULL if the project slot is empty */
const lsdj_project_t* lsdj_sav_get_project_const(const lsdj_sav_t* sav, uint8_t index);


// --- I/O --- //

//! Read an LSDj sav from virtual I/O
/*! @param rvio The virtual stream to read from
    @param sav A pointer to the place where the sav will be created
    @param allocator The allocator that will be used to create the sav (or NULL)
    @return Error/success code */
lsdj_error_t lsdj_sav_read(lsdj_vio_t* rvio, lsdj_sav_t** sav, const lsdj_allocator_t* allocator);

//! Read an LSDj sav from file
/*! @param path The path to te file to read from
    @param sav A pointer to the place where the sav will be created
    @param allocator The allocator that will be used to create the sav (or NULL)
    @return Error/success code */
lsdj_error_t lsdj_sav_read_from_file(const char* path, lsdj_sav_t** sav, const lsdj_allocator_t* allocator);

//! Read an LSDj sav from memory
/*! @param data Points to the memory to read from
	@param size The size in bytes of the memory to read from
    @param sav A pointer to the place where the sav will be created
    @param allocator The allocator that will be used to create the sav (or NULL)
    @return Error/success code */
lsdj_error_t lsdj_sav_read_from_memory(const uint8_t* data, size_t size, lsdj_sav_t** sav, const lsdj_allocator_t* allocator);

//! Find out whether given data is likely a valid sav
/*! @note This is not a 100% guarantee that the data will load, we're just checking some heuristics. */
bool lsdj_sav_is_likely_valid(lsdj_vio_t* wvio);

//! Find out whether given data is likely a valid sav
/*! @note This is not a 100% guarantee that the data will load, we're just checking some heuristics. */
bool lsdj_sav_is_likely_valid_file(const char* path);

//! Find out whether given data is likely a valid sav
/*! @note This is not a 100% guarantee that the data will load, we're just checking some heuristics. */
bool lsdj_sav_is_likely_valid_memory(const uint8_t* data, size_t size);
    
//! Write a sav to virtual I/O
/*! @param sav The save to be written to stream
    @param rvio The virtual stream into which the sav is written
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)

    @return Whether the write was successful */
lsdj_error_t lsdj_sav_write(const lsdj_sav_t* sav, lsdj_vio_t* rvio, size_t* writeCounter);

//! Write a sav to file
/*! @param sav The save to be written to memory
    @param path The path to the file where the sav should be written on disk
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)

    @return Whether the write was successful */
lsdj_error_t lsdj_sav_write_to_file(const lsdj_sav_t* sav, const char* path, size_t* writeCounter);

//! Write a sav to memory
/*! @param sav The save to be written to memory
    @param data Pointer to the write buffer, should be at least LSDJ_SAV_SIZE in size
    @param writeCounter The amount of bytes written is _added_ to this value, if provided (you should initialize this)

    @return Whether the write was successful */
lsdj_error_t lsdj_sav_write_to_memory(const lsdj_sav_t* sav, uint8_t* data, size_t size, size_t* writeCounter);
   

#ifdef __cplusplus
}
#endif

#endif
