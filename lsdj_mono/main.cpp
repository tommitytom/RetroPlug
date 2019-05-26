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

#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "../common/common.hpp"
#include "../liblsdj/sav.h"

bool verbose = false;
bool convertInstruments = false;
bool convertTables = false;
bool convertPhrases = false;

boost::filesystem::path addMonoSuffix(const boost::filesystem::path& path)
{
    return path.parent_path() / (path.stem().string() + ".MONO" + path.extension().string());
}

bool alreadyEndsWithMono(const boost::filesystem::path& path)
{
    const auto stem = path.stem().string();
    return stem.size() >= 5 && stem.substr(stem.size() - 5) == ".MONO";
}

void convertInstrument(lsdj_instrument_t* instrument)
{
    if (instrument != nullptr && lsdj_instrument_get_panning(instrument) != LSDJ_PAN_NONE)
        lsdj_instrument_set_panning(instrument, LSDJ_PAN_LEFT_RIGHT);
}

void convertCommand(lsdj_command_t* command)
{
    if (command->command == LSDJ_COMMAND_O && command->value != LSDJ_PAN_NONE)
    {
        command->value = LSDJ_PAN_LEFT_RIGHT;
    }
}

void convertTable(lsdj_table_t* table)
{
    if (table == nullptr)
        return;
    
    for (int i = 0; i < LSDJ_TABLE_LENGTH; ++i)
    {
        convertCommand(lsdj_table_get_command1(table, i));
        convertCommand(lsdj_table_get_command2(table, i));
    }
}

void convertPhrase(lsdj_phrase_t* phrase)
{
    if (phrase == nullptr)
        return;
    
    for (int i = 0; i < LSDJ_PHRASE_LENGTH; ++i)
        convertCommand(&phrase->commands[i]);
}

void convertSong(lsdj_song_t* song)
{
    if (convertInstruments)
    {
        for (int i = 0; i < LSDJ_INSTRUMENT_COUNT; ++i)
            convertInstrument(lsdj_song_get_instrument(song, i));
    }
    
    if (convertTables)
    {
        for (int i = 0; i < LSDJ_TABLE_COUNT; ++i)
            convertTable(lsdj_song_get_table(song, i));
    }
    
    if (convertPhrases)
    {
        for (int i = 0; i < LSDJ_PHRASE_COUNT; ++i)
            convertPhrase(lsdj_song_get_phrase(song, i));
    }
}

int processSav(const boost::filesystem::path& path)
{
    if (alreadyEndsWithMono(path))
        return 0;
    
    lsdj_error_t* error = nullptr;
    lsdj_sav_t* sav = lsdj_sav_read_from_file(path.string().c_str(), &error);
    if (error != nullptr)
    {
        lsdj_sav_free(sav);
        return 1;
    }
    
    if (verbose)
        std::cout << "Processing '" + path.string() + "'" << std::endl;
    
    convertSong(lsdj_sav_get_working_memory_song(sav));
    
    for (int i = 0; i < lsdj_sav_get_project_count(sav); ++i)
    {
        lsdj_project_t* project = lsdj_sav_get_project(sav, i);
        if (project == nullptr)
            continue;
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        if (song == nullptr)
            continue;
        
        convertSong(song);
    }
    
    lsdj_sav_write_to_file(sav, addMonoSuffix(path).string().c_str(), &error);
    if (error != nullptr)
    {
        lsdj_sav_free(sav);
        return 1;
    }
    
    lsdj_sav_free(sav);
    
    return 0;
}

int processLsdsng(const boost::filesystem::path& path)
{
    if (alreadyEndsWithMono(path))
        return 0;
    
    lsdj_error_t* error = nullptr;
    lsdj_project_t* project = lsdj_project_read_lsdsng_from_file(path.string().c_str(), &error);
    if (error != nullptr)
    {
        lsdj_project_free(project);
        return 1;
    }
    
    if (verbose)
        std::cout << "Processing '" + path.string() + "'" << std::endl;
    
    lsdj_song_t* song = lsdj_project_get_song(project);
    if (song == nullptr)
        return 0;
    
    convertSong(song);
    
    lsdj_project_write_lsdsng_to_file(project, addMonoSuffix(path).string().c_str(), &error);
    if (error != nullptr)
    {
        lsdj_project_free(project);
        return 1;
    }
    
    lsdj_project_free(project);
    
    return 0;
}

int process(const boost::filesystem::path& path);

int processDirectory(const boost::filesystem::path& path)
{
    for (auto it = boost::filesystem::directory_iterator(path); it != boost::filesystem::directory_iterator(); ++it)
    {
        if (process(it->path()) != 0)
            return 1;
    }
    
    return 0;
}

int process(const boost::filesystem::path& path)
{
    if (lsdj::isHiddenFile(path.filename().string()))
        return 0;
    
    if (boost::filesystem::is_directory(path))
    {
        if (processDirectory(path) != 0)
            return 1;
    }
    else if (path.extension() == ".sav")
    {
        if (processSav(path) != 0)
            return 1;
    }
    else if (path.extension() == ".lsdsng")
    {
        if (processLsdsng(path) != 0)
            return 1;
    }
    
    return 0;
}

int process(const std::vector<std::string>& inputs)
{
    for (auto& input : inputs)
    {
        const auto path = boost::filesystem::absolute(input);
        if (process(path) != 0)
            return 1;
    }
    
    return 0;
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Help screen")
        ("file", boost::program_options::value<std::vector<std::string>>(), ".sav or .lsdng file(s), 0 or more")
        ("verbose,v", "Verbose output during import")
        ("instrument,i", "Only adjust instruments")
        ("table,t", "Only adjust tables")
        ("phrase,p", "Only adjust phrases");
    
    boost::program_options::positional_options_description positionalOptions;
    positionalOptions.add("file", -1);
    
    try
    {
        boost::program_options::variables_map vm;
        boost::program_options::command_line_parser parser(argc, argv);
        parser = parser.options(desc);
        parser = parser.positional(positionalOptions);
        boost::program_options::store(parser.run(), vm);
        boost::program_options::notify(vm);
        
        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return 0;
        } else if (vm.count("file")) {
            verbose = vm.count("verbose");
            convertInstruments = vm.count("instrument");
            convertPhrases = vm.count("phrase");
            convertTables = vm.count("table");
            if (!convertInstruments && !convertPhrases && !convertTables)
                convertInstruments = convertPhrases = convertTables = true;
            
            return process(vm["file"].as<std::vector<std::string>>());
        } else {
            std::cout << desc << std::endl;
            return 0;
        }
    } catch (const boost::program_options::error& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    } catch (...) {
        std::cerr << "unknown error" << std::endl;
    }

	return 0;
}
