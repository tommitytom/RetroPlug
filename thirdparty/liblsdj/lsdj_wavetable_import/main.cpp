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

#include <ghc/filesystem.hpp>
#include <popl/popl.hpp>

#include <lsdj/project.h>
#include <lsdj/sav.h>
#include <lsdj/version.h>

#include "../common/common.hpp"
#include "wavetable_importer.hpp"

void printHelp(const popl::OptionParser& options)
{
    std::cout << "lsdj-wavetable-import source.lsdsng wavetables.snt --[synth 0-F | index 00-FF]\n\n"
              << "Version: " << LSDJ_VERSION_STRING << "\n\n"
              << options << "\n";

    std::cout << "LibLsdj is open source and freely available to anyone.\nIf you'd like to show your appreciation, please consider\n  - buying one of my albums (https://4ntler.bandcamp.com)\n  - donating money through PayPal (https://paypal.me/4ntler).\n";
}

uint8_t parseSynthIndex(const std::string& str)
{
    return static_cast<uint8_t>(std::stoul(str, nullptr, 16)) * 16;
}

uint8_t parseIndex(const std::string& str)
{
    return static_cast<uint8_t>(std::stoul(str, nullptr, 16));
}

int main(int argc, char* argv[])
{
    popl::OptionParser options("Options");
    auto help = options.add<popl::Switch>("h", "help", "Show the help screen");
    auto verbose = options.add<popl::Switch>("v", "verbose", "Verbose output during import");
    auto index = options.add<popl::Value<std::string>>("i", "index", "The wavetable index 00-FF where the wavetable data should be written");
    auto synth = options.add<popl::Value<std::string>>("s", "synth", "The synth number 0-F where the wavetable data should be written");
    auto zero = options.add<popl::Switch>("0", "zero", "Pad the synth with empty wavetables if the .snt file < 256 bytes");
    auto force = options.add<popl::Switch>("f", "force", "Force writing the wavetables, even though non-default data may be in them");
    auto output = options.add<popl::Value<std::string>>("o", "output", "The output .lsdsng to write to");
    
    try
    {
        options.parse(argc, argv);
        
        const auto inputs = options.non_option_args();
        
        if (help->is_set())
        {
            printHelp(options);
            return 0;
        }
        else if (inputs.size() == 2 && (synth->is_set() || index->is_set()))
        {
            lsdj::WavetableImporter importer;
                        
            std::string source;
            std::string wavetable;
            
            if (lsdj_sav_is_likely_valid_file(inputs[0].c_str()) ||
                lsdj_project_is_likely_valid_lsdsng_file(inputs[0].c_str()))
            {
                source = inputs[0];
                wavetable = inputs[1];
            }
            else if (lsdj_sav_is_likely_valid_file(inputs[1].c_str()) ||
                     lsdj_project_is_likely_valid_lsdsng_file(inputs[1].c_str()))
            {
                source = inputs[1];
                wavetable = inputs[0];
            }
            else
            {
                std::cerr << "Neither of the inputs is likely a valid .lsdsng or .sav" << std::endl;
                return 1;
            }
            
            importer.outputName = output->is_set() ? output->value() : source;
            importer.wavetableIndex = synth->is_set() ? parseSynthIndex(synth->value()) : parseIndex(index->value());
            importer.zero = zero->is_set();
            importer.force = force->is_set();
            importer.verbose = verbose->is_set();
            
            return importer.import(source, wavetable) ? 0 : 1;
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
