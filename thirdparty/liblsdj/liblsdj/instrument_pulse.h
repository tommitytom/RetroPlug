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

#ifndef LSDJ_INSTRUMENT_PULSE_H
#define LSDJ_INSTRUMENT_PULSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "instrument_constants.h"

typedef unsigned char lsdj_pulse_wave;
#define LSDJ_PULSE_WAVE_PW_125 (0)
#define LSDJ_PULSE_WAVE_PW_25 (1)
#define LSDJ_PULSE_WAVE_PW_50 (2)
#define LSDJ_PULSE_WAVE_PW_75 (3)

typedef struct
{
    lsdj_pulse_wave pulseWidth;
    unsigned char length; // 0x40 and above = unlimited
    unsigned char sweep;
    lsdj_plvib_speed plvibSpeed;
    lsdj_vib_shape vibShape;
    lsdj_vib_direction vibratoDirection;
    char transpose;
    char drumMode;
    unsigned char pulse2tune;
    unsigned char fineTune;
} lsdj_instrument_pulse_t;
    
#ifdef __cplusplus
}
#endif

#endif
