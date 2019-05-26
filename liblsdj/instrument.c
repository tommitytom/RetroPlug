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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "instrument.h"

// Structure representing one instrument
typedef struct lsdj_instrument_t
{
    char name[LSDJ_INSTRUMENT_NAME_LENGTH];
    
    instrument_type type;
    
    union { unsigned char envelope; unsigned char volume; };
    lsdj_panning panning;
    unsigned char table; // 0x20 or higher = LSDJ_NO_TABLE
    unsigned char automate;
    
    union
    {
        lsdj_instrument_pulse_t pulse;
        lsdj_instrument_wave_t wave;
        lsdj_instrument_kit_t kit;
        lsdj_instrument_noise_t noise;
    };
} lsdj_instrument_t;

lsdj_instrument_t* lsdj_instrument_new()
{
    lsdj_instrument_t* instrument = malloc(sizeof(lsdj_instrument_t));
    lsdj_instrument_clear(instrument);
    return instrument;
}

lsdj_instrument_t* lsdj_instrument_copy(const lsdj_instrument_t* instrument)
{
    lsdj_instrument_t* newInstrument = malloc(sizeof(lsdj_instrument_t));
    memcpy(newInstrument, instrument, sizeof(lsdj_instrument_t));
    return newInstrument;
}

void lsdj_instrument_free(lsdj_instrument_t* instrument)
{
    free(instrument);
}

void lsdj_instrument_clear(lsdj_instrument_t* instrument)
{
    memset(instrument->name, 0, LSDJ_INSTRUMENT_NAME_LENGTH);
    lsdj_instrument_clear_as_pulse(instrument);
}

void lsdj_instrument_clear_as_pulse(lsdj_instrument_t* instrument)
{
    instrument->type = LSDJ_INSTR_PULSE;
    instrument->envelope = 0xA8;
    instrument->panning = LSDJ_PAN_LEFT_RIGHT;
    instrument->table = LSDJ_NO_TABLE;
    instrument->automate = 0;
    
    instrument->pulse.pulseWidth = LSDJ_PULSE_WAVE_PW_125;
    instrument->pulse.length = LSDJ_INSTRUMENT_UNLIMITED_LENGTH;
    instrument->pulse.sweep = 0xFF;
    instrument->pulse.plvibSpeed = LSDJ_PLVIB_FAST;
    instrument->pulse.vibShape = LSDJ_VIB_TRIANGLE;
    instrument->pulse.vibratoDirection = LSDJ_VIB_UP;
    instrument->pulse.transpose = 1;
    instrument->pulse.drumMode = 0;
    instrument->pulse.pulse2tune = 0;
    instrument->pulse.fineTune = 0;
}

void lsdj_instrument_clear_as_wave(lsdj_instrument_t* instrument)
{
    instrument->type = LSDJ_INSTR_WAVE;
    instrument->volume = 3;
    instrument->panning = LSDJ_PAN_LEFT_RIGHT;
    instrument->table = LSDJ_NO_TABLE;
    instrument->automate = 0;
    
    instrument->wave.plvibSpeed = LSDJ_PLVIB_FAST;
    instrument->wave.vibShape = LSDJ_VIB_TRIANGLE;
    instrument->wave.vibratoDirection = LSDJ_VIB_UP;
    instrument->wave.transpose = 1;
    instrument->wave.drumMode = 0;
    instrument->wave.synth = 0;
    instrument->wave.playback = LSDJ_PLAY_ONCE;
    instrument->wave.length = 0x0F;
    instrument->wave.repeat = 0;
    instrument->wave.speed = 4;
}

void lsdj_instrument_clear_as_kit(lsdj_instrument_t* instrument)
{
    instrument->type = LSDJ_INSTR_KIT;
    instrument->volume = 3;
    instrument->panning = LSDJ_PAN_LEFT_RIGHT;
    instrument->table = LSDJ_NO_TABLE;
    instrument->automate = 0;
    
    instrument->kit.kit1 = 0;
    instrument->kit.offset1 = 0;
    instrument->kit.length1 = LSDJ_KIT_LENGTH_AUTO;
    instrument->kit.loop1 = LSDJ_KIT_LOOP_OFF;
    
    instrument->kit.kit2 = 0;
    instrument->kit.offset2 = 0;
    instrument->kit.length2 = LSDJ_KIT_LENGTH_AUTO;
    instrument->kit.loop2 = LSDJ_KIT_LOOP_OFF;
    
    instrument->kit.pitch = 0;
    instrument->kit.halfSpeed = 0;
    instrument->kit.distortion = LSDJ_KIT_DIST_CLIP;
    instrument->kit.plvibSpeed = LSDJ_PLVIB_FAST;
    instrument->kit.vibShape = LSDJ_VIB_TRIANGLE;
}

void lsdj_instrument_clear_as_noise(lsdj_instrument_t* instrument)
{
    instrument->type = LSDJ_INSTR_NOISE;
    instrument->envelope = 0xA8;
    instrument->panning = LSDJ_PAN_LEFT_RIGHT;
    instrument->table = LSDJ_NO_TABLE;
    instrument->automate = 0;
    
    instrument->noise.length = LSDJ_INSTRUMENT_UNLIMITED_LENGTH;
    instrument->noise.shape = 0xFF;
    instrument->noise.sCommand = LSDJ_SCOMMAND_FREE;
}

// --- Reading --- //

unsigned char parseLength(unsigned char byte)
{
    if (byte & 0x40)
        return (~byte) & 0x3F;
    else
        return LSDJ_INSTRUMENT_UNLIMITED_LENGTH;
}

unsigned char parseTable(unsigned char byte)
{
    if (byte & 0x20)
        return byte & 0x1F;
    else
        return LSDJ_NO_TABLE;
}

lsdj_panning parsePanning(unsigned char byte)
{
    return byte & 3;
}

char parseDrumMode(unsigned char byte, unsigned char version)
{
    if (version < 3)
        return 0;
    else
        return (byte & 0x40) ? 1 : 0;
}

char parseTranspose(unsigned char byte, unsigned char version)
{
    if (version < 3)
        return 0;
    else
        return (byte & 0x20) ? 0 : 1;
}

unsigned char parseAutomate(unsigned char byte)
{
    return (byte >> 3) & 0x1;
}

lsdj_pulse_wave parsePulseWidth(unsigned char byte)
{
    return (byte >> 6) & 0x3;
}

lsdj_playback_mode parsePlaybackMode(unsigned char byte)
{
    return byte & 0x3;
}

lsdj_kit_distortion parseKitDistortion(unsigned char byte)
{
    return byte;
}

lsdj_scommand_type parseScommand(unsigned char byte)
{
    return byte & 1;
}

void read_pulse_instrument(lsdj_vio_t* vio, unsigned char version, lsdj_instrument_t* instrument, lsdj_error_t** error)
{
    instrument->type = LSDJ_INSTR_PULSE; // 0
    
    vio->read(&instrument->envelope, 1, vio->user_data); // 1
    
    vio->read(&instrument->pulse.pulse2tune, 1, vio->user_data); // 2
    
    // 3
    unsigned char byte;
    vio->read(&byte, 1, vio->user_data);
    instrument->pulse.length = parseLength(byte);
    
    // 4
    vio->read(&instrument->pulse.sweep, 1, vio->user_data);
    
    // 5
    vio->read(&byte, 1, vio->user_data);
    instrument->pulse.drumMode = parseDrumMode(byte, version);
    instrument->pulse.transpose = parseTranspose(byte, version);
    instrument->automate = parseAutomate(byte);
    instrument->pulse.vibratoDirection = byte & 1;
    
    if (version < 4)
    {
        switch ((byte >> 1) & 3)
        {
            case 0:
                instrument->pulse.plvibSpeed = LSDJ_PLVIB_FAST;
                instrument->pulse.vibShape = LSDJ_VIB_TRIANGLE;
                break;
            case 1:
                instrument->pulse.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->pulse.vibShape = LSDJ_VIB_SAWTOOTH;
                break;
            case 2:
                instrument->pulse.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->pulse.vibShape = LSDJ_VIB_TRIANGLE;
                break;
            case 3:
                instrument->pulse.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->pulse.vibShape = LSDJ_VIB_SQUARE;
                break;
        }
    } else {
        if (byte & 0x80)
            instrument->pulse.plvibSpeed = LSDJ_PLVIB_STEP;
        else if (byte & 0x10)
            instrument->pulse.plvibSpeed = LSDJ_PLVIB_TICK;
        else
            instrument->pulse.plvibSpeed = LSDJ_PLVIB_FAST;
        
        switch ((byte >> 1) & 3)
        {
            case 0: instrument->pulse.vibShape = LSDJ_VIB_TRIANGLE; break;
            case 1: instrument->pulse.vibShape = LSDJ_VIB_SAWTOOTH; break;
            case 2: instrument->pulse.vibShape = LSDJ_VIB_SQUARE; break;
        }
    }
    
    vio->read(&byte, 1, vio->user_data);
    instrument->table = parseTable(byte);
    
    vio->read(&byte, 1, vio->user_data);
    instrument->pulse.pulseWidth = parsePulseWidth(byte);
    instrument->pulse.fineTune = ((byte >> 2) & 0xF);
    instrument->panning = parsePanning(byte);
    
//    unsigned char data[8];
//    read(data, 8, vio->user_data);
    vio->seek(8, SEEK_CUR, vio->user_data); // Bytes 8-15 are empty
}

void read_wave_instrument(lsdj_vio_t* vio, unsigned char version, lsdj_instrument_t* instrument, lsdj_error_t** error)
{
    instrument->type = LSDJ_INSTR_WAVE;
    vio->read(&instrument->volume, 1, vio->user_data);
    
    unsigned char byte;
    vio->read(&byte, 1, vio->user_data);
    instrument->wave.synth = (byte >> 4) & 0xF;
    instrument->wave.repeat = (byte & 0xF);
    vio->seek(2, SEEK_CUR, vio->user_data); // Bytes 3 and 4 are empty
    
    vio->read(&byte, 1, vio->user_data);
    instrument->wave.drumMode = parseDrumMode(byte, version);
    instrument->wave.transpose = parseTranspose(byte, version);
    instrument->automate = parseAutomate(byte);
    instrument->wave.vibratoDirection = byte & 1;
    
    if (version < 4)
    {
        switch ((byte >> 1) & 3)
        {
            case 0:
                instrument->wave.plvibSpeed = LSDJ_PLVIB_FAST;
                instrument->wave.vibShape = LSDJ_VIB_TRIANGLE;
                break;
            case 1:
                instrument->wave.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->wave.vibShape = LSDJ_VIB_SAWTOOTH;
                break;
            case 2:
                instrument->wave.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->wave.vibShape = LSDJ_VIB_TRIANGLE;
                break;
            case 3:
                instrument->wave.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->wave.vibShape = LSDJ_VIB_SQUARE;
                break;
        }
    } else {
        if (byte & 0x80)
            instrument->wave.plvibSpeed = LSDJ_PLVIB_STEP;
        else if (byte & 0x10)
            instrument->wave.plvibSpeed = LSDJ_PLVIB_TICK;
        else
            instrument->wave.plvibSpeed = LSDJ_PLVIB_FAST;
        
        switch ((byte >> 1) & 3)
        {
            case 0: instrument->wave.vibShape = LSDJ_VIB_TRIANGLE; break;
            case 1: instrument->wave.vibShape = LSDJ_VIB_SAWTOOTH; break;
            case 2: instrument->wave.vibShape = LSDJ_VIB_SQUARE; break;
        }
        
        instrument->kit.vibratoDirection = (byte & 1) == 1 ? LSDJ_VIB_UP : LSDJ_VIB_DOWN;
    }
    
    vio->read(&byte, 1, vio->user_data);
    instrument->table = parseTable(byte);
    
    vio->read(&byte, 1, vio->user_data);
    instrument->panning = parsePanning(byte);
    
    vio->seek(1, SEEK_CUR, vio->user_data); // Byte 8 is empty
    
    vio->read(&byte, 1, vio->user_data);
    instrument->wave.playback = parsePlaybackMode(byte);
    
    // WAVE length and speed changed in version 6
    if (version >= 7)
    {
        vio->read(&byte, 1, vio->user_data); // Byte 10
        instrument->wave.length = 0xF - (byte & 0xF);
        
        vio->read(&byte, 1, vio->user_data); // Byte 11
        instrument->wave.speed = byte + 4;
    }
    else if (version == 6)
    {
        vio->read(&byte, 1, vio->user_data); // Byte 10
        instrument->wave.length = (byte & 0xF);
        
        vio->read(&byte, 1, vio->user_data); // Byte 11
        instrument->wave.speed = byte + 1;
    } else {
        vio->seek(2, SEEK_CUR, vio->user_data); // Bytes 12-13 are empty
    }
    
    vio->seek(2, SEEK_CUR, vio->user_data); // Bytes 10-13 are empty
    
    vio->read(&byte, 1, vio->user_data);
    
    // WAVE length and speed changed in version 6
    if (version < 6)
    {
        instrument->wave.length = ((byte >> 4) & 0xF);
        instrument->wave.speed = (byte & 0xF) + 1;
    }
    
    vio->seek(1, SEEK_CUR, vio->user_data); // Byte 15 is empty
}

void read_kit_instrument(lsdj_vio_t* vio, unsigned char version, lsdj_instrument_t* instrument, lsdj_error_t** error)
{
    instrument->type = LSDJ_INSTR_KIT;
    vio->read(&instrument->volume, 1, vio->user_data);
    
    instrument->kit.loop1 = LSDJ_KIT_LOOP_OFF;
    instrument->kit.loop2 = LSDJ_KIT_LOOP_OFF;
    
    unsigned char byte;
    vio->read(&byte, 1, vio->user_data);
    if ((byte >> 7) & 1)
        instrument->kit.loop1 = LSDJ_KIT_LOOP_ATTACK;
    instrument->kit.halfSpeed = (byte >> 6) & 1;
    instrument->kit.kit1 = byte & 0x3F;
    vio->read(&instrument->kit.length1, 1, vio->user_data);
    
    vio->seek(1, SEEK_CUR, vio->user_data); // Byte 4 is empty
    
    // Byte 5
    vio->read(&byte, 1, vio->user_data);
    if (instrument->kit.loop1 != LSDJ_KIT_LOOP_ATTACK)
        instrument->kit.loop1 = (byte & 0x40) ? LSDJ_KIT_LOOP_ON : LSDJ_KIT_LOOP_OFF;
    instrument->kit.loop2 = (byte & 0x20) ? LSDJ_KIT_LOOP_ON : LSDJ_KIT_LOOP_OFF;
    instrument->automate = parseAutomate(byte);
    
    instrument->kit.vibratoDirection = byte & 1;
    
    if (version < 4)
    {
        switch ((byte >> 1) & 3)
        {
            case 0:
                instrument->kit.plvibSpeed = LSDJ_PLVIB_FAST;
                instrument->kit.vibShape = LSDJ_VIB_TRIANGLE;
                break;
            case 1:
                instrument->kit.plvibSpeed = LSDJ_PLVIB_TICK;
                instrument->kit.vibShape = LSDJ_VIB_TRIANGLE;
                break;
            case 2:
                instrument->kit.plvibSpeed = LSDJ_PLVIB_STEP;
                instrument->kit.vibShape = LSDJ_VIB_TRIANGLE;
                break;
        }
    } else {
        if (byte & 0x80)
            instrument->kit.plvibSpeed = LSDJ_PLVIB_STEP;
        else if (byte & 0x10)
            instrument->kit.plvibSpeed = LSDJ_PLVIB_TICK;
        else
            instrument->kit.plvibSpeed = LSDJ_PLVIB_FAST;
        
        switch ((byte >> 1) & 3)
        {
            case 0: instrument->kit.vibShape = LSDJ_VIB_TRIANGLE; break;
            case 1: instrument->kit.vibShape = LSDJ_VIB_SAWTOOTH; break;
            case 2: instrument->kit.vibShape = LSDJ_VIB_SQUARE; break;
        }
    }
    
    // byte 6
    vio->read(&byte, 1, vio->user_data);
    instrument->table = parseTable(byte);
    
    // byte 7
    vio->read(&byte, 1, vio->user_data);
    instrument->panning = parsePanning(byte);
    
    // byte 8
    vio->read(&instrument->kit.pitch, 1, vio->user_data);
    
    // byte 9
    vio->read(&byte, 1, vio->user_data);
    if ((byte >> 7) & 1)
        instrument->kit.loop2 = LSDJ_KIT_LOOP_ATTACK;
    instrument->kit.kit2 = byte & 0x3F;
    
    // byte 10
    vio->read(&byte, 1, vio->user_data);
    instrument->kit.distortion = parseKitDistortion(byte);
    
    vio->read(&instrument->kit.length2, 1, vio->user_data); // byte 11
    vio->read(&instrument->kit.offset1, 1, vio->user_data); // byte 12
    vio->read(&instrument->kit.offset2, 1, vio->user_data); // byte 13
    
    vio->seek(2, SEEK_CUR, vio->user_data); // Bytes 14 and 15 are empty
}

void read_noise_instrument(lsdj_vio_t* vio, lsdj_instrument_t* instrument, lsdj_error_t** error)
{
    instrument->type = LSDJ_INSTR_NOISE;
    vio->read(&instrument->envelope, 1, vio->user_data);
    
    unsigned char byte;
    vio->read(&byte, 1, vio->user_data);
    instrument->noise.sCommand = parseScommand(byte);
    
    vio->read(&byte, 1, vio->user_data);
    instrument->noise.length = parseLength(byte);
    
    vio->read(&instrument->noise.shape, 1, vio->user_data);
    
    vio->read(&byte, 1, vio->user_data);
    instrument->automate = parseAutomate(byte);
    
    vio->read(&byte, 1, vio->user_data);
    instrument->table = parseTable(byte);
    
    vio->read(&byte, 1, vio->user_data);
    instrument->panning = parsePanning(byte);
    
    vio->seek(8, SEEK_CUR, vio->user_data); // Bytes 8-15 are empty
}

void lsdj_instrument_read(lsdj_vio_t* vio, unsigned char version, lsdj_instrument_t* instrument, lsdj_error_t** error)
{
    if (vio->read == NULL)
        return lsdj_error_new(error, "read is NULL");
    
    if (vio->seek == NULL)
        return lsdj_error_new(error, "seek is NULL");
    
    if (instrument == NULL)
        return lsdj_error_new(error, "instrument is NULL");
    
    unsigned char type;
    vio->read(&type, 1, vio->user_data);
    
    const long pos = vio->tell(vio->user_data);
    
    switch (type)
    {
        case 0: read_pulse_instrument(vio, version, instrument, error); break;
        case 1: read_wave_instrument(vio, version, instrument, error); break;
        case 2: read_kit_instrument(vio, version, instrument, error); break;
        case 3: read_noise_instrument(vio, instrument, error); break;
        default: return lsdj_error_new(error, "unknown instrument type");
    }
    
    assert(vio->tell(vio->user_data) - pos == 15);
}

unsigned char createWaveVolumeByte(unsigned char volume)
{
    return volume;
}

unsigned char createPanningByte(lsdj_panning pan)
{
    return pan & 3;
}

unsigned char createLengthByte(unsigned char length)
{
    if (length >= LSDJ_INSTRUMENT_UNLIMITED_LENGTH)
        return 0;
    else
        return (~length & 0x3F) | 0x40;
}

unsigned char createTableByte(unsigned char table)
{
    if (table >= LSDJ_NO_TABLE)
        return 0;
    else
        return (table & 0x1F) | 0x20;
}

unsigned char createAutomateByte(unsigned char automate)
{
    return (automate == 0) ? 0x0 : 0x8;
}

unsigned char createDrumModeByte(char drumMode, unsigned char version)
{
    if (version < 3)
        return 0;
    else
        return (drumMode == 1) ? 0x40 : 0x0;
}

unsigned char createTransposeByte(char transpose, unsigned char version)
{
    if (version < 3)
        return 0;
    else
        return (transpose == 1) ? 0x0 : 0x20;
}

unsigned char createVibrationDirectionByte(lsdj_vib_direction dir)
{
    return dir & 1;
}

unsigned char createPulseWidthByte(lsdj_pulse_wave pw)
{
    return (unsigned char)((pw & 3) << 6);
}

unsigned char createPlaybackModeByte(lsdj_playback_mode play)
{
    return play & 3;
}

unsigned char createKitDistortionByte(lsdj_kit_distortion dist)
{
    return dist;
}

unsigned char createScommandByte(lsdj_scommand_type type)
{
    return type & 1;
}

void write_pulse_instrument(const lsdj_instrument_t* instrument, unsigned char version, lsdj_vio_t* vio)
{
    unsigned char byte = 0;
    vio->write(&byte, 1, vio->user_data);
    vio->write(&instrument->envelope, 1, vio->user_data);
    vio->write(&instrument->pulse.pulse2tune, 1, vio->user_data);
    
    byte = createLengthByte(instrument->pulse.length);
    vio->write(&byte, 1, vio->user_data);
    vio->write(&instrument->pulse.sweep, 1, vio->user_data);
    
    byte = createDrumModeByte(instrument->pulse.drumMode, version);
    byte |= createTransposeByte(instrument->pulse.transpose, version);
    byte |= createAutomateByte(instrument->automate);
    byte |= createVibrationDirectionByte(instrument->pulse.vibratoDirection);
    
    if (version < 4)
    {
        switch (instrument->pulse.vibShape)
        {
            case LSDJ_VIB_SAWTOOTH: byte |= 2; break;
            case LSDJ_VIB_SQUARE: byte |= 6; break;
            case LSDJ_VIB_TRIANGLE:
                if (instrument->pulse.plvibSpeed != LSDJ_PLVIB_FAST)
                    byte |= 4;
                break;
        }
    } else {
        byte |= (instrument->pulse.vibShape & 3) << 1;
        if (instrument->pulse.plvibSpeed == LSDJ_PLVIB_TICK)
            byte |= 0x10;
        else if (instrument->pulse.plvibSpeed == LSDJ_PLVIB_STEP)
            byte |= 0x80;
    }
    
    vio->write(&byte, 1, vio->user_data);
    
    byte = createTableByte(instrument->table);
    vio->write(&byte, 1, vio->user_data);
    
    byte = (unsigned char)(createPulseWidthByte(instrument->pulse.pulseWidth) | ((instrument->pulse.fineTune & 0xF) << 2) | createPanningByte(instrument->panning));
    vio->write(&byte, 1, vio->user_data);
    
    static unsigned char empty[8] = { 0, 0, 0xD0, 0, 0, 0, 0xF3, 0 };
    vio->write(empty, sizeof(empty), vio->user_data); // Bytes 8-15 are empty
}

void write_wave_instrument(const lsdj_instrument_t* instrument, unsigned char version, lsdj_vio_t* vio)
{
    // Byte 0
    unsigned char byte = 1;
    vio->write(&byte, 1, vio->user_data);
    
    // Byte 1
    byte = createWaveVolumeByte(instrument->volume);
    vio->write(&byte, 1, vio->user_data);
    
    // Byte 2
    byte = (unsigned char)((instrument->wave.synth & 0xF) << 4) | (instrument->wave.repeat & 0xF);
    vio->write(&byte, 1, vio->user_data);
    
    // Byte 3 (is empty)
    byte = 0;
    vio->write(&byte, 1, vio->user_data);
    
    // Byte 4 (is empty)
    byte = 0xFF;
    vio->write(&byte, 1, vio->user_data);
    
    byte = createDrumModeByte(instrument->wave.drumMode, version);
    byte |= createTransposeByte(instrument->wave.transpose, version);
    byte |= createAutomateByte(instrument->automate);
    byte |= createVibrationDirectionByte(instrument->wave.vibratoDirection);
    if (version < 4)
    {
        switch (instrument->wave.vibShape)
        {
            case LSDJ_VIB_SAWTOOTH: byte |= 2; break;
            case LSDJ_VIB_SQUARE: byte |= 6; break;
            case LSDJ_VIB_TRIANGLE:
                if (instrument->wave.plvibSpeed != LSDJ_PLVIB_FAST)
                    byte |= 4;
                break;
        }
    } else {
        byte |= (instrument->wave.vibShape & 3) << 1;
        if (instrument->wave.plvibSpeed == LSDJ_PLVIB_TICK)
            byte |= 0x10;
        else if (instrument->wave.plvibSpeed == LSDJ_PLVIB_STEP)
            byte |= 0x80;
    }
    vio->write(&byte, 1, vio->user_data);
    
    byte = createTableByte(instrument->table);
    vio->write(&byte, 1, vio->user_data);
    
    byte = createPanningByte(instrument->panning);
    vio->write(&byte, 1, vio->user_data);
    
    byte = 0;
    vio->write(&byte, 1, vio->user_data); // Byte 8 is empty
    
    byte = createPlaybackModeByte(instrument->wave.playback);
    vio->write(&byte, 1, vio->user_data);
    
    if (version >= 7)
    {
        byte = 0xF - (instrument->wave.length & 0xF);
        vio->write(&byte, 1, vio->user_data);
        
        byte = instrument->wave.speed - 4;
        vio->write(&byte, 1, vio->user_data);
    }
    else if (version == 6)
    {
        byte = (instrument->wave.length & 0xF);
        vio->write(&byte, 1, vio->user_data);
        
        byte = instrument->wave.speed - 1;
        vio->write(&byte, 1, vio->user_data);
    } else {
        byte = 0;
        vio->write(&byte, 1, vio->user_data); // Byte 10 is empty
        vio->write(&byte, 1, vio->user_data); // Byte 11 is empty
    }
    
    byte = 0;
    vio->write(&byte, 1, vio->user_data); // Byte 12 is empty
    vio->write(&byte, 1, vio->user_data); // Byte 13 is empty
    
    if (version < 6)
    {
        byte = (unsigned char)(((instrument->wave.length & 0xF) << 4) | ((instrument->wave.speed - 1) & 0xF));
        vio->write(&byte, 1, vio->user_data);
    } else {
        byte = 0;
        vio->write(&byte, 1, vio->user_data); // Byte 14 is empty
    }
    
    byte = 0;
    vio->write(&byte, 1, vio->user_data); // Byte 15 is empty
}

void write_kit_instrument(const lsdj_instrument_t* instrument, unsigned char version, lsdj_vio_t* vio)
{
    unsigned char byte = 2;
    vio->write(&byte, 1, vio->user_data);
    
    byte = createWaveVolumeByte(instrument->volume);
    vio->write(&byte, 1, vio->user_data);
    
    byte = ((instrument->kit.loop1 == LSDJ_KIT_LOOP_ATTACK) ? 0x80 : 0x0) | (instrument->kit.halfSpeed ? 0x40 : 0x0) | (instrument->kit.kit1 & 0x3F); // Keep attack 1?
    vio->write(&byte, 1, vio->user_data);
    
    vio->write(&instrument->kit.length1, 1, vio->user_data);
    
    byte = 0xFF;
    vio->write(&byte, 1, vio->user_data); // Byte 4 is empty
    
    // byte 5
    byte = ((instrument->kit.loop1 == LSDJ_KIT_LOOP_ON) ? 0x40 : 0x0) |
           ((instrument->kit.loop2 == LSDJ_KIT_LOOP_ON) ? 0x20 : 0x0) |
           createAutomateByte(instrument->automate);
    
    if (version < 4)
    {
        byte |= (instrument->kit.plvibSpeed & 3) << 1;
    } else {
        switch (instrument->kit.plvibSpeed)
        {
            case LSDJ_PLVIB_FAST: break;
            case LSDJ_PLVIB_TICK: byte |= 0x10; break;
            case LSDJ_PLVIB_STEP: byte |= 0x80; break;
        }
    }
    vio->write(&byte, 1, vio->user_data);
    
    byte = createTableByte(instrument->table);
    vio->write(&byte, 1, vio->user_data);
    
    byte = createPanningByte(instrument->panning);
    vio->write(&byte, 1, vio->user_data);
    
    vio->write(&instrument->kit.pitch, 1, vio->user_data);
    
    byte = ((instrument->kit.loop2 == LSDJ_KIT_LOOP_ATTACK) ? 0x80 : 0x0) | (instrument->kit.kit2 & 0x3F);
    vio->write(&byte, 1, vio->user_data);
    
    byte = createKitDistortionByte(instrument->kit.distortion);
    vio->write(&byte, 1, vio->user_data);
    
    vio->write(&instrument->kit.length2, 1, vio->user_data);
    vio->write(&instrument->kit.offset1, 1, vio->user_data);
    vio->write(&instrument->kit.offset2, 1, vio->user_data);
    
    byte = 0xF3;
    vio->write(&byte, 1, vio->user_data); // Byte 14 is empty
    
    byte = 0;
    vio->write(&byte, 1, vio->user_data); // Byte 15 is empty
}

void write_noise_instrument(const lsdj_instrument_t* instrument, lsdj_vio_t* vio)
{
    unsigned char byte = 3;
    vio->write(&byte, 1, vio->user_data);
    
    vio->write(&instrument->envelope, 1, vio->user_data);
    
    byte = createScommandByte(instrument->noise.sCommand);
    vio->write(&byte, 1, vio->user_data);
    
    byte = createLengthByte(instrument->noise.length);
    vio->write(&byte, 1, vio->user_data);
    
    vio->write(&instrument->noise.shape, 1, vio->user_data);
    
    byte = createAutomateByte(instrument->automate);
    vio->write(&byte, 1, vio->user_data);
    
    byte = createTableByte(instrument->table);
    vio->write(&byte, 1, vio->user_data);
    
    byte = createPanningByte(instrument->panning);
    vio->write(&byte, 1, vio->user_data);
    
    static unsigned char empty[8] = { 0, 0, 0xD0, 0, 0, 0, 0xF3, 0 };
    vio->write(empty, sizeof(empty), vio->user_data); // Bytes 8-15 are empty
}

void lsdj_instrument_write(const lsdj_instrument_t* instrument, unsigned char version, lsdj_vio_t* vio, lsdj_error_t** error)
{
    if (vio->write == NULL)
        return lsdj_error_new(error, "write is NULL");
    
    if (instrument == NULL)
        return lsdj_error_new(error, "instrument is NULL");
    
    const long pos = vio->tell(vio->user_data);
    
    switch (instrument->type)
    {
        case LSDJ_INSTR_PULSE: write_pulse_instrument(instrument, version, vio); break;
        case LSDJ_INSTR_WAVE: write_wave_instrument(instrument, version, vio); break;
        case LSDJ_INSTR_KIT: write_kit_instrument(instrument, version, vio); break;
        case LSDJ_INSTR_NOISE: write_noise_instrument(instrument, vio); break;
    }
    
    assert(vio->tell(vio->user_data) - pos == 16);
}

void lsdj_instrument_set_name(lsdj_instrument_t* instrument, const char* data, size_t size)
{
    strncpy(instrument->name, data, size < LSDJ_INSTRUMENT_NAME_LENGTH ? size : LSDJ_INSTRUMENT_NAME_LENGTH);
}

void lsdj_instrument_get_name(const lsdj_instrument_t* instrument, char* data, size_t size)
{
    const size_t len = strnlen(instrument->name, LSDJ_INSTRUMENT_NAME_LENGTH);
    strncpy(data, instrument->name, len);
    for (size_t i = len; i < LSDJ_INSTRUMENT_NAME_LENGTH; i += 1)
        data[i] = '\0';
}

void lsdj_instrument_set_panning(lsdj_instrument_t* instrument, lsdj_panning panning)
{
    instrument->panning = panning;
}

lsdj_panning lsdj_instrument_get_panning(const lsdj_instrument_t* instrument)
{
    return instrument->panning;
}
