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

#ifndef LSDJ_SYNTH_H
#define LSDJ_SYNTH_H

#ifdef __cplusplus
extern "C" {
#endif

#define LSDJ_SYNTH_WAVEFORM_SAWTOOTH (0)
#define LSDJ_SYNTH_WAVEFORM_SQUARE (1)
#define LSDJ_SYNTH_WAVEFORM_TRIANGLE (2)
    
#define LSDJ_SYNTH_FILTER_LOW_PASS (0)
#define LSDJ_SYNTH_FILTER_HIGH_PASS (1)
#define LSDJ_SYNTH_FILTER_BAND_PASS (2)
#define LSDJ_SYNTH_FILTER_ALL_PASS (3)
    
#define LSDJ_SYNTH_DISTORTION_CLIP (0)
#define LSDJ_SYNTH_DISTORTION_WRAP (1)
#define LSDJ_SYNTH_DISTORTION_FOLD (2)
    
#define LSDJ_SYNTH_PHASE_NORMAL (0)
#define LSDJ_SYNTH_PHASE_RESYNC (1)
#define LSDJ_SYNTH_PHASE_RESYNC2 (2)

// Structure representing soft synth data
typedef struct
{
    unsigned char waveform;
    unsigned char filter;
    unsigned char resonanceStart;
    unsigned char resonanceEnd;
    unsigned char distortion;
    unsigned char phase;
    
    unsigned char volumeStart;
    unsigned char volumeEnd;
    unsigned char cutOffStart;
    unsigned char cutOffEnd;
    
    unsigned char phaseStart;
    unsigned char phaseEnd;
    unsigned char vshiftStart;
    unsigned char vshiftEnd;
    
    unsigned char limitStart;
    unsigned char limitEnd;
    
    unsigned char reserved[2];
    
    unsigned char overwritten; // 0 if false, 1 if true
} lsdj_synth_t;

// Clear all soft synth data to factory settings
void lsdj_synth_clear(lsdj_synth_t* synth);
    
#ifdef __cplusplus
}
#endif
    
#endif
