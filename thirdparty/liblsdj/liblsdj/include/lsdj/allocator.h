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

#ifndef LSDJ_ALLOCATOR_H
#define LSDJ_ALLOCATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

//! Fill this structure with your (de)allocation functions to customize memory handling
typedef struct
{
	//! The function (pointer) to allocate new memory
	/*! The first parameter is the byte size to allocate, the second userData) */
	void*(*allocate)(size_t, void*);

	//! The function (pointer) to free up memory
	/*! The first parameter is the allocated memory, the second the userData) */
	void(*deallocate)(void*, void*);

	//! Any user data passed along to the allocation and deallocation functions
	void* userData;
} lsdj_allocator_t;

//! Allocate memory using an allocator
void* lsdj_allocate(const lsdj_allocator_t* allocator, size_t size);

//! Allocate memory using an allocator, or use malloc() if allocator is NULL
void* lsdj_allocate_or_malloc(const lsdj_allocator_t* allocator, size_t size);

//! Deallocate memory using an allocator
void lsdj_deallocate(const lsdj_allocator_t* allocator, void* data);

//! Deallocate memory using an allocator, or use free() is allocator is NULL
void lsdj_deallocate_or_free(const lsdj_allocator_t* allocator, void* data);
    
#ifdef __cplusplus
}
#endif

#endif
