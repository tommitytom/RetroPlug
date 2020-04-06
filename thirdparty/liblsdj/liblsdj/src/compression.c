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

#include "compression.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "defaults.h"
#include "sav.h"
#include "song.h"

//! The byte value signalling a Run-lenght Encoding (de)compression
#define RUN_LENGTH_ENCODING_BYTE (0xC0)

//! The byte value signalling a Special Action (de)compression
#define SPECIAL_ACTION_BYTE (0xE0)

//! The SA byte that signals a default wave (de)compression
#define LSDJ_DEFAULT_WAVE_BYTE (0xF0)

//! The SA byte that signals a default instrument (de)compression
#define LSDJ_DEFAULT_INSTRUMENT_BYTE (0xF1)


// --- Decompression --- //

lsdj_error_t decompress_rle_byte(lsdj_vio_t* rvio, size_t* readCounter, lsdj_vio_t* wvio, size_t* writeCounter)
{
    // Read the second byte of an RLE section
    uint8_t byte = 0;
    if (!lsdj_vio_read_byte(rvio, &byte, readCounter))
        return LSDJ_READ_FAILED;
    
    // If the second byte is *also* an RLE byte, we just need to output
    // one of these and stop the run-length decoding
    if (byte == RUN_LENGTH_ENCODING_BYTE)
    {
        if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
            return LSDJ_WRITE_FAILED;
    }
    
    // Otherwise, we're actually doing run-length decoding
    else
    {
        // Read the length of the string of identical bytes
        uint8_t count = 0;
        if (!lsdj_vio_read_byte(rvio, &count, readCounter))
            return LSDJ_READ_FAILED;
        
        // Write them to output
        if (!lsdj_vio_write_repeat(wvio, &byte, 1, count, writeCounter))
            return LSDJ_WRITE_FAILED;
    }

    return LSDJ_SUCCESS;
}

lsdj_error_t decompress_default_wave_byte(lsdj_vio_t* rvio, lsdj_vio_t* wvio, size_t* writeCounter)
{
    // Read the amount of times we need to stamp the default wave
    uint8_t count = 0;
    if (!lsdj_vio_read_byte(rvio, &count, NULL))
        return LSDJ_READ_FAILED;
    
    // Write the default wave bytes to stream
    if (!lsdj_vio_write_repeat(wvio, LSDJ_DEFAULT_WAVE, sizeof(LSDJ_DEFAULT_WAVE), count, writeCounter))
        return LSDJ_WRITE_FAILED;

    return LSDJ_SUCCESS;
}

lsdj_error_t decompress_default_instrument_byte(lsdj_vio_t* rvio, lsdj_vio_t* wvio, size_t* writeCounter)
{
    // Read the amount of times we need to stamp the default instrument
    uint8_t count = 0;
    if (!lsdj_vio_read_byte(rvio, &count, NULL))
        return LSDJ_READ_FAILED;
    
    // Write the default wave bytes to instrument
    if (!lsdj_vio_write_repeat(wvio, LSDJ_DEFAULT_INSTRUMENT, sizeof(LSDJ_DEFAULT_INSTRUMENT), count, writeCounter))
        return LSDJ_WRITE_FAILED;

    return LSDJ_SUCCESS;
}

lsdj_error_t decompress_sa_byte(lsdj_vio_t* rvio, size_t* readCounter,
                        lsdj_vio_t* wvio, size_t* writeCounter,
                        unsigned short* nextBlockIndex)
{
    // First, set the next block index to be absent
    // We'll fill this if we actually find one
    *nextBlockIndex = LSDJ_NO_NEXT_BLOCK_INDEX;
    
    // Read the first byte
    uint8_t byte = 0;
    if (!lsdj_vio_read_byte(rvio, &byte, readCounter))
        return LSDJ_READ_FAILED;
    
    switch (byte)
    {
        // If the first byte is actually another SA byte, we just output that
        // and be done with it
        case SPECIAL_ACTION_BYTE:
            if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
                return LSDJ_WRITE_FAILED;
            else
                return LSDJ_SUCCESS;
            
        // If we read a default wave byte, we delegate to that function
        case LSDJ_DEFAULT_WAVE_BYTE:
            return decompress_default_wave_byte(rvio, wvio, writeCounter);
            
        // If we read a default instrument byte, we delegate to that function
        case LSDJ_DEFAULT_INSTRUMENT_BYTE:
            return decompress_default_instrument_byte(rvio, wvio, writeCounter);
            
        // Otherwise, this is either a block jump, or an end-of-stream
        // In both cases, we write the value to the next block index and let the callee
        // handle this appropriately
        default:
            *nextBlockIndex = byte;
            return LSDJ_SUCCESS;
    }
}

lsdj_error_t lsdj_decompress(lsdj_vio_t* rvio, size_t* readCounter,
                     lsdj_vio_t* wvio, size_t* writeCounter,
                     long firstBlockPosition,
                     bool followBlockJumps)
{
    // Store our starting write position
    const long readStart = lsdj_vio_tell(rvio);
    const long writeStart = lsdj_vio_tell(wvio);
    
    // Keep on decompressing blocks until we've reached an end-of-stream
    unsigned short nextBlockIndex = LSDJ_NO_NEXT_BLOCK_INDEX;
    do
    {
        const lsdj_error_t result = lsdj_decompress_block(rvio, readCounter,
                                                          wvio, writeCounter,
                                                          &nextBlockIndex);
        
        if (result != LSDJ_SUCCESS)
            return result;
        
        assert(nextBlockIndex != LSDJ_NO_NEXT_BLOCK_INDEX);
        if (nextBlockIndex != LSDJ_END_OF_FILE_BLOCK_INDEX)
        {
            if (followBlockJumps)
            {
                const uint8_t index = (uint8_t)(nextBlockIndex) - 1;
                if (!lsdj_vio_seek(rvio, firstBlockPosition + (index * LSDJ_BLOCK_SIZE), SEEK_SET))
                    return LSDJ_SEEK_FAILED;
            }
        }
    } while (nextBlockIndex != LSDJ_END_OF_FILE_BLOCK_INDEX);
    
    assert((lsdj_vio_tell(rvio) - firstBlockPosition) % LSDJ_BLOCK_SIZE == 0);

    const long writeEnd = lsdj_vio_tell(wvio);
    if (writeEnd == -1L)
        return LSDJ_TELL_FAILED;
    
    const long readSize = writeEnd - writeStart;
    if (readSize != LSDJ_SONG_BYTE_COUNT)
        return LSDJ_DECOMPRESSION_INCORRECT_SIZE;
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_decompress_block(lsdj_vio_t* rvio, size_t* readCounter,
                           lsdj_vio_t* wvio, size_t* writeCounter,
                           unsigned short* nextBlockIndex)
{
    // Uncomment this to see every read and corresponding write position
//    const long rcur = lsdj_vio_tell(rvio)/* - readStart*/;
//    const long wcur = lsdj_vio_tell(wvio)/* - writeStart*/;
//    printf("read: 0x%lx ->\twrite: 0x%lx\n", rcur, wcur);
    
    // First, set the next block index to be absent
    // We'll fill this if we actually find one
    *nextBlockIndex = LSDJ_NO_NEXT_BLOCK_INDEX;
    
    // Store the start of this block, so that we can move to the end
    // of it afterwards
    const long start = lsdj_vio_tell(rvio);
    
    // Keep decompressing steps until we run into a block jump or
    // end-of-stream
    do
    {
        // Read the next step
        const lsdj_error_t result = lsdj_decompress_step(rvio, readCounter,
                                                         wvio, writeCounter,
                                                         nextBlockIndex);
        
        if (result != LSDJ_SUCCESS)
            return result;
    } while (*nextBlockIndex == LSDJ_NO_NEXT_BLOCK_INDEX);
    
    // Move to the end of this block to keep everything sane
    if (!lsdj_vio_seek(rvio, start + LSDJ_BLOCK_SIZE, SEEK_SET))
        return LSDJ_SEEK_FAILED;
    
    return LSDJ_SUCCESS;
}

lsdj_error_t lsdj_decompress_step(lsdj_vio_t* rvio, size_t* readCounter,
                          lsdj_vio_t* wvio, size_t* writeCounter,
                          unsigned short* nextBlockIndex)
{
    // Uncomment this to see every read and corresponding write position
//    const long rcur = lsdj_vio_tell(rvio)/* - readStart*/;
//    const long wcur = lsdj_vio_tell(wvio)/* - writeStart*/;
//    printf("read: 0x%lx ->\twrite: 0x%lx\n", rcur, wcur);
    
    // First, set the next block index to be absent
    // We'll fill this if we actually find one
    *nextBlockIndex = LSDJ_NO_NEXT_BLOCK_INDEX;
    
    // Read the byte that declares what step this is
    uint8_t byte = 0;
    if (!lsdj_vio_read_byte(rvio, &byte, readCounter))
        return LSDJ_READ_FAILED;
    
    switch (byte)
    {
        // In the case of an RLE byte, delegate to that function
        case RUN_LENGTH_ENCODING_BYTE:
            return decompress_rle_byte(rvio, readCounter, wvio, writeCounter);
            
        // In the case of an SA byte, delegate to that function
        case SPECIAL_ACTION_BYTE:
            return decompress_sa_byte(rvio, readCounter, wvio, writeCounter, nextBlockIndex);
        
        // Otherwise, just write the same byte to the output stream
        default:
            if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
                return LSDJ_WRITE_FAILED;
            
            return LSDJ_SUCCESS;
    }
}


// --- Compression --- //

lsdj_error_t lsdj_compress(const uint8_t* data, lsdj_vio_t* wvio, unsigned int blockOffset, size_t* writeCounter)
{
    //! @todo This function needs to be refactored because it is w-a-y too huge

    if (blockOffset == LSDJ_BLOCK_COUNT + 1)
        return false;
    
    uint8_t nextEvent[3] = { 0, 0, 0 };
    unsigned short eventSize = 0;
    
    unsigned int currentBlock = blockOffset;
    unsigned int currentBlockSize = 0;
    
    uint8_t byte = 0;
    
    long writeStart = lsdj_vio_tell(wvio);
    if (writeStart == -1L)
        return LSDJ_TELL_FAILED;
    
    const uint8_t* end = data + LSDJ_SONG_BYTE_COUNT;
    for (const uint8_t* read = data; read < end; )
    {
        // Uncomment this to print the current read and write positions
        // long wcur = lsdj_vio_tell(wvio) - writeStart;
        // printf("read: 0x%lx\twrite: 0x%lx\n", read - data, wcur);
        
        // Are we reading a default wave? If so, we can compress these!
        uint8_t defaultWaveLengthCount = 0;
        while (read + LSDJ_DEFAULT_WAVE_LENGTH < end && memcmp(read, LSDJ_DEFAULT_WAVE, LSDJ_DEFAULT_WAVE_LENGTH) == 0 && defaultWaveLengthCount != 0xFF)
        {
            read += LSDJ_DEFAULT_WAVE_LENGTH;
            ++defaultWaveLengthCount;
        }
        
        if (defaultWaveLengthCount > 0)
        {
            nextEvent[0] = SPECIAL_ACTION_BYTE;
            nextEvent[1] = LSDJ_DEFAULT_WAVE_BYTE;
            nextEvent[2] = defaultWaveLengthCount;
            eventSize = 3;
        } else {
            // Are we reading a default instrument? If so, we can compress these!
            uint8_t defaultInstrumentLengthCount = 0;
            while (read + LSDJ_DEFAULT_INSTRUMENT_LENGTH < end && memcmp(read, LSDJ_DEFAULT_INSTRUMENT, LSDJ_DEFAULT_INSTRUMENT_LENGTH) == 0 && defaultInstrumentLengthCount != 0xFF)
            {
                read += LSDJ_DEFAULT_INSTRUMENT_LENGTH;
                ++defaultInstrumentLengthCount;
            }
            
            if (defaultInstrumentLengthCount > 0)
            {
                nextEvent[0] = SPECIAL_ACTION_BYTE;
                nextEvent[1] = LSDJ_DEFAULT_INSTRUMENT_BYTE;
                nextEvent[2] = defaultInstrumentLengthCount;
                eventSize = 3;
            } else {
                // Not a default wave, time to do "normal" compression
                switch (*read)
                {
                    case RUN_LENGTH_ENCODING_BYTE:
                        nextEvent[0] = RUN_LENGTH_ENCODING_BYTE;
                        nextEvent[1] = RUN_LENGTH_ENCODING_BYTE;
                        eventSize = 2;
                        read++;
                        break;
                        
                    case SPECIAL_ACTION_BYTE:
                        nextEvent[0] = SPECIAL_ACTION_BYTE;
                        nextEvent[1] = SPECIAL_ACTION_BYTE;
                        eventSize = 2;
                        read++;
                        break;
                        
                    default:
                    {
                        const uint8_t* beg = read;
                       
                        uint8_t c = *read;
                        
                        // See if we can do run-length encoding
                        if ((read + 3 < end) &&
                            *(read + 1) == c &&
                            *(read + 2) == c &&
                            *(read + 3) == c)
                        {
                            uint8_t count = 0;
                            
                            while (read < end && *read == c && count != 0xFF)
                            {
                                ++count;
                                ++read;
                            }
                            
                            assert((read - beg) == count);
                            
                            nextEvent[0] = RUN_LENGTH_ENCODING_BYTE;
                            nextEvent[1] = c;
                            nextEvent[2] = count;
                            
                            eventSize = 3;
                        } else {
                            nextEvent[0] = *read++;
                            eventSize = 1;
                        }
                        
                        break;
                    }
                }
            }
        }
         
        
        // See if the event would still fit in this block
        // If not, move to a new block
        if (currentBlockSize + eventSize + 2 >= LSDJ_BLOCK_SIZE)
        {
            // Write the "next block" command
            byte = SPECIAL_ACTION_BYTE;
            if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
                return LSDJ_WRITE_FAILED;
            
            byte = (uint8_t)(currentBlock + 1);
            if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
                return LSDJ_WRITE_FAILED;
            
            currentBlockSize += 2;
            assert(currentBlockSize <= LSDJ_BLOCK_SIZE);
            
            // Fill the rest of the block with 0's
            byte = 0;
            for (; currentBlockSize < LSDJ_BLOCK_SIZE; currentBlockSize++)
            {
                if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
                    return LSDJ_WRITE_FAILED;
            }
            
            // Make sure we filled up the block entirely
            assert(currentBlockSize == LSDJ_BLOCK_SIZE);
            
            // Move to the next block
            currentBlock += 1;
            currentBlockSize = 0;
            
            // Have we reached the maximum block count?
            // If so, roll back
            if (currentBlock == LSDJ_BLOCK_COUNT + 1)
            {
                long pos = lsdj_vio_tell(wvio);
                if (!lsdj_vio_seek(wvio, writeStart, SEEK_SET))
                    return LSDJ_SEEK_FAILED;
                
                byte = 0;
                if (!lsdj_vio_write_repeat(wvio, &byte, 1, (size_t)(pos - writeStart), writeCounter))
                    return LSDJ_WRITE_FAILED;
                
                if (!lsdj_vio_seek(wvio, writeStart, SEEK_SET))
                    return LSDJ_SEEK_FAILED;
                
                return false;
            }
            
            // Don't "continue;" but fall through. We still need to write the event *in the next block*
        }
        
        if (!lsdj_vio_write(wvio, nextEvent, eventSize, writeCounter))
            return LSDJ_WRITE_FAILED;
        
        currentBlockSize += eventSize;
        nextEvent[0] = nextEvent[1] = nextEvent[2] = 0;
        eventSize = 0;
    }
    
    byte = SPECIAL_ACTION_BYTE;
    if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
        return LSDJ_WRITE_FAILED;
    
    byte = LSDJ_END_OF_FILE_BLOCK_INDEX;
    if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
        return LSDJ_WRITE_FAILED;
    
    // Pad 0's to the end of the block
    if (currentBlockSize > 0)
    {
        byte = 0;
        for (currentBlockSize += 2; currentBlockSize < LSDJ_BLOCK_SIZE; currentBlockSize++)
        {
            if (!lsdj_vio_write_byte(wvio, byte, writeCounter))
                return LSDJ_WRITE_FAILED;
        }
    }
    
    return LSDJ_SUCCESS;
}
