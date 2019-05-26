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

#ifndef LSDJ_EXPORTER_HPP
#define LSDJ_EXPORTER_HPP

#include <boost/filesystem/path.hpp>

#include "../liblsdj/error.h"
#include "../liblsdj/project.h"
#include "../liblsdj/sav.h"

namespace lsdj
{
    class Exporter
    {
    public:
        enum class VersionStyle
        {
            NONE,
            HEX,
            DECIMAL
        };
        
    public:
        int exportProjects(const boost::filesystem::path& path, const std::string& output);
        void exportProject(const lsdj_project_t* project, boost::filesystem::path folder, bool workingMemory, lsdj_error_t** error);
        int print(const boost::filesystem::path& path);
        
    public:
        // The version exporting style
        VersionStyle versionStyle = VersionStyle::HEX;
        
        bool underscore = false;
        bool putInFolder = false;
        bool verbose = false;
        
        std::vector<int> indices;
        std::vector<std::string> names;
        
    private:
        int printFolder(const boost::filesystem::path& path);
        int printSav(const boost::filesystem::path& path);
        
    private:
        // Converts a project version to a string representation using the current VersionStyle
        std::string convertVersionToString(unsigned char version, bool prefixDot) const;
        
        // Print the working memory song line
        void printWorkingMemorySong(const lsdj_sav_t* sav);
        
        // Print a sav project line
        void printProject(const lsdj_sav_t* sav, std::size_t index);
        
        std::string constructName(const lsdj_project_t* project);
    };
}

#endif
