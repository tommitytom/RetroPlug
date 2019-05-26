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

#ifndef LSDJ_COMMAND_H_GUARD
#define LSDJ_COMMAND_H_GUARD

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

#define LSDJ_COMMAND_NONE (0x00)
#define LSDJ_COMMAND_A (0x01)
#define LSDJ_COMMAND_C (0x02)
#define LSDJ_COMMAND_D (0x03)
#define LSDJ_COMMAND_E (0x04)
#define LSDJ_COMMAND_F (0x05)
#define LSDJ_COMMAND_G (0x06)
#define LSDJ_COMMAND_H (0x07)
#define LSDJ_COMMAND_K (0x08)
#define LSDJ_COMMAND_L (0x09)
#define LSDJ_COMMAND_M (0x0a)
#define LSDJ_COMMAND_O (0x0b)
#define LSDJ_COMMAND_P (0x0c)
#define LSDJ_COMMAND_R (0x0d)
#define LSDJ_COMMAND_S (0x0e)
#define LSDJ_COMMAND_T (0x0f)
#define LSDJ_COMMAND_V (0x10)
#define LSDJ_COMMAND_W (0x11)
#define LSDJ_COMMAND_Z (0x12)
#define LSDJ_COMMAND_ARDUINO_BOY_N (0x13)
#define LSDJ_COMMAND_ARDUINO_BOY_X (0x14)
#define LSDJ_COMMAND_ARDUINO_BOY_Q (0x15)
#define LSDJ_COMMAND_ARDUINO_BOY_Y (0x16)
    
// Structure representing an effect command with its argument value
typedef struct
{
    unsigned char command;
    unsigned char value;
} lsdj_command_t;
    
// Clear the command to factory settings
void lsdj_command_clear(lsdj_command_t* command);
    
#ifdef __cplusplus
}
#endif

#endif
