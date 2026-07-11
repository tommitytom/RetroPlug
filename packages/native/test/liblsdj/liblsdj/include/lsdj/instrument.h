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

#ifndef LSDJ_INSTRUMENT_H
#define LSDJ_INSTRUMENT_H

#include <stdbool.h>

#include "panning.h"
#include "song.h"

#ifdef __cplusplus
extern "C" {
#endif

//! The amount of instruments in a song
#define LSDJ_INSTRUMENT_COUNT (0x40)

//! The amount of bytes an instrument takes
#define LSDJ_INSTRUMENT_BYTE_COUNT (16)

//! The amount of bytes an instrument name takes
#define LSDJ_INSTRUMENT_NAME_LENGTH (5)

//! The value of an infinite pulse length
#define LSDJ_INSTRUMENT_PULSE_LENGTH_INFINITE (0x40)

//! The value of a kit length set to AUTO
#define LSDJ_INSTRUMENT_KIT_LENGTH_AUTO (0x0)

	//! The value of an infinite noise length
#define LSDJ_INSTRUMENT_NOISE_LENGTH_INFINITE (0x40)

//! The kind of instrument types that exist
typedef enum
{
	LSDJ_INSTRUMENT_TYPE_PULSE = 0,
	LSDJ_INSTRUMENT_TYPE_WAVE,
	LSDJ_INSTRUMENT_TYPE_KIT,
	LSDJ_INSTRUMENT_TYPE_NOISE,
} lsdj_instrument_type_t;

typedef enum
{
	LSDJ_INSTRUMENT_TABLE_PLAY,
	LSDJ_INSTRUMENT_TABLE_STEP
} lsdj_instrument_table_mode;

typedef enum
{
	LSDJ_INSTRUMENT_WAVE_VOLUME_0 = 0x00,
	LSDJ_INSTRUMENT_WAVE_VOLUME_1 = 0x60,
	LSDJ_INSTRUMENT_WAVE_VOLUME_2 = 0x40,
	LSDJ_INSTRUMENT_WAVE_VOLUME_3 = 0xA8
} lsdj_instrument_wave_volume_t;

typedef enum
{
	LSDJ_INSTRUMENT_PULSE_WIDTH_125 = 0,
	LSDJ_INSTRUMENT_PULSE_WIDTH_25,
	LSDJ_INSTRUMENT_PULSE_WIDTH_50,
	LSDJ_INSTRUMENT_PULSE_WIDTH_75
} lsdj_instrument_pulse_width_t;

typedef enum
{
	LSDJ_INSTRUMENT_VIBRATO_TRIANGLE = 0,
	LSDJ_INSTRUMENT_VIBRATO_SAWTOOTH,
	LSDJ_INSTRUMENT_VIBRATO_SQUARE
} lsdj_vibrato_shape_t;

typedef enum
{
	LSDJ_INSTRUMENT_VIBRATO_DOWN = 0,
	LSDJ_INSTRUMENT_VIBRATO_UP
} lsdj_vibrato_direction_t;

typedef enum
{
	LSDJ_INSTRUMENT_PLV_FAST,
	LSDJ_INSTRUMENT_PLV_TICK,
	LSDJ_INSTRUMENT_PLV_STEP,
	LSDJ_INSTRUMENT_PLV_DRUM,
} lsdj_plv_speed_t;

typedef enum
{
	LSDJ_INSTRUMENT_WAVE_PLAY_ONCE = 0,
	LSDJ_INSTRUMENT_WAVE_PLAY_LOOP,
	LSDJ_INSTRUMENT_WAVE_PLAY_PING_PONG,
	LSDJ_INSTRUMENT_WAVE_PLAY_MANUAL,
} lsdj_wave_play_mode_t;

typedef enum
{
	LSDJ_INSTRUMENT_KIT_LOOP_OFF = 0,
	LSDJ_INSTRUMENT_KIT_LOOP_ON,
	LSDJ_INSTRUMENT_KIT_LOOP_ATTACK
} lsdj_kit_loop_mode_t;

typedef enum
{
	LSDJ_INSTRUMENT_KIT_DISTORTION_CLIP = 0,
	LSDJ_INSTRUMENT_KIT_DISTORTION_SHAPE,
	LSDJ_INSTRUMENT_KIT_DISTORTION_SHAPE2,
	LSDJ_INSTRUMENT_KIT_DISTORTION_WRAP,
} lsdj_kit_distortion_mode_t;

typedef enum
{
	LSDJ_INSTRUMENT_NOISE_FREE = 0,
	LSDJ_INSTRUMENT_NOISE_STABLE,
} lsdj_noise_stability_t;

//! Copy some bits over to a specific byte in an instrument
/*! @note You won't have to use this function if you just use the other instrument functions */
void set_instrument_bits(lsdj_song_t* song, uint8_t instrument, uint8_t byte, uint8_t position, uint8_t count, uint8_t value);

//! Copy some bits over from a specific byte in an instrument
/*! @note You won't have to use this function if you just use the other instrument functions */
uint8_t get_instrument_bits(const lsdj_song_t* song, uint8_t instrument, uint8_t byte, uint8_t position, uint8_t count);

//! Returns whether an instrument is in use
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return True if the instrument is in use */
bool lsdj_instrument_is_allocated(const lsdj_song_t* song, uint8_t instrument);

//! Change the name of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param name The name to set, null-terminated or LSDJ_INSTRUMENT_NAME_LENGTH at max */
void lsdj_instrument_set_name(lsdj_song_t* song, uint8_t instrument, const char* name);

//! Retrieve the name of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The name of the instrument, at maximum LSDJ_INSTRUMENT_NAME_LENGTH (may not be null-terminated) */
const char* lsdj_instrument_get_name(const lsdj_song_t* song, uint8_t instrument);

//! Change the type of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param type The type to set */
void lsdj_instrument_set_type(lsdj_song_t* song, uint8_t instrument, lsdj_instrument_type_t type);

//! Retrieve the type of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The type of the instrument */
lsdj_instrument_type_t lsdj_instrument_get_type(const lsdj_song_t* song, uint8_t instrument);

//! Change the envelope of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param envelope The envelope value to set
    @note Envelope is only supported on pulse and noise channels
    @note From 8.1.0+ (v11) envelope isn't used anymore. Undefined behaviour otherwise */
void lsdj_instrument_set_envelope(lsdj_song_t* song, uint8_t instrument, uint8_t envelope);

//! Retrieve the envelope of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @return The envelope of the instrument
    @note Envelope is only supported on pulse and noise channels
    @note From 8.1.0+ (v11) envelope isn't used anymore. Undefined behaviour otherwise */
uint8_t lsdj_instrument_get_envelope(const lsdj_song_t* song, uint8_t instrument);

//! Set the ADSR's initial level of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param level The initial level of the instrument (0 - F)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise */
void lsdj_instrument_adsr_set_initial_level(lsdj_song_t* song, uint8_t instrument, uint8_t level);

//! Retrieve the ADSR's initial level of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise
    @return The initial level of the instrument (0 - F) */
uint8_t lsdj_instrument_adsr_get_initial_level(const lsdj_song_t* song, uint8_t instrument);

//! Set the ADSR's attack speed of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param speed The attack speed of the instrument (0 - 7 for 8.1.0+; 0 - F for 8.8.0+, 0 = hold)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise */
void lsdj_instrument_adsr_set_attack_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed);

//! Retrieve the ADSR's attack speed of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise
    @return The attack speed of the instrument (0 - 7 for 8.1.0+; 0 - F for 8.8.0+, 0 = hold) */
uint8_t lsdj_instrument_adsr_get_attack_speed(const lsdj_song_t* song, uint8_t instrument);

//! Set the ADSR's attack level of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param level The attack level of the instrument (0 - F)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise */
void lsdj_instrument_adsr_set_attack_level(lsdj_song_t* song, uint8_t instrument, uint8_t level);

//! Retrieve the ADSR's attack level of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise
    @return The attack level of the instrument (0 - F) */
uint8_t lsdj_instrument_adsr_get_attack_level(const lsdj_song_t* song, uint8_t instrument);

//! Set the ADSR's decay speed of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param speed The decay speed of the instrument (0 - 7 for 8.1.0+; 0 - F for 8.8.0+, 0 = hold)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise */
void lsdj_instrument_adsr_set_decay_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed);

//! Retrieve the ADSR's decay speed of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise
    @return The decay speed of the instrument (0 - 7 for 8.1.0+; 0 - F for 8.8.0+, 0 = hold) */
uint8_t lsdj_instrument_adsr_get_decay_speed(const lsdj_song_t* song, uint8_t instrument);

//! Set the ADSR's sustain level of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param level The sustain level of the instrument (0 - F)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise */
void lsdj_instrument_adsr_set_sustain_level(lsdj_song_t* song, uint8_t instrument, uint8_t level);

//! Retrieve the ADSR's sustain level of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise
    @return The sustain level of the instrument (0 - F) */
uint8_t lsdj_instrument_adsr_get_sustain_level(const lsdj_song_t* song, uint8_t instrument);

//! Set the ADSR's release speed of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param speed The release speed of the instrument (0 - 7 for 8.1.0+; 0 - F for 8.8.0+, 0 = hold)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise */
void lsdj_instrument_adsr_set_release_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed);

//! Retrieve the ADSR's release speed of an instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @note ADSR is only supported on pulse and noise channels
    @note ADSR is only supported on 8.1.0 (v11) and higher. Undefined behaviour otherwise
    @return The release speed of the instrument (0 - 7 for 8.1.0+; 0 - F for 8.8.0+, 0 = hold) */
uint8_t lsdj_instrument_adsr_get_release_speed(const lsdj_song_t* song, uint8_t instrument);

//! Change the panning of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param panning The panning value to set */
void lsdj_instrument_set_panning(lsdj_song_t* song, uint8_t instrument, lsdj_panning_t panning);

//! Retrieve the panning of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The panning of the instrument */
lsdj_panning_t lsdj_instrument_get_panning(const lsdj_song_t* song, uint8_t instrument);

//! Change the transpose of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param transpose Whether the instrument transposes
	@note This only makes sense for pulse and wave instruments */
void lsdj_instrument_set_transpose(lsdj_song_t* song, uint8_t instrument, bool transpose);

//! Retrieve the transpose of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return Whether the instrument transposes
	@note This only makes sense for pulse and wave instruments*/
bool lsdj_instrument_get_transpose(const lsdj_song_t* song, uint8_t instrument);

//! Enable or disable the table of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param enabled Whether the table is enabled */
void lsdj_instrument_enable_table(lsdj_song_t* song, uint8_t instrument, bool enabled);

//! Ask whether the table field of an instrument is enabled
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return Whether the table is enabled */
bool lsdj_instrument_is_table_enabled(const lsdj_song_t* song, uint8_t instrument);

//! Change the table of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param table The table value to set or LSDJ_INSTRUMENT_NO_TABLE
	@note You should also call lsdj_instrument_enable_table() to enable/disable it */
void lsdj_instrument_set_table(lsdj_song_t* song, uint8_t instrument, uint8_t table);

//! Retrieve the table of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@note You should also call lsdj_instrument_enable_table() to find out if the table is enabled in the first place
	@return The table of the instrument or LSDJ_INSTRUMENT_NO_TABLE */
uint8_t lsdj_instrument_get_table(const lsdj_song_t* song, uint8_t instrument);

//! Change the table play mode of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param mode The play mode
	@note in earlier versions this was called AUTOMATE (turned on = step mode) */
void lsdj_instrument_set_table_mode(lsdj_song_t* song, uint8_t instrument, lsdj_instrument_table_mode mode);

//! Retrieve the table play mode of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The play mode
	@note in earlier versions this was called AUTOMATE (turned on = step mode) */
lsdj_instrument_table_mode lsdj_instrument_get_table_mode(const lsdj_song_t* song, uint8_t instrument);

//! Change the vibrato direction of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param direction The direction of the vibrato to write
	@note This only makes sense for pulse, wave and noise instruments */
void lsdj_instrument_set_vibrato_direction(lsdj_song_t* song, uint8_t instrument, lsdj_vibrato_direction_t direction);

//! Retrieve the vibrato direction of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The direction of the vibrato
	@note This only makes sense for pulse, wave and noise instruments*/
lsdj_vibrato_direction_t lsdj_instrument_get_vibrato_direction(const lsdj_song_t* song, uint8_t instrument);

//! Change the vibrato shape and P/L/V speed of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param shape The shape of the vibrato to write

	In versions before 5.7.0 (fmt4) only certain combinations of shape and speed exist, which there only
	exists a function taking both. If the combination provided by you didn't exist, this function returns
	bool.

	@note This only makes sense for pulse, wave and noise instruments */
bool lsdj_instrument_set_vibrato_shape_and_plv_speed(lsdj_song_t* song, uint8_t instrument, lsdj_vibrato_shape_t shape, lsdj_plv_speed_t speed);

//! Retrieve the vibrato shape of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The shape of the vibrato
	@note This only makes sense for pulse, wave and noise instruments*/
lsdj_vibrato_shape_t lsdj_instrument_get_vibrato_shape(const lsdj_song_t* song, uint8_t instrument);

//! Retrieve the P/L/V speed setting of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The speed setting of P/L/V commands
	@note This only makes sense for pulse, wave and noise instruments*/
lsdj_plv_speed_t lsdj_instrument_get_plv_speed(const lsdj_song_t* song, uint8_t instrument);

//! Change the command rate of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param rate The command rate
	@note Kit instruments don't have a command rate */
void lsdj_instrument_set_command_rate(lsdj_song_t* song, uint8_t instrument, uint8_t rate);

//! Retrieve the command rate of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The command rate of the instrument
	@note Kit instruments don't have a command rate */
uint8_t lsdj_instrument_get_command_rate(const lsdj_song_t* song, uint8_t instrument);


// --- Pulse --- //

//! Change the pulse width of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param pulse width The pulse width to set
	@note Calling this only makes sense if the instrument is a pulse instrument */
void lsdj_instrument_pulse_set_pulse_width(lsdj_song_t* song, uint8_t instrument, lsdj_instrument_pulse_width_t pulseWidth);

//! Retrieve the pulse width of an instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The pulse width of the instrument
	@note Calling this only makes sense if the instrument is a pulse instrument */
lsdj_instrument_pulse_width_t lsdj_instrument_pulse_get_pulse_width(const lsdj_song_t* song, uint8_t instrument);

//! Change the length of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param length The length value to set
    @note Length is available up to v8.8.0 (v13), otherwise undefined behaviour */
void lsdj_instrument_pulse_set_length(lsdj_song_t* song, uint8_t instrument, uint8_t length);

//! Retrieve the length of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The length of the instrument
    @note Length is available up to v8.8.0 (v13), otherwise undefined behaviour */
uint8_t lsdj_instrument_pulse_get_length(const lsdj_song_t* song, uint8_t instrument);

//! Change the sweep of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param sweep The sweep value to set */
void lsdj_instrument_pulse_set_sweep(lsdj_song_t* song, uint8_t instrument, uint8_t sweep);

//! Retrieve the sweep of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The sweep of the instrument */
uint8_t lsdj_instrument_pulse_get_sweep(const lsdj_song_t* song, uint8_t instrument);

//! Change the pulse2 tune of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param tune The tune value to set */
void lsdj_instrument_pulse_set_pulse2_tune(lsdj_song_t* song, uint8_t instrument, uint8_t tune);

//! Retrieve the pulse2 tune of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The pulse2 tune of the instrument */
uint8_t lsdj_instrument_pulse_get_pulse2_tune(const lsdj_song_t* song, uint8_t instrument);

//! Change the finetune of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param finetune The finetune value to set */
void lsdj_instrument_pulse_set_finetune(lsdj_song_t* song, uint8_t instrument, uint8_t finetune);

//! Retrieve the finetune of a pulse instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The finetune of the instrument */
uint8_t lsdj_instrument_pulse_get_finetune(const lsdj_song_t* song, uint8_t instrument);


// --- Wave --- //

//! Change the volume of a wave instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param volume The envelope value to set
    @note Wave and kit instruments only supports 3 volume levels, which are defined in lsdj_instrument_wave_volume_t */
void lsdj_instrument_wave_set_volume(lsdj_song_t* song, uint8_t instrument, uint8_t volume);

//! Retrieve the volume of a wave instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @return The volume of the instrument
    @note Wave and kit instruments only supports 3 volume levels, which are defined in lsdj_instrument_wave_volume_t */
uint8_t lsdj_instrument_wave_get_volume(const lsdj_song_t* song, uint8_t instrument);

//! Change the synth index that a wave instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param synth The synth the instrument uses (0 - F) */
void lsdj_instrument_wave_set_synth(lsdj_song_t* song, uint8_t instrument, uint8_t synth);

//! Retrieve the synth index that a wave instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The synth the instrument uses (0 - F) */
uint8_t lsdj_instrument_wave_get_synth(const lsdj_song_t* song, uint8_t instrument);

//! Change the wave index that a wave instrument starts from
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param wave The start wavetable the instrument uses (0 - FF)
    @note This setting only makes sense when the instrument is set to manual */
void lsdj_instrument_wave_set_wave(lsdj_song_t* song, uint8_t instrument, uint8_t wave);

//! Retrieve the wave index that a wave instrument starts from
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @return The wave the instrument uses (0 - FF)
    @note This setting only makes sense when the instrument is set to manual */
uint8_t lsdj_instrument_wave_get_wave(const lsdj_song_t* song, uint8_t instrument);

//! Change the play mode for a wave instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param play_mode The play mode */
void lsdj_instrument_wave_set_play_mode(lsdj_song_t* song, uint8_t instrument, lsdj_wave_play_mode_t mode);

//! Retrieve the play mode that a wave instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The play mode */
lsdj_wave_play_mode_t lsdj_instrument_wave_get_play_mode(const lsdj_song_t* song, uint8_t instrument);

//! Change the length value for a wave instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param length The length value (0 - F) */
void lsdj_instrument_wave_set_length(lsdj_song_t* song, uint8_t instrument, uint8_t length);

//! Retrieve the length value that a wave instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The length value (0 - F) */
uint8_t lsdj_instrument_wave_get_length(const lsdj_song_t* song, uint8_t instrument);

//! Change the loop pos for a wave instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param pos The loop pos value (0 - F)
    @note This doesn't make sense before fmt v9, in which case you should use lsdj_instrument_wave_set_repeat() */
void lsdj_instrument_wave_set_loop_pos(lsdj_song_t* song, uint8_t instrument, uint8_t pos);

//! Retrieve the loop pos value that a wave instrument uses
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @return The loop pos value (0 - F)
    @note This doesn't make sense before fmt v9, in which case you should use lsdj_instrument_wave_get_repeat() */
uint8_t lsdj_instrument_wave_get_loop_pos(const lsdj_song_t* song, uint8_t pos);

//! Change the repeat value for a wave instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param repeat The repeat value (0 - F)
    @note This doesn't make sense since fmt v9, in which case you should use lsdj_instrument_wave_set_loop_pos() */
void lsdj_instrument_wave_set_repeat(lsdj_song_t* song, uint8_t instrument, uint8_t repeat);

//! Retrieve the repeat value that a wave instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The repeat value (0 - F)
    @note This doesn't make sense since fmt v9, in which case you should use lsdj_instrument_wave_get_loop_pos() */
uint8_t lsdj_instrument_wave_get_repeat(const lsdj_song_t* song, uint8_t instrument);

//! Change the speed value for a wave instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param speed The speed value (1 - FF for fmt 6+, 1-F below fmt6)
	@note < fmt6 speed could only go up to F, in fmt6 and higher the max is FF
	@return false If the format version does not support that speed value*/
bool lsdj_instrument_wave_set_speed(lsdj_song_t* song, uint8_t instrument, uint8_t speed);

//! Retrieve the speed index that a wave instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The speed value (0 - F) */
uint8_t lsdj_instrument_wave_get_speed(const lsdj_song_t* song, uint8_t instrument);


// --- Kit --- //

//! Change the volume of a kit instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @param volume The envelope value to set
    @note Wave and kit instruments only supports 3 volume levels, which are defined in lsdj_instrument_wave_volume_t */
void lsdj_instrument_kit_set_volume(lsdj_song_t* song, uint8_t instrument, uint8_t volume);

//! Retrieve the volume of a kit instrument
/*! @param song The song that contains the instrument
    @param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
    @return The volume of the instrument
    @note Wave and kit instruments only supports 3 volume levels, which are defined in lsdj_instrument_wave_volume_t */
uint8_t lsdj_instrument_kit_get_volume(const lsdj_song_t* song, uint8_t instrument);

//! Change the pitch for a kit instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param pitch The pitch */
void lsdj_instrument_kit_set_pitch(lsdj_song_t* song, uint8_t instrument, uint8_t pitch);

//! Retrieve the pitch that a kit instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The pitch */
uint8_t lsdj_instrument_kit_get_pitch(const lsdj_song_t* song, uint8_t instrument);

//! Change whether a kit instrument runs at half speed
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param halfSpeed Whether the instrument should be half speed */
void lsdj_instrument_kit_set_half_speed(lsdj_song_t* song, uint8_t instrument, bool halfSpeed);

//! Ask whether a kit instrument runs at half speed
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return Whether the instrument runs at half speed */
bool lsdj_instrument_kit_get_half_speed(const lsdj_song_t* song, uint8_t instrument);

//! Change the distortion mode for a kit instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param distortion The distortion mode */
void lsdj_instrument_kit_set_distortion_mode(lsdj_song_t* song, uint8_t instrument, lsdj_kit_distortion_mode_t distortion);

//! Retrieve the distortion mode that a kit instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The distortion mode */
lsdj_kit_distortion_mode_t lsdj_instrument_kit_get_distortion_mode(const lsdj_song_t* song, uint8_t instrument);

//! Change the first kit for a kit instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param kit The kit of the first kit */
void lsdj_instrument_kit_set_kit1(lsdj_song_t* song, uint8_t instrument, uint8_t kit);

//! Retrieve the first kit that a kit instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The kit of the first kit */
uint8_t lsdj_instrument_kit_get_kit1(const lsdj_song_t* song, uint8_t instrument);

//! Change the second kit for a kit instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param kit The kit of the set second kit */
void lsdj_instrument_kit_set_kit2(lsdj_song_t* song, uint8_t instrument, uint8_t kit);

//! Retrieve the second kit that a kit instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The kit of the second kit */
uint8_t lsdj_instrument_kit_get_kit2(const lsdj_song_t* song, uint8_t instrument);

//! Change the offset of the first kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param offset The offset of the first kit */
void lsdj_instrument_kit_set_offset1(lsdj_song_t* song, uint8_t instrument, uint8_t offset);

//! Retrieve the offset of the first kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The offset of the first kit */
uint8_t lsdj_instrument_kit_get_offset1(const lsdj_song_t* song, uint8_t instrument);

//! Change the offset of the second kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param offset The offset of the set second kit */
void lsdj_instrument_kit_set_offset2(lsdj_song_t* song, uint8_t instrument, uint8_t offset);

//! Retrieve the offset of the second kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The offset of the second kit */
uint8_t lsdj_instrument_kit_get_offset2(const lsdj_song_t* song, uint8_t instrument);

//! Change the length of the first kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param length The length of the first kit */
void lsdj_instrument_kit_set_length1(lsdj_song_t* song, uint8_t instrument, uint8_t length);

//! Retrieve the length of the first kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The length of the first kit */
uint8_t lsdj_instrument_kit_get_length1(const lsdj_song_t* song, uint8_t instrument);

//! Change the length of the second kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param length The length of the set second kit */
void lsdj_instrument_kit_set_length2(lsdj_song_t* song, uint8_t instrument, uint8_t length);

//! Retrieve the length of the second kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The length of the second kit */
uint8_t lsdj_instrument_kit_get_length2(const lsdj_song_t* song, uint8_t instrument);

//! Change the loop of the first kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param loop The loop of the first kit */
void lsdj_instrument_kit_set_loop1(lsdj_song_t* song, uint8_t instrument, lsdj_kit_loop_mode_t loop);

//! Retrieve the loop of the first kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The loop of the first kit */
lsdj_kit_loop_mode_t lsdj_instrument_kit_get_loop1(const lsdj_song_t* song, uint8_t instrument);

//! Change the loop of the second kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param loop The loop of the set second kit */
void lsdj_instrument_kit_set_loop2(lsdj_song_t* song, uint8_t instrument, lsdj_kit_loop_mode_t loop);

//! Retrieve the loop of the second kit
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The loop of the second kit */
lsdj_kit_loop_mode_t lsdj_instrument_kit_get_loop2(const lsdj_song_t* song, uint8_t instrument);


// --- Noise --- //

//! Change the length of a noise instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param length The length value to set
    @note Length is available up to v8.8.0 (v13), otherwise undefined behaviour */
void lsdj_instrument_noise_set_length(lsdj_song_t* song, uint8_t instrument, uint8_t length);

//! Retrieve the length of a noise instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The length of the instrument
    @note Length is available up to v8.8.0 (v13), otherwise undefined behaviour */
uint8_t lsdj_instrument_noise_get_length(const lsdj_song_t* song, uint8_t instrument);

//! Change the shape for a noise instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param shape The shape
    @note shape is available up to v8.6.0 (v12), otherwise undefined behaviour */
void lsdj_instrument_noise_set_shape(lsdj_song_t* song, uint8_t instrument, uint8_t shape);

//! Retrieve the shape that a noise instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The shape
    @note shape is available up to v8.6.0 (v12), otherwise undefined behaviour */
uint8_t lsdj_instrument_noise_get_shape(const lsdj_song_t* song, uint8_t instrument);

//! Change the stability for a noise instrument
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@param stability The stability */
void lsdj_instrument_noise_set_stability(lsdj_song_t* song, uint8_t instrument, lsdj_noise_stability_t stability);

//! Retrieve the stability that a noise instrument uses
/*! @param song The song that contains the instrument
	@param instrument The index of the instrument (< LSDJ_INSTRUMENT_COUNT)
	@return The stability */
lsdj_noise_stability_t lsdj_instrument_noise_get_stability(const lsdj_song_t* song, uint8_t instrument);
    
#ifdef __cplusplus
}
#endif

#endif
