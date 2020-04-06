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

#include "exporter.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

#include <lsdj/sav.h>
#include <lsdj/song.h>

#include "../common/common.hpp"

namespace lsdj
{
    int Exporter::exportProjects(const ghc::filesystem::path& path, const std::string& output)
    {
        // Load in the save file
        lsdj_sav_t* sav = nullptr;
        lsdj_error_t error = lsdj_sav_read_from_file(path.string().c_str(), &sav, nullptr);
        if (error != LSDJ_SUCCESS)
            return handle_error(error);
        assert(sav != nullptr);
        
        if (verbose)
            std::cout << "Read '" << path.string() << "'" << std::endl;
        
        const auto outputFolder = ghc::filesystem::absolute(output);
        
        if (shouldExportWorkingMemory())
        {
            lsdj_project_t* project = nullptr;
            error = lsdj_project_new_from_working_memory_song(sav, &project, nullptr);
            if (error != LSDJ_SUCCESS)
            {
                lsdj_sav_free(sav);
                return handle_error(error);
            }
            
            error = exportProject(project, outputFolder, true);
            if (error != LSDJ_SUCCESS)
            {
                lsdj_sav_free(sav);
                return handle_error(error);
            }
        }
        
        // Go through every project
        for (int i = 0; i < LSDJ_SAV_PROJECT_COUNT; ++i)
        {
            // Retrieve the project
            const lsdj_project_t* project = lsdj_sav_get_project_const(sav, i);
            if (project == NULL)
                continue;
            
            // See if we're using indices and this project hasn't been specified
            // If so, skip it and move on to the next one
            if (!indices.empty() && std::find(std::begin(indices), std::end(indices), i) == std::end(indices))
                continue;
            
            // See if we're using name-based specification and whether this project has been singled out
            // If not, skip it and move on to the next one
            if (!names.empty())
            {
                const char* name = lsdj_project_get_name(project);
                const auto namestr = std::string(name, strnlen(name, LSDJ_PROJECT_NAME_LENGTH));
                if (std::find_if(std::begin(names), std::end(names), [&](const auto& x){ return compareCaseInsensitive(x, namestr); }) == std::end(names))
                    continue;
            }
            
            // Export the project
            error = exportProject(project, outputFolder, false);
            if (error != LSDJ_SUCCESS)
            {
                lsdj_sav_free(sav);
                return handle_error(error);
            }
        }
        
        lsdj_sav_free(sav);
        
        return 0;
    }
    
    lsdj_error_t Exporter::exportProject(const lsdj_project_t* project, ghc::filesystem::path folder, bool workingMemory)
    {
        auto name = constructName(project);
        if (name.empty())
            name = "(EMPTY)";
        
        ghc::filesystem::path path = folder;
        
        if (putInFolder)
            path /= name;
        ghc::filesystem::create_directories(folder);
        
        std::stringstream stream;
        stream << name << convertVersionToString(lsdj_project_get_version(project), true);
        
        if (workingMemory)
            stream << ".WM";
        
        stream << ".lsdsng";
        path /= stream.str();
        
        ghc::filesystem::create_directories(path.parent_path());
        lsdj_error_t error = lsdj_project_write_lsdsng_to_file(project, path.string().c_str(), nullptr);
        if (error != LSDJ_SUCCESS)
            return error;
        
        // Let the user know if verbose output has been toggled on
        if (verbose)
        {
            std::cout << "Exported " << ghc::filesystem::relative(path, folder).string() << std::endl;
        }
        
        return LSDJ_SUCCESS;
    }
    
    int Exporter::print(const ghc::filesystem::path& path)
    {
        if (ghc::filesystem::is_directory(path))
            return printFolder(path);
        else
            return printSav(path);
    }
    
    int Exporter::printFolder(const ghc::filesystem::path& path)
    {
        for (auto it = ghc::filesystem::directory_iterator(path); it != ghc::filesystem::directory_iterator(); ++it)
        {
            const auto path = it->path();
            if (isHiddenFile(path.filename().string()) || path.extension() != ".sav")
                continue;
            
            std::cout << path.filename().string() << std::endl;
            if (printSav(path) != 0)
                return 1;
        }
        
        return 0;
    }
    
    int Exporter::printSav(const ghc::filesystem::path& path)
    {
        // Try and read the sav
        lsdj_sav_t* sav = nullptr;
        lsdj_error_t error = lsdj_sav_read_from_file(path.string().c_str(), &sav, nullptr);
        if (error != LSDJ_SUCCESS)
            return lsdj::handle_error(error);
        assert(sav != nullptr);
        
        // Header
        std::cout << "#   Name       ";
        if (versionStyle != VersionStyle::NONE)
            std::cout << "Ver  ";
        std::cout << "Fmt  BPM" << std::endl;
        
        if (shouldExportWorkingMemory())
        {
            printWorkingMemorySong(sav);
        }
        
        // Find out what the last non-empty project is
        int lastNonEmptyProject = LSDJ_SAV_PROJECT_COUNT - 1;
        for ( ; lastNonEmptyProject != 0; lastNonEmptyProject -= 1)
        {
            const auto project = lsdj_sav_get_project_const(sav, lastNonEmptyProject);
            if (project)
                break;
        }
        
        // Go through all compressed projects
        for (int i = 0; i <= lastNonEmptyProject; i++)
        {
            // If indices were specified and this project wasn't one of them, move on to the next
            if (!indices.empty() && std::find(std::begin(indices), std::end(indices), i) == std::end(indices))
                continue;
            
            printProject(sav, i);
        }
        
        return 0;
    }
    
    std::string Exporter::convertVersionToString(uint8_t version, bool prefixDot) const
    {
        std::ostringstream stream;
        
        switch (versionStyle)
        {
            case VersionStyle::NONE:
                break;
            case VersionStyle::HEX:
                if (prefixDot)
                    stream << '.';
                stream << std::uppercase << std::setfill(' ') << std::setw(2) << std::hex << static_cast<unsigned int>(version);
                break;
            case VersionStyle::DECIMAL:
                if (prefixDot)
                    stream << '.';
                stream << std::setfill('0') << std::setw(3) << static_cast<unsigned int>(version);
                break;
        }
        
        return stream.str();
    }
    
    void Exporter::printWorkingMemorySong(const lsdj_sav_t* sav)
    {
        std::cout << "WM  ";
        
        // If the working memory song represent one of the projects, display that name
        const auto active = lsdj_sav_get_active_project_index(sav);
        if (active != LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX)
        {
            const lsdj_project_t* project = lsdj_sav_get_project_const(sav, active);
            
            const auto name = constructName(project);
            std::cout << name;
            for (auto i = name.length(); i < 11; i += 1)
                std::cout << ' ';
        } else {
            // The working memory doesn't represent one of the projects, so it
            // doesn't really have a name
            std::cout << "          ";
        }
        
        const lsdj_song_t* song = lsdj_sav_get_working_memory_song_const(sav);
        
        // Display whether the working memory song is "dirty"/edited, and display that
        // as version number (it doesn't really have a version number otherwise)
        if (versionStyle != VersionStyle::NONE && lsdj_song_has_changed(song))
            std::cout << "*    ";
        else
            std::cout << "     ";
        
        // Retrieve the sav format version of the song and display it as well
        const auto versionString = std::to_string(lsdj_song_get_format_version(song));
        std::cout << versionString;
        for (auto i = 0; i < 5 - versionString.length(); i++)
            std::cout << ' ';
        
        // Display the bpm of the project
        if (song)
        {
            int tempo = lsdj_song_get_tempo(song);
            std::cout << tempo;
        }
        
        std::cout << std::endl;
    }

    void Exporter::printProject(const lsdj_sav_t* sav, std::size_t index)
    {
        // Retrieve the project
        const lsdj_project_t* project = lsdj_sav_get_project_const(sav, index);
        
        // See if there's actually a song here. If not, this is an (EMPTY) project among
        // existing projects, which is a thing that can happen in older versions of LSDJ
        // Since we're printing, we should show the user this slot is effectively empty
        if (!project)
        {
            std::cout << "(EMPTY)" << std::endl;
            return;
        }
        
        // See if we're using name-based specification and whether this project has been singled out
        // If not, skip it and move on to the next one
        if (!names.empty())
        {
            const char* name = lsdj_project_get_name(project);
            const auto namestr = std::string(name, strnlen(name, LSDJ_PROJECT_NAME_LENGTH));
            if (std::find_if(std::begin(names), std::end(names), [&](const auto& x){ return lsdj::compareCaseInsensitive(x, namestr); }) == std::end(names))
                return;
        }
        
        // Print out the index
        std::cout << std::to_string(index) << "  ";
        if (index < 10)
            std::cout << ' ';
        
        // Display the name of the project
        const auto name = constructName(project);
        std::cout << name;
        
        for (auto i = 0; i < (11 - name.length()); ++i)
            std::cout << ' ';
        
        // Display the version number of the project
        const auto songVersionString = convertVersionToString(lsdj_project_get_version(project), false);
        std::cout << songVersionString;
        for (auto i = songVersionString.size(); i < 5; i += 1)
            std::cout << ' ';
        
        // Retrieve the format version of the song to display
        const lsdj_song_t* song = lsdj_project_get_song_const(project);
        const auto formatVersionString = std::to_string(lsdj_song_get_format_version(song));
        std::cout << formatVersionString;
        for (auto i = 0; i < 5 - formatVersionString.length(); i++)
            std::cout << ' ';
        
        // Display the bpm of the project
        if (song)
        {
            int tempo = lsdj_song_get_tempo(song);
            std::cout << std::setfill(' ') << std::setw(3) << tempo;
        }
        
        std::cout << std::endl;
    }
    
    bool Exporter::shouldExportWorkingMemory()
    {
        // No specific indices were given, export working memory based on --skip-working
        if (indices.empty() && names.empty())
        {
            return !skipWorkingMemory;
        }
        // Export based on -w or --index -1
        return std::find(std::begin(indices), std::end(indices), -1) != std::end(indices);
    }

    std::string Exporter::constructName(const lsdj_project_t* project)
    {
        return constructProjectName(project, underscore);
    }
}
