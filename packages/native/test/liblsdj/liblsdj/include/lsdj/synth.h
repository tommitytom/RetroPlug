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

#ifndef LSDJ_SYNTH_H
#define LSDJ_SYNTH_H

#include <stdbool.h>

#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

//! The amount of synths in a song
#define LSDJ_SYNTH_COUNT (0x10)

//! The amount of bytes a synth takes
#define LSDJ_SYNTH_BYTE_COUNT (16)

//! The waveform shapes the synth can use
typedef enum
{
	LSDJ_SYNTH_WAVEFORM_SAW = 0,
	LSDJ_SYNTH_WAVEFORM_SQUARE,
	LSDJ_SYNTH_WAVEFORM_TRIANGLE
} lsdj_synth_waveform_t;

//! The filter types the synth can use
typedef enum
{
	LSDJ_SYNTH_FILTER_LOW_PASS = 0,
	LSDJ_SYNTH_FILTER_HIGH_PASS,
	LSDJ_SYNTH_FILTER_BAND_PASS,
	LSDJ_SYNTH_FILTER_ALL_PASS,
} lsdj_synth_filter_t;

//! The distortion types the synth can use
typedef enum
{
	LSDJ_SYNTH_DISTORTION_CLIP = 0,
	LSDJ_SYNTH_DISTORTION_WRAP,
	LSDJ_SYNTH_DISTORTION_FOLD
} lsdj_synth_distortion_t;

//! The phase compression modes the synth can use
typedef enum
{
	LSDJ_SYNTH_PHASE_NORMAL = 0,
	LSDJ_SYNTH_PHASE_RESYNC,
	LSDJ_SYNTH_PHASE_RESYNC2
} lsdj_synth_phase_compression_t;

//! Set the flag that the wave for this synth has been overwritten
/*! @param song The song containing the synth and wave
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param overwritten True when the wave has been overwritten */
void lsdj_synth_set_wave_overwritten(lsdj_song_t* song, uint8_t synth, bool overwritten);

//! Has the wave of this synth been overwritten?
/*! @param song The song containing the synth and wave
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@return True when the wave has been overwritten */
bool lsdj_synth_is_wave_overwritten(const lsdj_song_t* song, uint8_t synth);

//! Change the waveform type of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param waveform The waveform to set */
void lsdj_synth_set_waveform(lsdj_song_t* song, uint8_t synth, lsdj_synth_waveform_t waveform);

//! Retrieve the waveform type of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
lsdj_synth_waveform_t lsdj_synth_get_waveform(const lsdj_song_t* song, uint8_t synth);

//! Change the filter type of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param filter The filter to set */
void lsdj_synth_set_filter(lsdj_song_t* song, uint8_t synth, lsdj_synth_filter_t filter);

//! Retrieve the filter type of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
lsdj_synth_filter_t lsdj_synth_get_filter(const lsdj_song_t* song, uint8_t synth);

//! Change the distortion type of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param distortion The distortion to set */
void lsdj_synth_set_distortion(lsdj_song_t* song, uint8_t synth, lsdj_synth_distortion_t distortion);

//! Retrieve the distortion type of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
lsdj_synth_distortion_t lsdj_synth_get_distortion(const lsdj_song_t* song, uint8_t synth);

//! Change the phase compression mode of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param phase_compression The phase_compression to set */
void lsdj_synth_set_phase_compression(lsdj_song_t* song, uint8_t synth, lsdj_synth_phase_compression_t compression);

//! Retrieve the phase compression mode of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
lsdj_synth_phase_compression_t lsdj_synth_get_phase_compression(const lsdj_song_t* song, uint8_t synth);

//! Change the volume start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param volume The volume to set */
void lsdj_synth_set_volume_start(lsdj_song_t* song, uint8_t synth, uint8_t volume);

//! Retrieve the volume start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_volume_start(const lsdj_song_t* song, uint8_t synth);

//! Change the volume end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param volume The volume to set */
void lsdj_synth_set_volume_end(lsdj_song_t* song, uint8_t synth, uint8_t volume);

//! Retrieve the volume end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_volume_end(const lsdj_song_t* song, uint8_t synth);

//! Change the resonance start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param resonance The resonance to set */
void lsdj_synth_set_resonance_start(lsdj_song_t* song, uint8_t synth, uint8_t resonance);

//! Retrieve the resonance start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_resonance_start(const lsdj_song_t* song, uint8_t synth);

//! Change the resonance end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param resonance The resonance to set
	@return false if the format version doesn't support this (< 5)*/
bool lsdj_synth_set_resonance_end(lsdj_song_t* song, uint8_t synth, uint8_t resonance);

//! Retrieve the resonance end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_resonance_end(const lsdj_song_t* song, uint8_t synth);

//! Change the cutoff start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param cutoff The cutoff to set */
void lsdj_synth_set_cutoff_start(lsdj_song_t* song, uint8_t synth, uint8_t cutoff);

//! Retrieve the cutoff start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_cutoff_start(const lsdj_song_t* song, uint8_t synth);

//! Change the cutoff end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param cutoff The cutoff to set */
void lsdj_synth_set_cutoff_end(lsdj_song_t* song, uint8_t synth, uint8_t cutoff);

//! Retrieve the cutoff end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_cutoff_end(const lsdj_song_t* song, uint8_t synth);

//! Change the vshift start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param vshift The vshift to set */
void lsdj_synth_set_vshift_start(lsdj_song_t* song, uint8_t synth, uint8_t vshift);

//! Retrieve the vshift start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_vshift_start(const lsdj_song_t* song, uint8_t synth);

//! Change the vshift end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param vshift The vshift to set */
void lsdj_synth_set_vshift_end(lsdj_song_t* song, uint8_t synth, uint8_t vshift);

//! Retrieve the vshift end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_vshift_end(const lsdj_song_t* song, uint8_t synth);

//! Change the limit start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param limit The limit to set */
void lsdj_synth_set_limit_start(lsdj_song_t* song, uint8_t synth, uint8_t limit);

//! Retrieve the limit start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_limit_start(const lsdj_song_t* song, uint8_t synth);

//! Change the limit end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param limit The limit to set */
void lsdj_synth_set_limit_end(lsdj_song_t* song, uint8_t synth, uint8_t limit);

//! Retrieve the limit end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_limit_end(const lsdj_song_t* song, uint8_t synth);

//! Change the phase start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param phase The phase to set */
void lsdj_synth_set_phase_start(lsdj_song_t* song, uint8_t synth, uint8_t phase);

//! Retrieve the phase start of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_phase_start(const lsdj_song_t* song, uint8_t synth);

//! Change the phase end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT)
	@param phase The phase to set */
void lsdj_synth_set_phase_end(lsdj_song_t* song, uint8_t synth, uint8_t phase);

//! Retrieve the phase end of a synth
/*! @param song The song containing the synth
	@param synth The index of the synth (< LSDJ_SYNTH_COUNT) */
uint8_t lsdj_synth_get_phase_end(const lsdj_song_t* song, uint8_t synth);
    
#ifdef __cplusplus
}
#endif

#endif
