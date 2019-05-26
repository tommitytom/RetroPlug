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

#ifndef LSDJ_WAVETABLE_IMPORTER_HPP
#define LSDJ_WAVETABLE_IMPORTER_HPP

#include <boost/filesystem.hpp>
#include <string>
#include <vector>

#include "../liblsdj/error.h"
#include "../liblsdj/song.h"

namespace lsdj
{
    class WavetableImporter
    {
    public:
        bool import(const std::string& projectName, const std::string& wavetableName);
        
    public:
        std::string outputName;
        unsigned char wavetableIndex = 0;
        
        bool zero = false;
        bool force = false;
        bool verbose = false;
        
    private:
        bool importToSav(const boost::filesystem::path& path, const std::string& wavetableName);
        bool importToLsdsng(const boost::filesystem::path& path, const std::string& wavetableName);
        std::pair<bool, unsigned int> importToSong(lsdj_song_t* song, const std::string& wavetableName);
    };
}

#endif
