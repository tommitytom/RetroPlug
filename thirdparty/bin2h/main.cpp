
/*
Copyright (c) 2011, Matt Gordon
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
in the documentation and/or other materials provided with the distribution.

Neither the name of the author nor the names of its contributors may be used to endorse or promote products 
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string>
#include <iostream>
#include <fstream>
#include <cstdarg>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

//prints help to stdout
void Help()
{
	std::cout << "bin2h utility v1.01\n\n";

	std::cout << "Interprets any file as plain binary data and dumps to a raw C/C++ array.\n";
	std::cout << "usage: bin2h <in-file> <out-file> <opt-args>\n\n";
	
	std::cout << "Valid optional arguments:\n";
	std::cout << "-id=<name> the C array is identified as \"name\". identifier is \"data\" if this argument is not present. bin2h does not check the identifier is valid in C/C++.\n";
	std::cout << "-ns=<namespace> causes the data to be wrapped in a namespace. no namespace is inserted if this argument is not used.\n";
}

//checks if main() argument looks like it might be an (optional) argument
bool IsArg(char* arg)
{
	return (strncmp(arg, "-", 1) == 0);
}

struct Arguments
{
	std::string ns;
	std::string id;

	Arguments(int argc, char* argv[])
		: id("data")
	{
		for(int i=0; i<argc; ++i)
		{
			if(strncmp(argv[i], "-id=", 4) == 0)
			{
				id = argv[i] + 4;
			}
			else if(strncmp(argv[i], "-ns=", 4) == 0)
			{
				ns = argv[i] + 4;
			}
		}
	}
};

int main(int argc, char* argv[])
{
	//argv 1 should be a valid filename, and argv 2 is optionally something that a output stream
	//can be opened against. after that, options.
	if(argc < 2 || IsArg(argv[1]))
	{
		Help();
		return 0;
	}

	bool bFileout = argc >2 && !IsArg(argv[2]);

	//check input file can be opened
	std::ifstream in(argv[1], std::ios_base::binary);
	if(!in.is_open())
	{
		std::cerr << "couldn't open " << argv[1] << " for reading.\n\n";
		Help();
		return 0;
	}

	//check output file can be opened
	std::ofstream outfile;
	if(bFileout)
	{
		outfile.open(argv[2], std::ios_base::trunc);
		if(!outfile.is_open())
		{
			std::cerr << "couldn't open " << argv[2] << "for writing.\n\n";
			Help();
			in.close();
			return 0;
		}
	}	

	//stream to the output file, or std out if no file was provided
	std::ostream& out = outfile.is_open() ? outfile : std::cout;
	Arguments A(argc - (bFileout ? 2:1), argv + (bFileout ? 2:1));

	//write out pre-amble

	//insert a comment to indicate that this file was auto generated from some other file
	out << "//file auto-generated from " << argv[1] << " by bin2h.exe\n";

	bool bWroteNamespace = false;
	if(!A.ns.empty())
	{
		out << "namespace " << A.ns << " \n{\n";
		bWroteNamespace = true;
	}

	//get the file size
	in.seekg(0, std::ios_base::end);
	size_t filesize = in.tellg();
	in.seekg(0, std::ios_base::beg);

	//array size, for use in code
	out << "size_t " << A.id << "_len = " << filesize << ";\n";
	//and now, the array
	out << "unsigned char " << A.id << "[" << filesize << "]=\n{\n\t";

	//stream the data through
	int restart = 0;
	for(size_t i=0; i<filesize; )
	{
		static const size_t bufferLen = 1024;
		//buffer has to be unsigned for the sprintf to work as required.
		unsigned char in_buffer[bufferLen];

		const size_t chunk = i+bufferLen < filesize ? bufferLen : filesize-i;
		in.read(reinterpret_cast<char*>(in_buffer), chunk);

		static const int k_buff = 6;
		char out_buffer[k_buff];
		for(unsigned j=0; j<chunk; ++j)
		{
			snprintf(out_buffer, k_buff, "0x%.2hX,", in_buffer[j]);
			out << out_buffer;

			++restart;
			if(restart > 10)
			{
				out << "\n\t";
				restart = 0;
			}
		}

		i += chunk;		
	}

	//post-amble - close array, then namespace
	out << "\n};\n";
	if(bWroteNamespace)
	{
		out << "}//end namespace\n";
	}

	if(outfile.is_open())
	{
		outfile.close();
	}
	in.close();

	return 0;
}


#ifdef _WIN32
int mainCRTStartup(int argc, wchar_t* argv[])
{
	//This is used on Windows only, where UTF-8 support is (at time of writing) still an opt-in feature.
	//Get a 16bit input from the system and convert to utf-8, then proceed as before.
	//This isn't necessary on macOS where UTF-8 is the default encoding and it all just works.
	char** argv8 = new char*[argc];
	for(int i=0; i<argc; ++i)
	{
		//measure, allocate, convert
		int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, NULL, NULL);
		argv8[i] = new char[n]; //-1 parameter to WideCharToMulteByte yields size in bytes including 0 terminator 
		if( 0 == WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, argv8[i], n, NULL, NULL))
		{
			std::wcerr << L"failed to convert argument " << argv[i] << L"to utf-8, gle=" << GetLastError() <<std::endl;
			return -1;
		}
	}
	
	//can now call with UTF-8 arguments
	int r = main(argc, argv8);

	//deallocate everything
	for(int i=0; i<argc; ++i)
	{
		delete[] argv8[i];
	}
	delete[] argv8;

	return r;
}
#endif