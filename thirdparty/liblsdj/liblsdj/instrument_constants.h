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

#ifndef LSDJ_INSTRUMENT_CONSTANTS_H
#define LSDJ_INSTRUMENT_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char lsdj_plvib_speed;
#define LSDJ_PLVIB_FAST (0)
#define LSDJ_PLVIB_TICK (1)
#define LSDJ_PLVIB_STEP (2)

typedef unsigned char lsdj_vib_shape;
#define LSDJ_VIB_TRIANGLE (0)
#define LSDJ_VIB_SAWTOOTH (1)
#define LSDJ_VIB_SQUARE (2)

typedef unsigned char lsdj_vib_direction;
#define LSDJ_VIB_UP (0)
#define LSDJ_VIB_DOWN (1)
    
#ifdef __cplusplus
}
#endif

#endif
