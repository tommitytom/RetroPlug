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

#include "error.h"

#include <stddef.h>

const char* lsdj_error_get_description(lsdj_error_t error)
{
    switch (error)
    {
        case LSDJ_SUCCESS: return "success";
        case LSDJ_READ_FAILED: return "reading from virtual I/O failed";
        case LSDJ_WRITE_FAILED: return "writing to virtual I/O failed";
        case LSDJ_SEEK_FAILED: return "seeking position within virtual I/O failed";
        case LSDJ_TELL_FAILED: return "telling position within virtual I/O failed";
        case LSDJ_ALLOCATION_FAILED: return "allocating memory failed";
        case LSDJ_NO_PROJECT_AT_INDEX: return "there is no project at the given slot index";
        case LSDJ_DECOMPRESSION_INCORRECT_SIZE: return "the size of a song is not 0x8000 bytes after decompression";
        case LSDJ_SRAM_INITIALIZATION_CHECK_FAILED: return "the SRAM initialization bytes aren't set to 'jk'";
        case LSDJ_FILE_OPEN_FAILED: return "couldn't open a file";
        default: return NULL;
    }
}
