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
#include <popl/popl.hpp>

#include <lsdj/version.h>

#include "../common/common.hpp"
#include "importer.hpp"

void printHelp(const popl::OptionParser& options)
{
    std::cout << "lsdsng-import -o output.sav song1.lsgsng song2.lsdsng...\n\n"
              << "Version: " << LSDJ_VERSION_STRING << "\n\n"
              << options << "\n";

    std::cout << "LibLsdj is open source and freely available to anyone.\nIf you'd like to show your appreciation, please consider\n  - buying one of my albums (https://4ntler.bandcamp.com)\n  - donating money through PayPal (https://paypal.me/4ntler).\n";
}

std::string generateOutputFilename(const std::vector<std::string>& inputs)
{
    // If we've got no output file and are only importing one folder,
    // we take that folder name as output. In case of multiple folders,
    if (inputs.size() == 1)
    {
        const auto path = ghc::filesystem::absolute(inputs.front());
        return path.stem().filename().string() + ".sav";
    }
    
    return "out.sav";
}

int main(int argc, char* argv[])
{
    popl::OptionParser options("Options");
    auto help = options.add<popl::Switch>("h", "help", "Show the help screen");
    auto verbose = options.add<popl::Switch>("v", "verbose", "Verbose output during import");
    auto output = options.add<popl::Value<std::string>>("o", "output", "The output file (.sav)");
    auto sav = options.add<popl::Value<std::string>>("s", "sav", "A sav file to append all .lsdsng's to");
    
    try
    {
        options.parse(argc, argv);
        
        const auto lsdsngs = options.non_option_args();
        
        if (help->is_set())
        {
            printHelp(options);
            return 0;
        } else if (!lsdsngs.empty()) {
            lsdj::Importer importer;
            
            importer.inputs = lsdsngs;
            importer.verbose = verbose->is_set();
            
            if (output->is_set())
                importer.outputFile = output->value();
            else if (sav->is_set())
                importer.outputFile = ghc::filesystem::absolute(sav->value()).stem().filename().string() + ".sav";
            else
                importer.outputFile = generateOutputFilename(importer.inputs);
            
            return importer.importSongs(sav->is_set() ? sav->value().c_str() : nullptr);
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
