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
#include <string.h>

#include "compression.h"
#include "instrument.h"
#include "song.h"
#include "wave.h"

#define RUN_LENGTH_ENCODING_BYTE 0xC0
#define SPECIAL_ACTION_BYTE 0xE0
#define END_OF_FILE_BYTE 0xFF
#define LSDJ_DEFAULT_WAVE_BYTE 0xF0
#define LSDJ_DEFAULT_INSTRUMENT_BYTE 0xF1

static const unsigned char LSDJ_DEFAULT_INSTRUMENT_COMPRESSION[LSDJ_LSDJ_DEFAULT_INSTRUMENT_LENGTH] = { 0xA8, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0, 0 };

void decompress_rle_byte(lsdj_vio_t* rvio, lsdj_vio_t* wvio, lsdj_error_t** error)
{
    unsigned char byte;
    if (rvio->read(&byte, 1, rvio->user_data) != 1)
        return lsdj_error_new(error, "could not read RLE byte");
    
    if (byte == RUN_LENGTH_ENCODING_BYTE)
    {
        if (wvio->write(&byte, 1, wvio->user_data) != 1)
            return lsdj_error_new(error, "could not write RLE byte");
    }
    else
    {
        unsigned char count = 0;
        if (rvio->read(&count, 1, rvio->user_data) != 1)
            return lsdj_error_new(error, "could not read RLE count byte");
        
        for (int i = 0; i < count; ++i)
        {
            if (wvio->write(&byte, 1, wvio->user_data) != 1)
                return lsdj_error_new(error, "could not write byte for RLE expansion");
        }
    }
}

void decompress_LSDJ_DEFAULT_WAVE_byte(lsdj_vio_t* rvio, lsdj_vio_t* wvio, lsdj_error_t** error)
{
    unsigned char count = 0;
    if (rvio->read(&count, 1, rvio->user_data) != 1)
        return lsdj_error_new(error, "could not read default wave count byte");
    
    for (int i = 0; i < count; ++i)
    {
        if (wvio->write(LSDJ_DEFAULT_WAVE, sizeof(LSDJ_DEFAULT_WAVE), wvio->user_data) != sizeof(LSDJ_DEFAULT_WAVE))
            return lsdj_error_new(error, "could not write default wave byte");
    }
}

void decompress_LSDJ_DEFAULT_INSTRUMENT_byte(lsdj_vio_t* rvio, lsdj_vio_t* wvio, lsdj_error_t** error)
{
    unsigned char count = 0;
    if (rvio->read(&count, 1, rvio->user_data) != 1)
        return lsdj_error_new(error, "could not read default instrument count byte");
    
    for (int i = 0; i < count; ++i)
    {
        if (wvio->write(LSDJ_DEFAULT_INSTRUMENT_COMPRESSION, sizeof(LSDJ_DEFAULT_INSTRUMENT_COMPRESSION), wvio->user_data) != sizeof(LSDJ_DEFAULT_INSTRUMENT_COMPRESSION))
            return lsdj_error_new(error, "could not write default instrument byte");
    }
}

void decompress_sa_byte(lsdj_vio_t* rvio, long* currentBlockPosition, long* block1position, size_t blockSize, lsdj_vio_t* wvio, int* reading, lsdj_error_t** error)
{
    unsigned char byte = 0;
    if (rvio->read(&byte, 1, rvio->user_data) != 1)
        return lsdj_error_new(error, "could not read SA byte");
    
    switch (byte)
    {
        case SPECIAL_ACTION_BYTE:
            if (wvio->write(&byte, 1, wvio->user_data) != 1)
                return lsdj_error_new(error, "could not write SA byte");
            break;
        case LSDJ_DEFAULT_WAVE_BYTE:
            decompress_LSDJ_DEFAULT_WAVE_byte(rvio, wvio, error);
            if (error && *error)
                return;
            break;
        case LSDJ_DEFAULT_INSTRUMENT_BYTE:
            decompress_LSDJ_DEFAULT_INSTRUMENT_byte(rvio, wvio, error);
            if (error && *error)
                return;
            break;
        case END_OF_FILE_BYTE:
            *reading = 0;
            break;
        default:
            if (block1position)
                *currentBlockPosition = *block1position + (long)((byte - 1) * blockSize);
            else
                *currentBlockPosition += blockSize;
            
            if (rvio->seek(*currentBlockPosition, SEEK_SET, rvio->user_data) != 0)
                return lsdj_error_new(error, "could not seek to new block position");
            break;
    }
}

void lsdj_decompress(lsdj_vio_t* rvio, lsdj_vio_t* wvio, long* block1position, size_t blockSize, lsdj_error_t** error)
{
    long wstart = wvio->tell(wvio->user_data);
    long currentBlockPosition = rvio->tell(rvio->user_data);
    if (currentBlockPosition == -1L)
        return lsdj_error_new(error, "could not tell current block position");
    
    unsigned char byte = 0;
    
    int reading = 1;
    while (reading == 1)
    {
        // Uncomment this to see every read and corresponding write position
//        const long rcur = rvio->tell(rvio->user_data) - 9;
//        const long wcur = wvio->tell(wvio->user_data) - wstart;
//        printf("read: 0x%lx\twrite: 0x%lx\n", rcur, wcur);
        
        if (rvio->read(&byte, 1, rvio->user_data) != 1)
            return lsdj_error_new(error, "could not read byte for decompression");
        
        switch (byte)
        {
            case RUN_LENGTH_ENCODING_BYTE:
                decompress_rle_byte(rvio, wvio, error);
                if (error && *error)
                    return;
                break;
            case SPECIAL_ACTION_BYTE:
                decompress_sa_byte(rvio, &currentBlockPosition, block1position, blockSize, wvio, &reading, error);
                if (error && *error)
                    return;
                break;
            default:
                if (wvio->write(&byte, 1, wvio->user_data) != 1)
                    return lsdj_error_new(error, "could not write decompression byte");
                break;
        }
    }

    const long wend = wvio->tell(wvio->user_data);
    if (wend == -1L)
        return lsdj_error_new(error, "could not tell compression end");
    
    const long readSize = wend - wstart;
    if (wend - wstart != LSDJ_SONG_DECOMPRESSED_SIZE)
    {
        char buffer[100];
        memset(buffer, '\0', sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "decompressed size does not line up with 0x8000 bytes (but 0x%lx)", wend - wstart);
        return lsdj_error_new(error, buffer);
    }
}

void lsdj_decompress_from_file(const char* path, lsdj_vio_t* wvio, long* firstBlockOffset, size_t blockSize, lsdj_error_t** error)
{
    if (path == NULL)
    {
        lsdj_error_new(error, "path is NULL");
        return;
    }
    
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        char message[512];
        snprintf(message, 512, "could not open %s for reading", path);
        return lsdj_error_new(error, message);
    }
    
    lsdj_vio_t vio;
    vio.read = lsdj_fread;
    vio.tell = lsdj_ftell;
    vio.seek = lsdj_fseek;
    vio.user_data = file;
    
    lsdj_decompress(&vio, wvio, firstBlockOffset, blockSize, error);
    
    fclose(file);
}

unsigned int lsdj_compress(const unsigned char* data, unsigned int blockSize, unsigned char startBlock, unsigned int blockCount, lsdj_vio_t* wvio, lsdj_error_t** error)
{
    if (startBlock == blockCount + 1)
        return 0;
    
    unsigned char nextEvent[3] = { 0, 0, 0 };
    unsigned short eventSize = 0;
    
    unsigned char currentBlock = startBlock;
    unsigned int currentBlockSize = 0;
    
    unsigned char byte = 0;
    
    long wstart = wvio->tell(wvio->user_data);
    if (wstart == -1L)
    {
        lsdj_error_new(error, "could not tell write position on compression");
        return 0;
    }
    
    const unsigned char* end = data + LSDJ_SONG_DECOMPRESSED_SIZE;
    for (const unsigned char* read = data; read < end; )
    {
        // Uncomment this to print the current read and write positions
        long wcur = wvio->tell(wvio->user_data) - wstart;
//        printf("read: 0x%lx\twrite: 0x%lx\n", read - data, wcur);
        
        // Are we reading a default wave? If so, we can compress these!
        unsigned char defaultWaveLengthCount = 0;
        while (read + LSDJ_WAVE_LENGTH < end && memcmp(read, LSDJ_DEFAULT_WAVE, LSDJ_WAVE_LENGTH) == 0 && defaultWaveLengthCount != 0xFF)
        {
            read += LSDJ_WAVE_LENGTH;
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
            unsigned char defaultInstrumentLengthCount = 0;
            while (read + LSDJ_LSDJ_DEFAULT_INSTRUMENT_LENGTH < end && memcmp(read, LSDJ_DEFAULT_INSTRUMENT_COMPRESSION, LSDJ_LSDJ_DEFAULT_INSTRUMENT_LENGTH) == 0 && defaultInstrumentLengthCount != 0xFF)
            {
                read += LSDJ_LSDJ_DEFAULT_INSTRUMENT_LENGTH;
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
                        const unsigned char* beg = read;
                        
                        unsigned char c = *read;
                        
                        // See if we can do run-length encoding
                        if ((read + 3 < end) &&
                            *(read + 1) == c &&
                            *(read + 2) == c &&
                            *(read + 3) == c)
                        {
                            unsigned char count = 0;
                            
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
        if (currentBlockSize + eventSize + 2 >= blockSize)
        {
            // Write the "next block" command
            byte = SPECIAL_ACTION_BYTE;
            if (wvio->write(&byte, 1, wvio->user_data) != 1)
            {
                lsdj_error_new(error, "could not write SA byte for next block command");
                return 0;
            }
            
            byte = currentBlock + 1;
            if (wvio->write(&byte, 1, wvio->user_data) != 1)
            {
                lsdj_error_new(error, "could not write next block byte for compression");
                return 0;
            }
            
            currentBlockSize += 2;
            assert(currentBlockSize <= blockSize);
            
            // Fill the rest of the block with 0's
            byte = 0;
            for (; currentBlockSize < blockSize; currentBlockSize++)
            {
                if (wvio->write(&byte, 1, wvio->user_data) != 1)
                {
                    lsdj_error_new(error, "could not write 0 for block padding");
                    return 0;
                }
            }
            
            // Make sure we filled up the block entirely
            if (currentBlockSize != blockSize)
            {
                lsdj_error_new(error, "block wasn't completely filled upon compression");
                return 0;
            }
            
            // Move to the next block
            currentBlock += 1;
            currentBlockSize = 0;
            
            // Have we reached the maximum block count?
            // If so, roll back
            if (currentBlock == blockCount + 1)
            {
                long pos = wvio->tell(wvio->user_data);
                if (wvio->seek(wstart, SEEK_SET, wvio->user_data) != 0)
                {
                    lsdj_error_new(error, "could not roll back after reaching max block count for compression");
                    return 0;
                }
                
                byte = 0;
                for (long i = 0; i < pos - wstart; ++i)
                {
                    if (wvio->write(&byte, 1, wvio->user_data) != 1)
                    {
                        lsdj_error_new(error, "could not fill rolled back data with 0 for compression");
                        return 0;
                    }
                }
                
                if (wvio->seek(wstart, SEEK_SET, wvio->user_data) != 0)
                {
                    lsdj_error_new(error, "could not fill roll back to start for compression roll back");
                    return 0;
                }
                
                return 0;
            }
            
            // Don't "continue;" but fall through. We still need to write the event *in the next block*
        }
        
        if (wvio->write(nextEvent, eventSize, wvio->user_data) != eventSize)
        {
            lsdj_error_new(error, "could not write event for compression");
            return 0;
        }
        
        currentBlockSize += eventSize;
        nextEvent[0] = nextEvent[1] = nextEvent[2] = 0;
        eventSize = 0;
    }
    
    byte = SPECIAL_ACTION_BYTE;
    if (wvio->write(&byte, 1, wvio->user_data) != 1)
    {
        lsdj_error_new(error, "could not write SA for EOF for compression");
        return 0;
    }
    
    byte = END_OF_FILE_BYTE;
    if (wvio->write(&byte, 1, wvio->user_data) != 1)
    {
        lsdj_error_new(error, "could not write EOF for compression");
        return 0;
    }
    
    if (currentBlockSize > 0)
    {
        byte = 0;
        for (currentBlockSize += 2; currentBlockSize < blockSize; currentBlockSize++)
        {
            if (wvio->write(&byte, 1, wvio->user_data) != 1)
            {
                lsdj_error_new(error, "could not write 0 for block padding");
                return 0;
            }
        }
    }
    
    return currentBlock - startBlock + 1;
}

unsigned int lsdj_compress_to_file(const unsigned char* data, unsigned int blockSize, unsigned char startBlock, unsigned int blockCount, const char* path, lsdj_error_t** error)
{
    if (path == NULL)
    {
        lsdj_error_new(error, "path is NULL");
        return 0;
    }
    
    if (data == NULL)
    {
        lsdj_error_new(error, "data is NULL");
        return 0;
    }
    
    FILE* file = fopen(path, "wb");
    if (file == NULL)
    {
        char message[512];
        snprintf(message, 512, "could not open %s for writing", path);
        lsdj_error_new(error, message);
        return 0;
    }
    
    lsdj_vio_t vio;
    vio.write = lsdj_fwrite;
    vio.tell = lsdj_ftell;
    vio.seek = lsdj_fseek;
    vio.user_data = file;
    
    unsigned int result = lsdj_compress(data, blockSize, startBlock, blockCount, &vio, error);
    
    fclose(file);
    
    return result;
}
