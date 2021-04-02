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

#include <popl/popl.hpp>
#include <ghc/filesystem.hpp>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include <lsdj/sav.h>
#include <lsdj/version.h>

#include "../common/common.hpp"
#include "exporter.hpp"

void printHelp(const popl::OptionParser& options)
{
    std::cout << "lsdsng-export mymusic.sav|folder\n\n"
              << "Version: " << LSDJ_VERSION_STRING << "\n\n"
              << options << "\n";

    std::cout << "LibLSDJ is open source and freely available to anyone.\nIf you'd like to show your appreciation, please consider\n  - buying one of my albums (https://4ntler.bandcamp.com)\n  - donating money through PayPal (https://paypal.me/4ntler).\n";
}

int main(int argc, char* argv[])
{
    popl::OptionParser options("Options");
    auto help = options.add<popl::Switch>("h", "help", "Show the help screen");
    auto verbose = options.add<popl::Switch>("v", "verbose", "Verbose output during export");
    auto noversion = options.add<popl::Switch>("", "noversion", "Don't add version numbers to the filename");
    auto folder = options.add<popl::Switch>("f", "folder", "Put every lsdsng in its own folder");
    auto print = options.add<popl::Switch>("p", "print", "Print a list of all songs in the sav, instead of exporting");
    auto decimal = options.add<popl::Switch>("d", "decimal", "Use decimal notation for the version number, instead of hex");
    auto underscore = options.add<popl::Switch>("u", "underscore", "Use an underscore for the special lightning bolt character, instead of x");
    auto output = options.add<popl::Value<std::string>>("o", "output", "Output folder for the lsdsng's", "");
    auto index = options.add<popl::Value<int>>("i", "index", "Single out a given project index to export, 0 or more");
    auto name = options.add<popl::Value<std::string>>("n", "name", "Single out a given project by name to export");
    auto wm = options.add<popl::Switch>("w", "working-memory", "Single out the working-memory song to export");
    auto skipWorkingMemory = options.add<popl::Switch>("", "skip-working", "Do not export the song in working-memory when no other projects are given");

    try
    {
        options.parse(argc, argv);
        
        const auto inputs = options.non_option_args();
        
        // Show help if requested
        if (help->is_set())
        {
            printHelp(options);
            return 0;
        // Do we have an input file?
        } else if (!inputs.empty()) {
            // What is the path of the input file, and does it exist on disk?
            const auto path = ghc::filesystem::absolute(inputs.front());
            if (!ghc::filesystem::exists(path))
            {
                std::cerr << "Path '" << path.string() << "' does not exist" << std::endl;
                return 1;
            }

            // Find conflicting arguments
            if (wm->is_set() && skipWorkingMemory->is_set())
            {
                std::cerr << "Incompatible arguments: --working-memory and --skip-working";
                return 1;
            }
            
            // Create the exporter, that will do the work
            lsdj::Exporter exporter;
            
            // Parse some of the flags manipulating output "style"
            exporter.versionStyle = noversion->is_set() ? lsdj::Exporter::VersionStyle::NONE : decimal->is_set() ? lsdj::Exporter::VersionStyle::DECIMAL : lsdj::Exporter::VersionStyle::HEX;
            exporter.underscore = underscore->is_set();
            exporter.putInFolder = folder->is_set();
            exporter.verbose = verbose->is_set();
            exporter.skipWorkingMemory = skipWorkingMemory->is_set();

            // Has the user specified one or more specific indices to export?
            if (index->is_set())
            {
                for (auto i = 0; i < index->count(); i++)
                    exporter.indices.emplace_back(index->value(i));
            }
            
            if (name->is_set())
            {
                for (auto i = 0; i < name->count(); i++)
                    exporter.names.emplace_back(name->value(i));
            }
            
            if (wm->is_set())
                exporter.indices.emplace_back(-1); // -1 represents working memory, kind-of a hack, but meh :/

            // Has the user requested a print, or an actual export?
            if (print->is_set()) {
                return exporter.print(path);
            } else {
                exporter.output = output->value();
                return exporter.export_(path);
            }
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
