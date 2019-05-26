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

#include <string.h>

#include "synth.h"

void lsdj_synth_clear(lsdj_synth_t* synth)
{
    synth->waveform = LSDJ_SYNTH_WAVEFORM_SAWTOOTH;
    synth->filter = LSDJ_SYNTH_FILTER_LOW_PASS;
    synth->resonanceStart = 0;
    synth->resonanceEnd = 0;
    synth->distortion = LSDJ_SYNTH_DISTORTION_CLIP;
    synth->phase = LSDJ_SYNTH_PHASE_NORMAL;
    synth->volumeStart = 0x10;
    synth->cutOffStart = 0xFF;
    synth->phaseStart = 0;
    synth->vshiftStart = 0;
    synth->volumeEnd = 0x10;
    synth->cutOffEnd = 0xFF;
    synth->phaseEnd = 0;
    synth->vshiftEnd = 0;
    synth->limitStart = 0xF;
    synth->limitEnd = 0xF;
    synth->reserved[0] = 0;
    synth->reserved[1] = 0;
    
    synth->overwritten = 0;
}
