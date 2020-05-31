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

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

#include "common.hpp"

namespace lsdj
{
    int handle_error(lsdj_error_t error)
    {
        std::cerr << "ERROR: " << lsdj_error_get_description(error) << std::endl;
        return 1;
    }
    
    bool compareCaseInsensitive(std::string str1, std::string str2)
    {
        std::transform(str1.begin(), str1.end(), str1.begin(), ::tolower);
        std::transform(str2.begin(), str2.end(), str2.begin(), ::tolower);
        return str1 == str2;
    }
    
    std::string constructProjectName(const lsdj_project_t* project, bool underscore)
    {
        std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
        name.fill('\0');
        strncpy(name.data(), lsdj_project_get_name(project), name.size());
        
        if (underscore)
            std::replace(name.begin(), name.end(), 'x', '_');
        
        return {name.data(), strnlen(name.data(), name.size())};
    }
    
    bool isHiddenFile(const std::string& str)
    {
        switch (str.size())
        {
            case 0: return true;
            case 1: return false;
            default: return str[0] == '.' && str[1] != '.' && str[1] != '/';
        }
    }
}
