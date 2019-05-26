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

#include <boost/program_options.hpp>

#include <iostream>

#include "importer.hpp"

std::string generateOutputFilename(const std::vector<std::string>& inputs)
{
    // If we've got no output file and are only importing one folder,
    // we take that folder name as output. In case of multiple folders,
    if (inputs.size() == 1)
    {
        const auto path = boost::filesystem::absolute(inputs.front()).remove_trailing_separator();
        if (boost::filesystem::is_directory(path))
            return path.stem().filename().string() + ".sav";
    }
    
    return "out.sav";
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description hidden{"Hidden"};
    hidden.add_options()
        ("file", boost::program_options::value<std::vector<std::string>>(), ".lsdsng file(s), 0 or more");
    
    boost::program_options::options_description cmd{"Options"};
    cmd.add_options()
        ("help,h", "Help screen")
        ("output,o", boost::program_options::value<std::string>(), "The output file (.sav)")
        ("sav,s", boost::program_options::value<std::string>(), "A sav file to append all .lsdsng's to")
        ("verbose,v", "Verbose output during import");
    
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
            std::cout << cmd << std::endl;
            return 0;
        } else if (vm.count("file")) {
            lsdj::Importer importer;
            
            importer.inputs = vm["file"].as<std::vector<std::string>>();
            importer.verbose = vm.count("verbose");
            
            if (vm.count("output"))
                importer.outputFile = vm["output"].as<std::string>();
            else if (vm.count("sav"))
                importer.outputFile = boost::filesystem::absolute(vm["sav"].as<std::string>()).stem().filename().string() + ".sav";
            else
                importer.outputFile = generateOutputFilename(importer.inputs);
            
            return importer.importSongs(vm.count("sav") ? vm["sav"].as<std::string>().c_str() : nullptr);
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
