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

#include "sav.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compression.h"
#include "song.h"

//! Empty blocks in the block allocation table have this value
#define LSDJ_SAV_EMPTY_BLOCK_VALUE (0xFF)

//! Representation of an entire LSDj save state
struct lsdj_sav_t
{
    //! The song in active working memory
    lsdj_song_t workingMemorysong;

    //! Index of the project that is currently being edited
    /*! Indices start at 0, a value of LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX means there is no active project */
    uint8_t activeProjectIndex;

    //! The project slots
    /*! If one of these is NULL, it means the slot isn't used by the sav */
    lsdj_project_t* projects[LSDJ_SAV_PROJECT_COUNT];
    
    //! Reserved empty memory
    uint8_t reserved8120[30];

    //! The allocator used to create this sav
    const lsdj_allocator_t* allocator;
};

typedef struct
{
	char projectNames[LSDJ_SAV_PROJECT_COUNT][LSDJ_PROJECT_NAME_LENGTH];
	uint8_t projectVersions[LSDJ_SAV_PROJECT_COUNT];
	uint8_t empty[30];
	uint8_t init[2];
	uint8_t activeProject;
    uint8_t blockAllocationTable[LSDJ_BLOCK_COUNT];
} header_t;


// --- Allocation --- //

//! Allocate a sav in memory
lsdj_error_t lsdj_sav_alloc(lsdj_sav_t** sav, const lsdj_allocator_t* allocator)
{
    *sav = lsdj_allocate_or_malloc(allocator, sizeof(lsdj_sav_t));;
    if (*sav == NULL)
        return LSDJ_ALLOCATION_FAILED;

    memset(*sav, 0, sizeof(lsdj_sav_t));
    (*sav)->allocator = allocator;
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_sav_new(lsdj_sav_t** psav, const lsdj_allocator_t* allocator)
{
    const lsdj_error_t result = lsdj_sav_alloc(psav, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_sav_t* sav = *psav;

    // Set the new song to default
    memcpy(&sav->workingMemorysong, LSDJ_SONG_NEW_BYTES, LSDJ_SONG_BYTE_COUNT);
    
    // Clear the projects
    for (int i = 0; i < LSDJ_SAV_PROJECT_COUNT; i++)
    {
        sav->projects[i] = NULL;
    }
    
    // None of the project slots is active
    sav->activeProjectIndex = LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX;
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_sav_copy(const lsdj_sav_t* source, lsdj_sav_t** destination, const lsdj_allocator_t* allocator)
{
    // Allocate memory for a new sav
    lsdj_error_t result = lsdj_sav_alloc(destination, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_sav_t* copy = *destination;

    memcpy(&copy->workingMemorysong, &source->workingMemorysong, sizeof(source->workingMemorysong));
    copy->activeProjectIndex = source->activeProjectIndex;
    memcpy(copy->reserved8120, source->reserved8120, sizeof(source->reserved8120));

    bool copy_projects_succeeded = true;
    for (int i = 0; i < LSDJ_SAV_PROJECT_COUNT; i++)
    {
        if (source->projects[i] != NULL)
        {
            result = lsdj_project_copy(source->projects[i], &copy->projects[i], allocator);
            if (result != LSDJ_SUCCESS)
            {
                copy_projects_succeeded = false;
                break;
            }
        }
        else
        {
            copy->projects[i] = NULL;
        }
    }

    // If something went wrong, free any copied over projects
    if (!copy_projects_succeeded)
    {
        for (int i = 0; i < LSDJ_SAV_PROJECT_COUNT; i++)
            lsdj_project_free(copy->projects[i]);

        return result;
    }

    return LSDJ_SUCCESS;
}

void lsdj_sav_free(lsdj_sav_t* sav)
{
    if (sav)
    {
        for (int i = 0; i < LSDJ_SAV_PROJECT_COUNT; ++i)
        {
            if (sav->projects[i] != NULL)
                lsdj_project_free(sav->projects[i]);
        }
        
        lsdj_deallocate_or_free(sav->allocator, sav);
    }
}


// --- Changing Data --- //

void lsdj_sav_set_working_memory_song(lsdj_sav_t* sav, const lsdj_song_t* song)
{
    memcpy(&sav->workingMemorysong, song, sizeof(lsdj_song_t));
}

lsdj_error_t lsdj_sav_set_working_memory_song_from_project(lsdj_sav_t* sav, uint8_t index)
{
    if (index >= LSDJ_SAV_PROJECT_COUNT)
        return LSDJ_NO_PROJECT_AT_INDEX;

    const lsdj_project_t* project = lsdj_sav_get_project(sav, index);
    if (project == NULL)
        return LSDJ_NO_PROJECT_AT_INDEX;

    const lsdj_song_t* song = lsdj_project_get_song_const(project);
    assert(song != NULL);
    
    lsdj_sav_set_working_memory_song(sav, song);
    lsdj_sav_set_active_project_index(sav, index);

    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_sav_reset_working_memory_song(lsdj_sav_t* sav)
{
    lsdj_project_t* project = NULL;
    lsdj_error_t result = lsdj_project_new(&project, sav->allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_sav_set_working_memory_song(sav, lsdj_project_get_song(project));
    lsdj_project_free(project);
    
    return LSDJ_SUCCESS;
}

lsdj_song_t* lsdj_sav_get_working_memory_song(lsdj_sav_t* sav)
{
    return &sav->workingMemorysong;
}

const lsdj_song_t* lsdj_sav_get_working_memory_song_const(const lsdj_sav_t* sav)
{
    return &sav->workingMemorysong;
}

void lsdj_sav_set_active_project_index(lsdj_sav_t* sav, uint8_t index)
{
    sav->activeProjectIndex = index;
}

uint8_t lsdj_sav_get_active_project_index(const lsdj_sav_t* sav)
{
    return sav->activeProjectIndex;
}

 lsdj_error_t lsdj_project_new_from_working_memory_song(const lsdj_sav_t* sav, lsdj_project_t** pproject, const lsdj_allocator_t* allocator)
 {
     const lsdj_error_t result = lsdj_project_new(pproject, allocator);
     if (result != LSDJ_SUCCESS)
         return result;
     
     lsdj_project_t* project = *pproject;
    
     uint8_t active = sav->activeProjectIndex;
    
     char name[LSDJ_PROJECT_NAME_LENGTH];
     memset(name, '\0', LSDJ_PROJECT_NAME_LENGTH);
     uint8_t version = 0;
     if (active != LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX)
     {
         const lsdj_project_t* oldProject = lsdj_sav_get_project_const(sav, active);
         if (oldProject != NULL)
         {
             strncpy(name, lsdj_project_get_name(oldProject), LSDJ_PROJECT_NAME_LENGTH);
             version = lsdj_project_get_version(oldProject);
         }
     }
    
     lsdj_project_set_song(project, &sav->workingMemorysong);
     lsdj_project_set_name(project, name);
     lsdj_project_set_version(project, version);
    
     return LSDJ_SUCCESS;
 }

lsdj_error_t lsdj_sav_set_project_copy(lsdj_sav_t* sav, uint8_t index, const lsdj_project_t* project, const lsdj_allocator_t* allocator)
{
    lsdj_project_t** slot = &sav->projects[index];
    if (*slot)
        lsdj_project_free(*slot);
    
    const lsdj_error_t result = lsdj_project_copy(project, slot, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    assert(*slot != NULL);
    
    return LSDJ_SUCCESS;
}

void lsdj_sav_set_project_move(lsdj_sav_t* sav, uint8_t index, lsdj_project_t* project)
{
    lsdj_project_t* slot = sav->projects[index];
    if (slot)
        lsdj_project_free(slot);

    sav->projects[index] = project;
}

void lsdj_sav_erase_project(lsdj_sav_t* sav, uint8_t index)
{
    lsdj_sav_set_project_move(sav, index, NULL);
}

lsdj_project_t* lsdj_sav_get_project(lsdj_sav_t* sav, uint8_t index)
{
    return sav->projects[index];
}

const lsdj_project_t* lsdj_sav_get_project_const(const lsdj_sav_t* sav, uint8_t index)
{
    return sav->projects[index];
}

// Read compressed project data from memory sav file
lsdj_error_t decompress_blocks(lsdj_vio_t* rvio, header_t* header, lsdj_project_t** projects, const lsdj_allocator_t* allocator)
{
    const long firstBlockPosition = lsdj_vio_tell(rvio);
    
    // Pointers for storing decompressed song data
    // Handle decompression
    for (int i = 0; i < LSDJ_BLOCK_COUNT; i += 1)
    {
        uint8_t p = header->blockAllocationTable[i];
        if (p == LSDJ_SAV_EMPTY_BLOCK_VALUE)
            continue;

        // Create the project if this is the first block we come across
        if (projects[p] == NULL)
        {
#ifdef DEBUG
            for (int j = 0; j < i; j += 1)
                assert(header->blockAllocationTable[j] != p);
#endif
            
            // Move to the first block of this project
            // We do this, because reading the last project might have moved rvio all
            // arouned the block space, and we gotta be sure we're at the right starting
            // position.
            if (!lsdj_vio_seek(rvio, firstBlockPosition + i * LSDJ_BLOCK_SIZE, SEEK_SET))
                return LSDJ_SEEK_FAILED;
            
            lsdj_project_t* project = NULL;
            lsdj_error_t result = lsdj_project_new(&project, allocator);
            if (result != LSDJ_SUCCESS)
                return result;
            
            lsdj_project_set_name(project, header->projectNames[p]);
            lsdj_project_set_version(project, header->projectVersions[p]);

            lsdj_song_t song;
            lsdj_memory_access_state_t state;
            state.begin = state.cur = song.bytes;
            state.size = sizeof(song.bytes);

            lsdj_vio_t wvio = lsdj_create_memory_vio(&state);

            size_t readCounter = 0;
            size_t writeCounter = 0;
            
            result = lsdj_decompress(rvio, &readCounter, &wvio, &writeCounter, firstBlockPosition, true);
            if (result != LSDJ_SUCCESS)
            {
                lsdj_project_free(project);
                return false;
            }
            
            assert(writeCounter == LSDJ_SONG_BYTE_COUNT);

            lsdj_project_set_song(project, &song);

            projects[p] = project;
        }
    }

    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_sav_read(lsdj_vio_t* rvio, lsdj_sav_t** psav, const lsdj_allocator_t* allocator)
{
    lsdj_error_t result = lsdj_sav_new(psav, allocator);
    if (result != LSDJ_SUCCESS)
        return result;
    
    lsdj_sav_t* sav = *psav;
    assert(sav != NULL);

    // Read the working memory song
    if (!lsdj_vio_read(rvio, sav->workingMemorysong.bytes, LSDJ_SONG_BYTE_COUNT, NULL))
    {
        lsdj_sav_free(sav);
        return LSDJ_READ_FAILED;
    }
    
    // Read the header block, before we start processing each song
    assert(sizeof(header_t) == LSDJ_BLOCK_SIZE);
    header_t header;
    if (!lsdj_vio_read(rvio, &header, sizeof(header), NULL))
	{
        lsdj_sav_free(sav);
        return LSDJ_READ_FAILED;
    }
    
    // Check the initialization characters. If they're not 'jk', we're
    // probably not dealing with an actual LSDJ sav format file.
    if (header.init[0] != 'j' || header.init[1] != 'k')
    {
        lsdj_sav_free(sav);
        return LSDJ_SRAM_INITIALIZATION_CHECK_FAILED;
    }

    // Store the active project index
    sav->activeProjectIndex = header.activeProject;
    
    // Store the reserved empty memory at 0x8120
    // Not sure what's really in there, but might as well keep it intact
    memcpy(sav->reserved8120, header.empty, sizeof(sav->reserved8120));
    
    // Read the compressed projects
    result = decompress_blocks(rvio, &header, sav->projects, sav->allocator);
    if (result != LSDJ_SUCCESS)
    {
        lsdj_sav_free(sav);
        return result;
    }
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_sav_read_from_file(const char* path, lsdj_sav_t** sav, const lsdj_allocator_t* allocator)
{
    assert(path != NULL);
        
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return LSDJ_FILE_OPEN_FAILED;
    
    lsdj_vio_t rvio = lsdj_create_file_vio(file);

    const lsdj_error_t result = lsdj_sav_read(&rvio, sav, allocator);
    
    fclose(file);
    return result;
}

lsdj_error_t lsdj_sav_read_from_memory(const uint8_t* data, size_t size, lsdj_sav_t** sav, const lsdj_allocator_t* allocator)
{
    assert(data != NULL);

    lsdj_memory_access_state_t state;
    state.begin = state.cur = (uint8_t*)data;
    state.size = size;
    
    lsdj_vio_t rvio = lsdj_create_memory_vio(&state);
    
    return lsdj_sav_read(&rvio, sav, allocator);
}

bool lsdj_sav_is_likely_valid(lsdj_vio_t* vio)
{
    lsdj_song_t song;
    lsdj_vio_read(vio, song.bytes, LSDJ_SONG_BYTE_COUNT, NULL);
    
    // If the working memory song is invalid the song itself surely is as well
    if (!lsdj_song_is_likely_valid(&song))
        return false;
    
    // Move to the initialization bytes
    if (!lsdj_vio_seek(vio, 0x13E, SEEK_CUR))
        return false;
    
    // Ensure these bytes are 'jk', that's what LSDJ sets them to on RAM init
    uint8_t buffer[2];
    if (!lsdj_vio_read(vio, buffer, sizeof(buffer), NULL))
        return false;
    
    if (buffer[0] != 'j' || buffer[1] != 'k')
        return false;
    
    return true;
}

bool lsdj_sav_is_likely_valid_file(const char* path)
{
    assert(path != NULL);
    
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return false;
    
    lsdj_vio_t vio = lsdj_create_file_vio(file);
    
    const bool result = lsdj_sav_is_likely_valid(&vio);
    
    fclose(file);
    return result;
}

bool lsdj_sav_is_likely_valid_memory(const uint8_t* data, size_t size)
{
    assert(data != NULL);
    
    lsdj_memory_access_state_t state;
    state.begin = state.cur = (uint8_t*)data;
    state.size = size;
    
    lsdj_vio_t vio = lsdj_create_memory_vio(&state);
    
    return lsdj_sav_is_likely_valid(&vio);
}

lsdj_error_t compress_projects(lsdj_project_t* const* projects, uint8_t* blocks, uint8_t* blockAllocTable)
{
    lsdj_memory_access_state_t state;
    state.cur = state.begin = blocks;
    state.size = LSDJ_BLOCK_COUNT * LSDJ_BLOCK_SIZE;
    
    lsdj_vio_t wvio = lsdj_create_memory_vio(&state);
    
    unsigned int currentBlock = 1;
    for (int i = 0; i < LSDJ_SAV_PROJECT_COUNT; i++)
    {
        // See if there's a project in this slot
        const lsdj_project_t* project = projects[i];
        if (project == NULL)
            continue;
        
        // Get the song buffer to compress
        const lsdj_song_t* song = lsdj_project_get_song_const(project);
        
        // Compress and store success + how many bytes were written
        size_t compressionSize = 0;
        const lsdj_error_t result = lsdj_compress(song->bytes, &wvio, currentBlock, &compressionSize);
        
        // Bail out if this failed
        if (result != LSDJ_SUCCESS)
            return result;
        
        // Set the block allocation table
        const unsigned int blockCount = (unsigned int)(compressionSize / LSDJ_BLOCK_SIZE);
        memset(blockAllocTable + currentBlock - 1, i, blockCount);
        
        currentBlock += blockCount;
    }
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_sav_write(const lsdj_sav_t* sav, lsdj_vio_t* vio, size_t* writeCounter)
{
    // Write the working project
    if (!lsdj_vio_write(vio, sav->workingMemorysong.bytes, LSDJ_SONG_BYTE_COUNT, writeCounter))
        return LSDJ_WRITE_FAILED;

    // Create the header for writing
    header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.empty, sav->reserved8120, sizeof(sav->reserved8120));
    header.init[0] = 'j';
    header.init[1] = 'k';
    header.activeProject = sav->activeProjectIndex;
    
    // Initialize the block alloc table completely empty (we'll fill this later)
    memset(header.blockAllocationTable, LSDJ_SAV_EMPTY_BLOCK_VALUE, sizeof(header.blockAllocationTable));
    
    // Set the project names and versions
    for (size_t i = 0; i < LSDJ_SAV_PROJECT_COUNT; i++)
    {
        lsdj_project_t* project = sav->projects[i];
        if (project)
        {
            const char* name = lsdj_project_get_name(project);
            memcpy(header.projectNames[i], name, LSDJ_PROJECT_NAME_LENGTH);
            
            header.projectVersions[i] = lsdj_project_get_version(project);
        }
    }
    
    // Compress the projects into blocks
    uint8_t blocks[LSDJ_BLOCK_COUNT * LSDJ_BLOCK_SIZE];
    memset(blocks, 0, sizeof(blocks));
    lsdj_error_t result = compress_projects(sav->projects, blocks, header.blockAllocationTable);
    if (result != LSDJ_SUCCESS)
        return result;
    
    // Write the header to output
    if (!lsdj_vio_write(vio, &header, sizeof(header), writeCounter))
        return LSDJ_WRITE_FAILED;
    
    // Write the blocks
    if (!lsdj_vio_write(vio, blocks, sizeof(blocks), writeCounter))
        return LSDJ_WRITE_FAILED;

    return LSDJ_SUCCESS;
}

 lsdj_error_t lsdj_sav_write_to_file(const lsdj_sav_t* sav, const char* path, size_t* writeCounter)
 {
     assert(sav != NULL);
     assert(path != NULL);
    
     FILE* file = fopen(path, "wb");
     if (file == NULL)
         return LSDJ_FILE_OPEN_FAILED;
    
     lsdj_vio_t vio = lsdj_create_file_vio(file);
     const lsdj_error_t result = lsdj_sav_write(sav, &vio, writeCounter);
     fclose(file);
     
     return result;
 }

lsdj_error_t lsdj_sav_write_to_memory(const lsdj_sav_t* sav, uint8_t* data, size_t size, size_t* writeCounter)
{
    assert(sav != NULL);
    assert(data != NULL);
    
    lsdj_memory_access_state_t state;
    state.begin = state.cur = data;
    state.size = size;
    
    lsdj_vio_t wvio = lsdj_create_memory_vio(&state);
    
    return lsdj_sav_write(sav, &wvio, writeCounter);
}
