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

#include "project.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "compression.h"

struct lsdj_project_t
{
    //! The name of the project
    char name[LSDJ_PROJECT_NAME_LENGTH];
    
    //! The version of the song
    /*! @note This is a simply song version counter increased with every song save in LSDJ; it has nothing to do with LSDJ versions of the sav format version. */
    uint8_t version;
    
    //! The song belonging to this project
    /*! Uncompressed, but you'll need to call a parsing function to get a sensible lsdj_song_t structured object. */
    lsdj_song_t song;

    //! The allocator used to create this project
    const lsdj_allocator_t* allocator;
};


// --- Allocation --- //

lsdj_error_t lsdj_project_alloc(lsdj_project_t** project, const lsdj_allocator_t* allocator)
{
    *project = lsdj_allocate_or_malloc(allocator, sizeof(lsdj_project_t));;
    if (*project == NULL)
        return LSDJ_ALLOCATION_FAILED;

    (*project)->allocator = allocator;
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_project_new(lsdj_project_t** pproject, const lsdj_allocator_t* allocator)
{
    const lsdj_error_t result = lsdj_project_alloc(pproject, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_project_t* project = *pproject;
    assert(project != NULL);
        
    memset(project->name, '\0', LSDJ_PROJECT_NAME_LENGTH);
    project->version = 0;
    memcpy(&project->song, LSDJ_SONG_NEW_BYTES, LSDJ_SONG_BYTE_COUNT);
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_project_copy(const lsdj_project_t* source, lsdj_project_t** destination, const lsdj_allocator_t* allocator)
{
    const lsdj_error_t result = lsdj_project_alloc(destination, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_project_t* copy = *destination;

    memcpy(copy->name, source->name, LSDJ_PROJECT_NAME_LENGTH);
    copy->version = source->version;
    memcpy(&copy->song, &source->song, LSDJ_SONG_BYTE_COUNT);

    return LSDJ_SUCCESS;
}

void lsdj_project_free(lsdj_project_t* project)
{
    if (project)
        lsdj_deallocate_or_free(project->allocator, project);
}


// --- Changing Data --- //

void lsdj_project_set_name(lsdj_project_t* project, const char* name)
{
    strncpy(project->name, name, LSDJ_PROJECT_NAME_LENGTH);
    sanitize_name(project->name, LSDJ_PROJECT_NAME_LENGTH);
}

const char* lsdj_project_get_name(const lsdj_project_t* project)
{
    return project->name;
}

size_t lsdj_project_get_name_length(const lsdj_project_t* project)
{
    return strnlen(project->name, LSDJ_PROJECT_NAME_LENGTH);
}

void lsdj_project_set_version(lsdj_project_t* project, uint8_t version)
{
    project->version = version;
}

uint8_t lsdj_project_get_version(const lsdj_project_t* project)
{
    return project->version;
}

void lsdj_project_set_song(lsdj_project_t* project, const lsdj_song_t* song)
{
    memcpy(&project->song, song, sizeof(lsdj_song_t));
}

lsdj_song_t* lsdj_project_get_song(lsdj_project_t* project)
{
    return &project->song;
}

const lsdj_song_t* lsdj_project_get_song_const(const lsdj_project_t* project)
{
    return &project->song;
}


// --- I/O --- //

lsdj_error_t lsdj_project_read_lsdsng(lsdj_vio_t* rvio, lsdj_project_t** pproject, const lsdj_allocator_t* allocator)
{
    lsdj_error_t result = lsdj_project_alloc(pproject, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_project_t* project = *pproject;
    
    if (!lsdj_vio_read(rvio, project->name, LSDJ_PROJECT_NAME_LENGTH, NULL))
    {
        lsdj_project_free(project);
        return LSDJ_READ_FAILED;
    }
    
    if (!lsdj_vio_read_byte(rvio, &project->version, NULL))
    {
        lsdj_project_free(project);
        return LSDJ_READ_FAILED;
    }

    // Decompress the song data
    lsdj_song_t song;
    memset(&song, 0, sizeof(lsdj_song_t));
    
    lsdj_memory_access_state_t state;
    state.begin = state.cur = song.bytes;
    state.size = sizeof(song.bytes);
    
    lsdj_vio_t wvio = lsdj_create_memory_vio(&state);
    
    result = lsdj_decompress(rvio, NULL, &wvio, NULL, lsdj_vio_tell(rvio), false);
    if (result != LSDJ_SUCCESS)
    {
        lsdj_project_free(project);
        return result;
    }
    
    lsdj_project_set_song(project, &song);
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_project_read_lsdsng_from_file(const char* path, lsdj_project_t** project, const lsdj_allocator_t* allocator)
{
    assert(path != NULL);
    
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return LSDJ_FILE_OPEN_FAILED;

    lsdj_vio_t rvio = lsdj_create_file_vio(file);
    
    const lsdj_error_t result = lsdj_project_read_lsdsng(&rvio, project, allocator);
    
    fclose(file);
    
    return result;
}

lsdj_error_t lsdj_project_read_lsdsng_from_memory(const uint8_t* data, size_t size, lsdj_project_t** project, const lsdj_allocator_t* allocator)
{
    assert(data != NULL);
    
    lsdj_memory_access_state_t state;
    state.begin = state.cur = (uint8_t*)data;
    state.size = size;

    lsdj_vio_t rvio = lsdj_create_memory_vio(&state);
    
    return lsdj_project_read_lsdsng(&rvio, project, allocator);
}

bool lsdj_project_is_likely_valid_lsdsng(lsdj_vio_t* vio)
{
    // Really, the only thing we can do is check whether the name contains valid chars
    // Can't do any checks on the format version, not any byte count checks (the virtual
    // I/O might actually contain more bytes than just the .lsdsng)
    
    // Read the name first
    char name[LSDJ_PROJECT_NAME_LENGTH];
    memset(name, '\0', sizeof(name));
    if (!lsdj_vio_read(vio, name, sizeof(name), NULL))
        return false;
    
    // Check if any of the characters is invalid
    for (uint8_t i = 0; i < strnlen(name, LSDJ_PROJECT_NAME_LENGTH); i += 1)
    {
        if (!is_valid_name_char(name[i]))
            return false;
    }
    
    // If not, return true
    return true;
}

bool lsdj_project_is_likely_valid_lsdsng_file(const char* path)
{
    assert(path != NULL);
    
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return false;
    
    lsdj_vio_t rvio = lsdj_create_file_vio(file);
    
    bool result = lsdj_project_is_likely_valid_lsdsng(&rvio);
    
    fclose(file);
    return result;
}

bool lsdj_project_is_likely_valid_lsdsng_memory(const uint8_t* data, size_t size)
{
    assert(data != NULL);
    
    lsdj_memory_access_state_t state;
    state.begin = state.cur = (uint8_t*)data;
    state.size = size;
    
    lsdj_vio_t rvio = lsdj_create_memory_vio(&state);
    
    return lsdj_project_is_likely_valid_lsdsng(&rvio);
}

lsdj_error_t lsdj_project_write_lsdsng(const lsdj_project_t* project, lsdj_vio_t* wvio, size_t* writeCounter)
{
    // Write the name
    if (!lsdj_vio_write(wvio, project->name, LSDJ_PROJECT_NAME_LENGTH, writeCounter))
        return LSDJ_WRITE_FAILED;
    
    // Write the version
    if (!lsdj_vio_write_byte(wvio, project->version, writeCounter))
        return LSDJ_WRITE_FAILED;
    
    // Compress and write the song buffer
    const lsdj_song_t* song = lsdj_project_get_song_const(project);
    return lsdj_compress(song->bytes, wvio, 1, writeCounter);
}

lsdj_error_t lsdj_project_write_lsdsng_to_file(const lsdj_project_t* project, const char* path, size_t* writeCounter)
{
    assert(project != NULL);
    assert(path != NULL);

    FILE* file = fopen(path, "wb");
    if (file == NULL)
        return LSDJ_FILE_OPEN_FAILED;

    lsdj_vio_t wvio = lsdj_create_file_vio(file);
    const lsdj_error_t result = lsdj_project_write_lsdsng(project, &wvio, writeCounter);
    fclose(file);

    return result;
}

lsdj_error_t lsdj_project_write_lsdsng_to_memory(const lsdj_project_t* project, uint8_t* data, size_t* writeCounter)
{
    assert(project != NULL);
    assert(data != NULL);
    
    lsdj_memory_access_state_t state;
    state.begin = state.cur = data;
    state.size = LSDSNG_MAX_SIZE;
    
    lsdj_vio_t wvio = lsdj_create_memory_vio(&state);
    
    return lsdj_project_write_lsdsng(project, &wvio, writeCounter);
}
