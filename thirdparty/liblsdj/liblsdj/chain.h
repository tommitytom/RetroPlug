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

#ifndef LSDJ_CHAIN_H
#define LSDJ_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "command.h"

// The length of a chain
#define LSDJ_CHAIN_LENGTH (16)
    
// Structure representing a chain
typedef struct
{
    // The phrases in the chain (indices, actual phrases are stored in the song)
    unsigned char phrases[LSDJ_CHAIN_LENGTH];
    
    // The transpositions for each row
    unsigned char transpositions[LSDJ_CHAIN_LENGTH];
} lsdj_chain_t;

// Copy a chain
lsdj_chain_t* lsdj_chain_copy(const lsdj_chain_t* chain);
    
// Clear chain data to factory settings
void lsdj_chain_clear(lsdj_chain_t* chain);
    
#ifdef __cplusplus
}
#endif

#endif
