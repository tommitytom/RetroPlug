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
#include <popl/popl.hpp>

#include <lsdj/version.h>

#include "../common/common.hpp"
#include "mono_processor.hpp"

void printHelp(const popl::OptionParser& options)
{
    std::cout << "lsdj-mono mymusic.sav|mymusic.lsdsng ...\n\n"
              << "Version: " << LSDJ_VERSION_STRING << "\n\n"
              << options << "\n\n";

    std::cout << "LibLsdj is open source and freely available to anyone.\nIf you'd like to show your appreciation, please consider\n  - buying one of my albums (https://4ntler.bandcamp.com)\n  - donating money through PayPal (https://paypal.me/4ntler).\n";
}

int main(int argc, char* argv[])
{
    popl::OptionParser options("Options");
    auto help = options.add<popl::Switch>("h", "help", "Show the help screen");
    auto verbose = options.add<popl::Switch>("v", "verbose", "Verbose output during import");
    auto instrument = options.add<popl::Switch>("i", "instrument", "Only adjust instruments");
    auto table = options.add<popl::Switch>("t", "table", "Only adjust tables");
    auto phrase = options.add<popl::Switch>("p", "phrase", "Only adjust phrases");
    
    try
    {
        options.parse(argc, argv);
        
        const auto inputs = options.non_option_args();
        
        if (help->is_set())
        {
            printHelp(options);
            return 0;
        } else if (!inputs.empty()) {
            
            lsdj::MonoProcessor processor;
            
            processor.verbose = verbose->is_set();
            processor.processInstruments = instrument->is_set();
            processor.processPhrases = phrase->is_set();
            processor.processTables = table->is_set();
            if (!processor.processInstruments && !processor.processPhrases && !processor.processTables)
                processor.processInstruments = processor.processPhrases = processor.processTables = true;
            
            for (auto& input : inputs)
            {
                if (!processor.process(ghc::filesystem::absolute(input)))
                    return 1;
            }
            
            return 0;
        } else {
            printHelp(options);
            return 0;
        }
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    } catch (...) {
        std::cerr << "unknown error" << std::endl;
    }

	return 0;
}
