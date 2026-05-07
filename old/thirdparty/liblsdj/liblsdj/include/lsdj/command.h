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

#ifndef LSDJ_COMMAND_H_GUARD
#define LSDJ_COMMAND_H_GUARD

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	LSDJ_COMMAND_NONE = 0,
	LSDJ_COMMAND_A = 1,
	LSDJ_COMMAND_C,
	LSDJ_COMMAND_D,
	LSDJ_COMMAND_E,
	LSDJ_COMMAND_F,
	LSDJ_COMMAND_G,
	LSDJ_COMMAND_H,
	LSDJ_COMMAND_K,
	LSDJ_COMMAND_L,
	LSDJ_COMMAND_M,
	LSDJ_COMMAND_O,
	LSDJ_COMMAND_P,
	LSDJ_COMMAND_R,
	LSDJ_COMMAND_S,
	LSDJ_COMMAND_T,
	LSDJ_COMMAND_V,
	LSDJ_COMMAND_W,
	LSDJ_COMMAND_Z,
	LSDJ_COMMAND_ARDUINO_BOY_N,
	LSDJ_COMMAND_ARDUINO_BOY_X,
	LSDJ_COMMAND_ARDUINO_BOY_Q,
	LSDJ_COMMAND_ARDUINO_BOY_Y,
    LSDJ_COMMAND_B // Added in 7.1.0
} lsdj_command_t;
    
#ifdef __cplusplus
}
#endif

#endif
