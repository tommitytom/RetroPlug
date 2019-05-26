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

#include <assert.h>
#include <string.h>

#include "vio.h"

size_t lsdj_fread(void* ptr, size_t size, void* user_data)
{
    return fread(ptr, size, 1, (FILE*)user_data) * size;
}

size_t lsdj_fwrite(const void* ptr, size_t size, void* user_data)
{
    return fwrite(ptr, size, 1, (FILE*)user_data) * size;
}

long lsdj_ftell(void* user_data)
{
    return ftell((FILE*)user_data);
}

long lsdj_fseek(long offset, int whence, void* user_data)
{
    return fseek((FILE*)user_data, offset, whence);
}

size_t lsdj_mread(void* ptr, size_t size, void* user_data)
{
    lsdj_memory_data_t* mem = (lsdj_memory_data_t*)user_data;
    
    const size_t available = mem->size - (size_t)(mem->cur - mem->begin);
    const size_t minSize = size < available ? size : available;
    
    memcpy(ptr, mem->cur, minSize);
    mem->cur += minSize;
    assert(mem->cur <= mem->begin + mem->size);
    
    return minSize;
}

size_t lsdj_mwrite(const void* ptr, size_t size, void* user_data)
{
    lsdj_memory_data_t* mem = (lsdj_memory_data_t*)user_data;
    
    const size_t available = mem->size - (size_t)(mem->cur - mem->begin);
    const size_t minSize = size < available ? size : available;
    
    memcpy(mem->cur, ptr, minSize);
    mem->cur += minSize;
    assert(mem->cur <= mem->begin + mem->size);
    
    return minSize;
}

long lsdj_mtell(void* user_data)
{
    const lsdj_memory_data_t* mem = (const lsdj_memory_data_t*)user_data;
    
    long pos = mem->cur - mem->begin;
    if (pos < 0 || pos > mem->size)
        return -1L;
    
    return pos;
}

long lsdj_mseek(long offset, int whence, void* user_data)
{
    lsdj_memory_data_t* mem = (lsdj_memory_data_t*)user_data;
    
    switch (whence)
    {
        case SEEK_SET: mem->cur = mem->begin + offset; break;
        case SEEK_CUR: mem->cur = mem->cur + offset; break;
        case SEEK_END: mem->cur = mem->begin + mem->size + offset; break;
    }
    
    if (mem->cur < mem->begin ||
        mem->cur > mem->begin + mem->size)
    {
        return 1;
    }
    
    return 0;
}
