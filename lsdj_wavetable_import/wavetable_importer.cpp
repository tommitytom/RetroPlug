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

#include <algorithm>
#include <array>
#include <iostream>

#include "../liblsdj/project.h"
#include "../liblsdj/sav.h"

#include "../common/common.hpp"
#include "wavetable_importer.hpp"

namespace lsdj
{
    bool WavetableImporter::import(const std::string& destination, const std::string& wavetableName)
    {
        const auto path = boost::filesystem::absolute(destination);
        if (!boost::filesystem::exists(path))
        {
            std::cerr << path.filename().string() << " does not exist" << std::endl;
            return false;
        }
        
        if (path.extension() == ".sav")
            return importToSav(path, wavetableName);
        else if (path.extension() == ".lsdsng")
            return importToLsdsng(path, wavetableName);
        else
        {
            std::cerr << "Unknown file format at '" << path.string() << "'" << std::endl;
            return false;
        }
    }
    
    bool WavetableImporter::importToSav(const boost::filesystem::path& path, const std::string& wavetableName)
    {
        // Load the sav
        lsdj_error_t* error = nullptr;
        lsdj_sav_t* sav = lsdj_sav_read_from_file(path.string().c_str(), &error);
        if (error != nullptr)
        {
            lsdj_sav_free(sav);
            return false;
        }
        
        if (verbose)
            std::cout << "Loaded sav " + path.string() << std::endl;
        
        lsdj_song_t* song = lsdj_sav_get_working_memory_song(sav);
        if (!song)
        {
            lsdj_sav_free(sav);
            return false;
        }
        
        // Do the actual import
        const auto result = importToSong(song, wavetableName);
        if (!result.first)
        {
            lsdj_sav_free(sav);
            return false;
        }
        const auto frameCount = result.second;
        
        // Write the sav back to file
        const auto outputPath = boost::filesystem::absolute(outputName);
        lsdj_sav_write_to_file(sav, outputPath.string().c_str(), &error);
        if (error != nullptr)
        {
            lsdj_sav_free(sav);
            return false;
        }
        
        std::cout << "Wrote " << std::dec << frameCount << " frames starting at 0x" << std::hex << (int)wavetableIndex << " to " << outputPath.string() << std::endl;
        
        return true;
    }
    
    bool WavetableImporter::importToLsdsng(const boost::filesystem::path& path, const std::string& wavetableName)
    {
        // Load the project
        lsdj_error_t* error = nullptr;
        lsdj_project_t* project = lsdj_project_read_lsdsng_from_file(path.string().c_str(), &error);
        if (error != nullptr)
        {
            lsdj_project_free(project);
            return false;
        }
        
        if (verbose)
            std::cout << "Loaded project " + path.string() << std::endl;
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        
        // Do the actual import
        const auto result = importToSong(song, wavetableName);
        if (!result.first)
        {
            lsdj_project_free(project);
            return false;
        }
        const auto frameCount = result.second;
        
        // Write the project back to file
        const auto outputPath = boost::filesystem::absolute(outputName);
        lsdj_project_write_lsdsng_to_file(project, outputPath.string().c_str(), &error);
        if (error != nullptr)
        {
            lsdj_project_free(project);
            return false;
        }
        
        std::cout << "Wrote " << std::dec << frameCount << " frames starting at 0x" << std::hex << (int)wavetableIndex << " to " << outputPath.string() << std::endl;
        
        return true;
    }
    
    std::pair<bool, unsigned int> WavetableImporter::importToSong(lsdj_song_t* song, const std::string& wavetableName)
    {
        // Find the wavetable file
        const auto wavetablePath = boost::filesystem::absolute(wavetableName);
        if (!boost::filesystem::exists(wavetablePath))
        {
            std::cerr << wavetablePath.filename().string() << " does not exist" << std::endl;
            return {false, 0};
        }
        
        // Make sure the wavetable is the correct size
        const auto wavetableSize = boost::filesystem::file_size(wavetablePath);
        if (wavetableSize % 16 != 0)
        {
            std::cerr << "The wavetable file size is not a multiple of 16 bytes" << std::endl;
            return {false, 0};
        }
        
        // Load the wavetable file
        std::ifstream wavetableStream(wavetablePath.string(), std::ios_base::binary);
        if (!wavetableStream.is_open())
        {
            std::cerr << "Could not open " << wavetablePath.filename().string() << std::endl;
            return {false, 0};
        }
        
        // Compute the amount of frames we will write
        const auto frameCount = wavetableSize / 16;
        if (verbose)
            std::cout << "Found " << std::dec << frameCount << " frames in " << wavetablePath.string() << std::endl;
        
        const auto actualFrameCount = std::min<unsigned int>(0x100 - wavetableIndex, frameCount);
        if (frameCount != actualFrameCount)
        {
            std::cout << "Last " << std::dec << (frameCount - actualFrameCount) << " won't fit in the song" << std::endl;
            
            if (verbose)
                std::cout << "Writing only " << std::dec << actualFrameCount << " frames due to space limits" << std::endl;
        }
        
        // Check to see if we're overwriting non-default wavetables
        if (!force)
        {
            if (verbose)
                std::cout << "Comparing frames to ensure no overwriting" << std::endl;
            
            for (auto frame = 0; frame < actualFrameCount; frame++)
            {
                lsdj_wave_t* wave = lsdj_song_get_wave(song, wavetableIndex + frame);
                if (memcmp(wave->data, LSDJ_DEFAULT_WAVE, LSDJ_WAVE_LENGTH) != 0)
                {
                    std::cout << "Some of the wavetable frames you are trying to overwrite already contain data.\nDo you want to continue? y/n\n> ";
                    char answer = 'n';
                    std::cin >> answer;
                    if (answer != 'y')
                    {
                        return {false, 0};
                    } else {
                        break;
                    }
                } else if (verbose) {
                    std::cout << "Frame 0x" << std::hex << (wavetableIndex + frame) << " is default" << std::endl;
                }
            }
        }
        
        // Apply the wavetable
        for (unsigned char frame = 0; frame < actualFrameCount; frame++)
        {
            lsdj_wave_t* wave = lsdj_song_get_wave(song, wavetableIndex + frame);
            wavetableStream.read(reinterpret_cast<char*>(wave->data), sizeof(wave->data));
            
            if (verbose)
                std::cout << "Wrote " << std::dec << sizeof(wave->data) << " bytes to frame 0x" << std::hex << (wavetableIndex + frame) << std::endl;
        }
        
        // Write zero wavetables
        if (zero)
        {
            if (verbose)
                std::cout << "Padding empty frames" << std::endl;
            
            std::array<char, LSDJ_WAVE_LENGTH> table;
            table.fill(0x88);
            
            for (unsigned char frame = actualFrameCount; frame < 16; frame++)
            {
                lsdj_wave_t* wave = lsdj_song_get_wave(song, wavetableIndex + frame);
                memcpy(wave->data, table.data(), sizeof(table));
                
                if (verbose)
                    std::cout << "Wrote default bytes to frame 0x" << std::hex << (wavetableIndex + frame) << std::endl;
            }
        }
        
        return {true, actualFrameCount};
    }
}
