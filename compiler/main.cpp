extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <algorithm>
#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>

namespace fs = std::filesystem;
using u8 = unsigned char;

struct CompiledScript {
	std::string name;
	size_t size;
};

struct RawScript {
	fs::path path;
	std::string content;
};

const char* rawHeaderCode = R"(// GENERATED! CHANGES WILL BE OVERWRITTEN!

#include "ConfigScripts.h"

)";

const char* headerCode = R"(// GENERATED! CHANGES WILL BE OVERWRITTEN!

#include "CompiledLua.h"

#ifdef COMPILE_LUA_SCRIPTS

#include <unordered_map>
#include <string>

struct CompiledScript {
	const unsigned char* data;
	size_t size;
};

)";

const char* loaderCode = R"(
const std::vector<const char*>& getScriptNames() { return _scriptNames; }

int compiledScriptLoader(lua_State* state) {
	const char* name = lua_tostring(state, -1);
	auto found = _compiledScripts.find(name);
	if (found != _compiledScripts.end()) {
		luaL_loadbuffer(state, (const char*)found->second.data, found->second.size, name);
		return 1;
	}

	return 0;
}

)";

static int writer(lua_State* L, const void* p, size_t sz, void* ud) {
	const u8* data = (const u8*)p;
	std::vector<u8>* target = (std::vector<u8>*)ud;

	for (size_t i = 0; i < sz; ++i) {
		target->push_back(data[i]);
	}

	return 0;
}

std::string getScriptName(fs::path path) {
	std::string name = path.replace_extension().string().substr(2);
	std::replace(name.begin(), name.end(), '\\', '.');
	std::replace(name.begin(), name.end(), '/', '.');
	return name;
}

std::string getScriptVarName(std::string name) {
	std::replace(name.begin(), name.end(), '.', '_');
	std::replace(name.begin(), name.end(), '-', '_');
	return "_" + name + "_LUA_";
}

std::string readTextFile(const fs::path& path) {
	std::ifstream file(path);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());;
}

bool parseConfigDirectory(fs::path dirPath, fs::path rootDirPath, std::vector<RawScript>& descs) {
	bool error = false;
	for (auto& p : fs::directory_iterator(dirPath)) {
		if (p.is_directory()) {
			error = parseConfigDirectory(p, rootDirPath, descs) || error;
		} else {
			fs::path path = p.path();
			if (path.extension() != ".lua") {
				continue;
			}

			std::string relativePath = path.string().substr(rootDirPath.string().size());
			std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
			std::cout << relativePath << std::endl;

			std::string content = readTextFile(path);

			RawScript s = { relativePath, content };
			descs.push_back(s);
		}
	}

	return error;
}

bool parseDirectory(fs::path dirPath, std::stringstream& out, std::vector<CompiledScript>& descs) {
	bool error = false;
	for (auto& p : fs::directory_iterator(dirPath)) {
		if (p.is_directory()) {
			error = parseDirectory(p, out, descs) || error;
		} else {
			fs::path path = p.path();
			if (path.extension() != ".lua") {
				continue;
			}
			
			std::string name = getScriptName(path);

			std::cout << name;

			std::string target;
			{
				std::ifstream f(path);
				std::stringstream ss;
				ss << f.rdbuf();
				target = ss.str();
			}

			std::vector<u8> data;
			{
				lua_State* L = luaL_newstate();
				if (luaL_loadstring(L, target.c_str()) == LUA_OK) {
					lua_dump(L, writer, &data, 0);
				} else {
					const char* err = lua_tostring(L, -1);
					std::cout << " FAILED: " << err << std::endl;
					error = true;
				}
				
				lua_close(L);
			}

			if (data.empty()) {
				continue;
			}

			CompiledScript s = { name, data.size() };
			descs.push_back(s);

			out << "const unsigned char " << getScriptVarName(s.name) << "[] = { ";
			for (size_t i = 0; i < data.size(); ++i) {
				if (i != 0) out << ", ";
				out << (unsigned int)data[i];
			}

			out << " };" << std::endl;
			std::cout << std::endl;
		}
	}

	return error;
}

int main(int argc, char** argv) {
	bool error = false;
	fs::current_path(argv[1]);

	std::cout << "Compiling scripts in " << argv[1] << std::endl;
	{
		std::vector<CompiledScript> descs;
		std::stringstream out;
	
		out << headerCode;
		error = parseDirectory(".", out, descs);
		out << std::endl;

		out << "std::unordered_map<std::string_view, CompiledScript> _compiledScripts = {" << std::endl;
		for (size_t i = 0; i < descs.size(); ++i) {
			out << "\t{ \"" << descs[i].name << "\", { " << getScriptVarName(descs[i].name) << ", " << descs[i].size << " } }," << std::endl;
		}
		out << "};" << std::endl << std::endl;

		out << "std::vector<const char*> _scriptNames = {" << std::endl;
		for (size_t i = 0; i < descs.size(); ++i) {
			out << "\t\"" << descs[i].name << "\"," << std::endl;
		}
		out << "};" << std::endl << std::endl;

		out << loaderCode;
		out << "#endif" << std::endl;

		std::ofstream outf(argv[3]);
		outf << out.str();
	}

	fs::current_path(argv[2]);
	std::cout << "Compiling raw scripts in " << argv[2] << std::endl;

	{
		std::stringstream out;
		std::vector<RawScript> rawDescs;

		out << rawHeaderCode;
		error = parseConfigDirectory("./", "./", rawDescs) || error;

		out << "std::array<RawScript, " << rawDescs.size() << "> _rawScripts = {" << std::endl;
		for (size_t i = 0; i < rawDescs.size(); ++i) {
			out << "\tRawScript { " << rawDescs[i].path << ", R\"(" << rawDescs[i].content << ")\" }," << std::endl;
		}
		out << "};" << std::endl << std::endl;

		out << "const std::array<RawScript, 2>& getRawScripts() { return _rawScripts; }" << std::endl;

		std::ofstream outf(argv[4]);
		outf << out.str();
	}

	return error == false ? 0 : 1;
}
