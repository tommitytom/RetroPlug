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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compression.h"
#include "project.h"

#define BLOCK_SIZE 0x200
#define BLOCK_COUNT 191

struct lsdj_project_t
{
    // The name of the project
    char name[LSDJ_PROJECT_NAME_LENGTH];
    
    // The version of the project
    unsigned char version;
    
    // The song belonging to this project
    /*! If this is NULL, the project isn't in use */
    lsdj_song_t* song;
};

lsdj_project_t* alloc_project(lsdj_error_t** error)
{
    lsdj_project_t* project = (lsdj_project_t*)calloc(sizeof(lsdj_project_t), 1);
    if (project == NULL)
    {
        lsdj_error_new(error, "could not allocate project");
        return NULL;
    }
    
    return project;
}

lsdj_project_t* lsdj_project_new(lsdj_error_t** error)
{
    lsdj_project_t* project = alloc_project(error);
    if (project == NULL)
        return NULL;
    
    memset(project->name, '\0', sizeof(project->name));
    project->version = 0;
    project->song = NULL;
    
    return project;
}

void lsdj_project_free(lsdj_project_t* project)
{
    free(project);
}

lsdj_project_t* lsdj_project_read_lsdsng(lsdj_vio_t* vio, lsdj_error_t** error)
{
    lsdj_project_t* project = alloc_project(error);
    
    if (vio->read(project->name, LSDJ_PROJECT_NAME_LENGTH, vio->user_data) != LSDJ_PROJECT_NAME_LENGTH)
    {
        lsdj_error_new(error, "could not read project name");
        lsdj_project_free(project);
        return NULL;
    }
    
    if (vio->read(&project->version, 1, vio->user_data) != 1)
    {
        lsdj_error_new(error, "could not read project version");
        lsdj_project_free(project);
        return NULL;
    }

    // Decompress the data
    unsigned char decompressed[LSDJ_SONG_DECOMPRESSED_SIZE];
    memset(decompressed, 0, sizeof(decompressed));
    
    lsdj_memory_data_t mem;
    mem.begin = mem.cur = decompressed;
    mem.size = sizeof(decompressed);
    
    lsdj_vio_t wvio;
    wvio.write = lsdj_mwrite;
    wvio.tell = lsdj_mtell;
    wvio.seek = lsdj_mseek;
    wvio.user_data = &mem;
    
    lsdj_decompress(vio, &wvio, NULL, BLOCK_SIZE, error);
    if (error && *error)
        return NULL;
    
    // Read in the song
    if (project->song == NULL)
        project->song = lsdj_song_read_from_memory(decompressed, sizeof(decompressed), error);
    
    return project;
}

lsdj_project_t* lsdj_project_read_lsdsng_from_file(const char* path, lsdj_error_t** error)
{
    if (path == NULL)
    {
        lsdj_error_new(error, "path is NULL");
        return NULL;
    }
    
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        char message[512];
        snprintf(message, 512, "could not open %s for reading", path);
        lsdj_error_new(error, message);
        return NULL;
    }
    
    lsdj_vio_t vio;
    vio.read = lsdj_fread;
    vio.tell = lsdj_ftell;
    vio.seek = lsdj_fseek;
    vio.user_data = file;
    
    lsdj_project_t* project = lsdj_project_read_lsdsng(&vio, error);
    
    fclose(file);
    
    return project;
}

lsdj_project_t* lsdj_project_read_lsdsng_from_memory(const unsigned char* data, size_t size, lsdj_error_t** error)
{
    if (data == NULL)
    {
        lsdj_error_new(error, "data is NULL");
        return NULL;
    }
    
    lsdj_memory_data_t mem;
    mem.begin = (unsigned char*)data;
    mem.cur = mem.begin;
    mem.size = size;
    
    lsdj_vio_t vio;
    vio.read = lsdj_mread;
    vio.tell = lsdj_mtell;
    vio.seek = lsdj_mseek;
    vio.user_data = &mem;
    
    return lsdj_project_read_lsdsng(&vio, error);
}

void lsdj_project_write_lsdsng(const lsdj_project_t* project, lsdj_vio_t* vio, lsdj_error_t** error)
{
    if (project->song == NULL)
        return lsdj_error_new(error, "project does not contain a song");
    
    if (vio->write(project->name, LSDJ_PROJECT_NAME_LENGTH, vio->user_data) != LSDJ_PROJECT_NAME_LENGTH)
        return lsdj_error_new(error, "could not write project name for lsdsng");
    
    if (vio->write(&project->version, 1, vio->user_data) != 1)
        return lsdj_error_new(error, "could not write project version for lsdsng");
    
    // Write the song to memory
    unsigned char decompressed[LSDJ_SONG_DECOMPRESSED_SIZE];
    memset(decompressed, 0x34, LSDJ_SONG_DECOMPRESSED_SIZE);
    lsdj_song_write_to_memory(project->song, decompressed, LSDJ_SONG_DECOMPRESSED_SIZE, error);
    if (error && *error)
        return;
    
    // Compress the song
    lsdj_compress(decompressed, BLOCK_SIZE, 1, BLOCK_COUNT, vio, error);
}

void lsdj_project_write_lsdsng_to_file(const lsdj_project_t* project, const char* path, lsdj_error_t** error)
{
    if (path == NULL)
        return lsdj_error_new(error, "path is NULL");
    
    if (project == NULL)
        return lsdj_error_new(error, "project is NULL");
    
    FILE* file = fopen(path, "wb");
    if (file == NULL)
    {
        char message[512];
        snprintf(message, 512, "could not open %s for writing", path);
        return lsdj_error_new(error, message);
    }
    
    lsdj_vio_t vio;
    vio.write = lsdj_fwrite;
    vio.tell = lsdj_ftell;
    vio.seek = lsdj_fseek;
    vio.user_data = file;
    
    lsdj_project_write_lsdsng(project, &vio, error);
    
    fclose(file);
}

void lsdj_project_write_lsdsng_to_memory(const lsdj_project_t* project, unsigned char* data, size_t size, lsdj_error_t** error)
{
    if (project == NULL)
        return lsdj_error_new(error, "project is NULL");
    
    if (data == NULL)
        return lsdj_error_new(error, "data is NULL");
    
    lsdj_memory_data_t mem;
    mem.begin = data;
    mem.cur = mem.begin;
    mem.size = size;
    
    lsdj_vio_t vio;
    vio.write = lsdj_mwrite;
    vio.tell = lsdj_mtell;
    vio.seek = lsdj_mseek;
    vio.user_data = &mem;
    
    lsdj_project_write_lsdsng(project, &vio, error);
}

void lsdj_clear_project(lsdj_project_t* project)
{
    memset(project->name, 0, LSDJ_PROJECT_NAME_LENGTH);
    project->version = 0;
    
    if (project->song)
    {
        lsdj_song_free(project->song);
        project->song = NULL;
    }
}

void lsdj_project_set_name(lsdj_project_t* project, const char* data, size_t size)
{
    strncpy(project->name, data, size < LSDJ_PROJECT_NAME_LENGTH ? size : LSDJ_PROJECT_NAME_LENGTH);
}

void lsdj_project_get_name(const lsdj_project_t* project, char* data, size_t size)
{
    const size_t len = strnlen(project->name, LSDJ_PROJECT_NAME_LENGTH);
    strncpy(data, project->name, len);
    for (size_t i = len; i < LSDJ_PROJECT_NAME_LENGTH; i += 1)
        data[i] = '\0';
}

void lsdj_project_set_version(lsdj_project_t* project, unsigned char version)
{
    project->version = version;
}

unsigned char lsdj_project_get_version(const lsdj_project_t* project)
{
    return project->version;
}

void lsdj_project_set_song(lsdj_project_t* project, lsdj_song_t* song)
{
    if (project->song)
        lsdj_song_free(project->song);
    
    project->song = song;
}

lsdj_song_t* lsdj_project_get_song(const lsdj_project_t* project)
{
    return project->song;
}
