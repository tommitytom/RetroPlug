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
#include <iostream>

#include <lsdj/song.h>
#include <lsdj/project.h>

#include "../common/common.hpp"
#include "importer.hpp"

namespace lsdj
{
    // Scan a path, see whether it's either an .lsdsng or a folder containing .lsdsng's
    // Returns the path to the .WM (working memory) file, or {} is there was none
    ghc::filesystem::path scanPath(const ghc::filesystem::path& path, std::vector<ghc::filesystem::path>& paths)
    {
        if (isHiddenFile(path.filename().string()))
            return {};
        
        if (ghc::filesystem::is_regular_file(path))
        {
            paths.emplace_back(path);
            return {};
        }
        else if (ghc::filesystem::is_directory(path))
        {
            ghc::filesystem::path workingMemoryPath;
            std::vector<ghc::filesystem::path> contents;
            for (auto it = ghc::filesystem::directory_iterator(path); it != ghc::filesystem::directory_iterator(); ++it)
            {
                const auto path = it->path();
                if (isHiddenFile(path.filename().string()) || !ghc::filesystem::is_regular_file(path) || path.extension() != ".lsdsng")
                    continue;
                
                const auto str = path.stem().string();
                if (str.size() >= 3 && str.substr(str.length() - 3) == ".WM")
                {
                    workingMemoryPath = path;
                    continue;
                }
                
                contents.emplace_back(path);
            }
            
            std::sort(contents.begin(), contents.end());
            for (auto& path : contents)
                paths.emplace_back(path);
            
            return workingMemoryPath;
        } else {
            throw std::runtime_error(path.string() + " is not a file or directory");
        }
    }
    
    int Importer::importSongs(const char* savName)
    {
        // Try to load the provided destination sav, or create a new one
        lsdj_sav_t* sav = nullptr;
        lsdj_error_t error = LSDJ_SUCCESS;
        if (savName)
            error = lsdj_sav_read_from_file(ghc::filesystem::absolute(savName).string().c_str(), &sav, nullptr);
        else
            error = lsdj_sav_new(&sav, nullptr);
        
        if (error != LSDJ_SUCCESS)
            return handle_error(error);
        
        // Find the first available project slot index
        auto index = 0;
        for( ; index < LSDJ_SAV_PROJECT_COUNT; ++index)
        {
            if (!lsdj_sav_get_project_const(sav, index))
                break;
        }
        
        if (savName && verbose)
            std::cout << "Read " << savName << ", containing " << std::to_string(index) << " saves" << std::endl;
        
        // Go through all input files and recursively find all .lsdsngs's (and the working memory file)
        std::vector<ghc::filesystem::path> paths;
        for (auto& input : inputs)
        {
            const auto wm = scanPath(ghc::filesystem::absolute(input), paths);
            if (!wm.empty())
            {
                if (!workingMemoryPath.empty())
                {
                    std::cerr << "Multiple working memory (.WM) .lsdsng's found" << std::endl;
                    return 1;
                }
                
                workingMemoryPath = wm;
            }
        }
        
        // Construct an output file name if it's empty
        if (outputFile.empty())
        {
            if (inputs.size() == 1)
                outputFile = std::string(paths.front().filename().string()) + ".sav";
            else
                outputFile = "out.sav";
        }
        
        // Import all lsdsng files
        const auto active = lsdj_sav_get_active_project_index(sav);
        for (auto i = 0; i < paths.size(); ++i)
        {
            if (index == LSDJ_SAV_PROJECT_COUNT)
            {
                std::cerr << "Reached maximum project count, can't write " << paths[i].string() << std::endl;
                break;
            }
            
            const lsdj_error_t error = importSong(paths[i].string(), sav, index, active);
            if (error != LSDJ_SUCCESS)
            {
                lsdj_sav_free(sav);
                return handle_error(error);
            }
            
            index += 1;
        }
        
        if (!workingMemoryPath.empty())
        {
            const lsdj_error_t error = importWorkingMemorySong(sav, paths);
            if (error != LSDJ_SUCCESS)
            {
                lsdj_sav_free(sav);
                return handle_error(error);
            }
        }
        
        if (outputFile.empty())
            outputFile = "out.sav";
        
        // Write the sav to file
        error = lsdj_sav_write_to_file(sav, ghc::filesystem::absolute(outputFile).string().c_str(), nullptr);
        if (error != LSDJ_SUCCESS)
        {
            lsdj_sav_free(sav);
            return handle_error(error);
        }
        
        return 0;
    }
    
    lsdj_error_t Importer::importSong(const std::string& path, lsdj_sav_t* sav, uint8_t index, uint8_t active)
    {
        lsdj_project_t* project = nullptr;
        lsdj_error_t error = lsdj_project_read_lsdsng_from_file(path.c_str(), &project, nullptr);
        if (error != LSDJ_SUCCESS)
            return error;
        assert(project != nullptr);
        
        lsdj_sav_set_project_move(sav, index, project);
        
        const auto n = lsdj_project_get_name(project);
        std::string name(n, strnlen(n, LSDJ_PROJECT_NAME_LENGTH));
        
        if (verbose)
            std::cout << "Imported " << name.data() << " at slot " << std::to_string(index) << std::endl;
        
        if (index == 0 && active == LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX && workingMemoryPath.empty())
        {
            error = lsdj_sav_set_working_memory_song_from_project(sav, index);
            if (error != LSDJ_SUCCESS)
            {
                lsdj_project_free(project);
                return error;
            }
        }
        
        return LSDJ_SUCCESS;
    }
    
    lsdj_error_t Importer::importWorkingMemorySong(lsdj_sav_t* sav, const std::vector<ghc::filesystem::path>& paths)
    {
        lsdj_project_t* project = nullptr;
        lsdj_error_t error = lsdj_project_read_lsdsng_from_file(workingMemoryPath.string().c_str(), &project, nullptr);
        if (error != LSDJ_SUCCESS)
            return error;
        assert(project != nullptr);
        
        const auto song = lsdj_project_get_song_const(project);
        lsdj_sav_set_working_memory_song(sav, song);
        
        // Find out if one of the slots has the same name as the working memory filename
        const auto str = workingMemoryPath.stem().string();
        const auto stem = str.substr(0, str.size() - 3);
        for (int i = 0; i != paths.size(); ++i)
        {
            if (stem == paths[i].stem().string())
            {
                lsdj_sav_set_active_project_index(sav, i);
                break;
            }
        }
        
        lsdj_project_free(project);
        
        return LSDJ_SUCCESS;
    }
}
