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

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "../liblsdj/sav.h"
#include "exporter.hpp"

int main(int argc, char* argv[])
{
    // Setup the command-line options
    boost::program_options::options_description hidden{"Hidden"};
    hidden.add_options()
        ("file", boost::program_options::value<std::string>(), "Input save file, or folder for print");
    
    boost::program_options::options_description cmd{"Options"};
    cmd.add_options()
        ("help,h", "Help screen")
        ("noversion", "Don't add version numbers to the filename")
        ("folder,f", "Put every lsdsng in its own folder")
        ("print,p", "Print a list of all songs in the sav")
        ("decimal,d", "Use decimal notation for the version number, instead of hex")
        ("underscore,u", "Use an underscore for the special lightning bolt character, instead of x")
        ("output,o", boost::program_options::value<std::string>()->default_value(""), "Output folder for the lsdsng's")
        ("verbose,v", "Verbose output during export")
        ("index,i", boost::program_options::value<std::vector<int>>(), "Single out a given project index to export, 0 or more")
        ("name,n", boost::program_options::value<std::vector<std::string>>(), "Single out a given project by name to export")
        ("working-memory,w", "Single out the working-memory song to export");
    
    boost::program_options::options_description options;
    options.add(cmd).add(hidden);
    
    // Set up the input file command-line argument
    boost::program_options::positional_options_description positionalOptions;
    positionalOptions.add("file", 1);
    
    try
    {
        // Parse the command-line options
        boost::program_options::variables_map vm;
        boost::program_options::command_line_parser parser(argc, argv);
        parser = parser.options(options);
        parser = parser.positional(positionalOptions);
        boost::program_options::store(parser.run(), vm);
        boost::program_options::notify(vm);
        
        // Show help if requested
        if (vm.count("help"))
        {
            std::cout << cmd << std::endl;
            return 0;
        // Do we have an input file?
        } else if (vm.count("file")) {
            // What is the path of the input file, and does it exist on disk?
            const auto path = boost::filesystem::absolute(vm["file"].as<std::string>());
            if (!boost::filesystem::exists(path))
            {
                std::cerr << "Path '" << path.string() << "' does not exist" << std::endl;
                return 1;
            }
            
            // Create the exporter, that will do the work
            lsdj::Exporter exporter;
            
            // Parse some of the flags manipulating output "style"
            exporter.versionStyle = vm.count("noversion") ? lsdj::Exporter::VersionStyle::NONE : vm.count("decimal") ? lsdj::Exporter::VersionStyle::DECIMAL : lsdj::Exporter::VersionStyle::HEX;
            exporter.underscore = vm.count("underscore");
            exporter.putInFolder = vm.count("folder");
            exporter.verbose = vm.count("verbose");
            
            // Has the user specified one or more specific indices to export?
            if (vm.count("index"))
                exporter.indices = vm["index"].as<std::vector<int>>();
            if (vm.count("working-memory"))
                exporter.indices.emplace_back(-1); // -1 represents working memory, kind-of a hack, but meh :/
            if (vm.count("name"))
                exporter.names = vm["name"].as<std::vector<std::string>>();

            // Has the user requested a print, or an actual export?
            if (vm.count("print"))
                return exporter.print(path);
            else
                return exporter.exportProjects(path, vm["output"].as<std::string>());
        } else {
            std::cout << cmd << std::endl;
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
