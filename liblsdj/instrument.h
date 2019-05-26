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

#ifndef LSDJ_INSTRUMENT_H
#define LSDJ_INSTRUMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "error.h"
#include "instrument_kit.h"
#include "instrument_noise.h"
#include "instrument_pulse.h"
#include "instrument_wave.h"
#include "panning.h"
#include "vio.h"

// The default constant length of an instrument name
#define LSDJ_INSTRUMENT_NAME_LENGTH (5)

#define LSDJ_LSDJ_DEFAULT_INSTRUMENT_LENGTH (16)
static const unsigned char LSDJ_DEFAULT_INSTRUMENT[LSDJ_LSDJ_DEFAULT_INSTRUMENT_LENGTH] = { 0, 0xA8, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0 };

typedef enum
{
    LSDJ_INSTR_PULSE,
    LSDJ_INSTR_WAVE,
    LSDJ_INSTR_KIT,
    LSDJ_INSTR_NOISE
} instrument_type;

#define LSDJ_NO_TABLE (0x20)
#define LSDJ_INSTRUMENT_UNLIMITED_LENGTH (0x40)
#define LSDJ_KIT_LENGTH_AUTO (0x0)

typedef struct lsdj_instrument_t lsdj_instrument_t;

// Copy a instrument
lsdj_instrument_t* lsdj_instrument_new();
lsdj_instrument_t* lsdj_instrument_copy(const lsdj_instrument_t* instrument);
void lsdj_instrument_free(lsdj_instrument_t* instrument);
    
// Clear all instrument data to factory settings
void lsdj_instrument_clear(lsdj_instrument_t* instrument);
void lsdj_instrument_clear_as_pulse(lsdj_instrument_t* instrument);
void lsdj_instrument_clear_as_wave(lsdj_instrument_t* instrument);
void lsdj_instrument_clear_as_kit(lsdj_instrument_t* instrument);
void lsdj_instrument_clear_as_noise(lsdj_instrument_t* instrument);

// Instrument I/O
void lsdj_instrument_read(lsdj_vio_t* vio, unsigned char version, lsdj_instrument_t* instrument, lsdj_error_t** error);
void lsdj_instrument_write(const lsdj_instrument_t* instrument, unsigned char version, lsdj_vio_t* vio, lsdj_error_t** error);

void lsdj_instrument_set_name(lsdj_instrument_t* instrument, const char* data, size_t size);
void lsdj_instrument_get_name(const lsdj_instrument_t* instrument, char* data, size_t size);

void lsdj_instrument_set_panning(lsdj_instrument_t* instrument, lsdj_panning panning);
lsdj_panning lsdj_instrument_get_panning(const lsdj_instrument_t* instrument);
    
#ifdef __cplusplus
}
#endif

#endif
