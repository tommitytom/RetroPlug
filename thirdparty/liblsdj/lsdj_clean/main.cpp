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

#include <iostream>

#include <ghc/filesystem.hpp>
#include <boost/program_options.hpp>

#include "../common/common.hpp"
#include "clean_processor.hpp"

void printHelp(const boost::program_options::options_description& desc)
{
    std::cout << "lsdj-clean mymusic.sav|mymusic.lsdsng ...\n\n"
              << "Version: " << lsdj::VERSION << "\n\n"
              << desc << "\n\n";

    std::cout << "LibLsdj is open source and freely available to anyone.\nIf you'd like to show your appreciation, please consider\n  - buying one of my albums (https://4ntler.bandcamp.com)\n  - donating money through PayPal (https://paypal.me/4ntler).\n";
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description hidden{"Hidden"};
    hidden.add_options()
        ("file", boost::program_options::value<std::vector<std::string>>(), ".sav or .lsdng file(s), 0 or more");
    
    boost::program_options::options_description cmd{"Options"};
    cmd.add_options()
        ("help,h", "Help screen")
        ("verbose,v", "Verbose output during processing")
        ("instrument,i", "Only adjust instruments")
        ("table,t", "Only adjust tables")
        ("phrase,p", "Only adjust phrases");
    
    boost::program_options::options_description options;
    options.add(cmd).add(hidden);
    
    boost::program_options::positional_options_description positionalOptions;
    positionalOptions.add("file", -1);
    
    try
    {
        boost::program_options::variables_map vm;
        boost::program_options::command_line_parser parser(argc, argv);
        parser = parser.options(options);
        parser = parser.positional(positionalOptions);
        boost::program_options::store(parser.run(), vm);
        boost::program_options::notify(vm);
        
        if (vm.count("help"))
        {
            printHelp(cmd);
            return 0;
        } else if (vm.count("file")) {
            
            lsdj::CleanProcessor processor;
            
            processor.verbose = vm.count("verbose");
            processor.processInstruments = vm.count("instrument");
            processor.processPhrases = vm.count("phrase");
            processor.processTables = vm.count("table");
            if (!processor.processInstruments && !processor.processPhrases && !processor.processTables)
                processor.processInstruments = processor.processPhrases = processor.processTables = true;
            
            for (auto& input : vm["file"].as<std::vector<std::string>>())
            {
                if (!processor.process(ghc::filesystem::absolute(input)))
                    return 1;
            }
            
            return 0;
        } else {
            printHelp(cmd);
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
