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

namespace fs = std::filesystem;
using u8 = unsigned char;

struct CompiledScript {
	std::string name;
	size_t size;
};

const char* headerCode = R"(#include "CompiledLua.h"

#ifdef COMPILE_LUA_SCRIPTS

#include <map>
#include <string>

struct CompiledScript {
	const char* data;
	size_t size;
};

)";

const char* loaderCode = R"(
const std::vector<const char*>& getScriptNames() { return _scriptNames; }

int compiledScriptLoader(lua_State* state) {
	const char* name = lua_tostring(state, -1);
	auto found = _compiledScripts.find(name);
	if (found != _compiledScripts.end()) {
		luaL_loadbuffer(state, found->second.data, found->second.size, name);
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
	return "_" + name + "_LUA_";
}

bool parseDirectory(fs::path dirPath, std::stringstream& out, std::vector<CompiledScript>& descs) {
	bool error = false;
	for (auto& p : fs::directory_iterator(dirPath)) {
		if (p.is_directory()) {
			error = parseDirectory(p, out, descs) || error;
		} else {
			fs::path path = p.path();
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

			out << "const char " << getScriptVarName(s.name) << "[" << s.size << "] = { ";
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
	std::vector<CompiledScript> descs;
	std::stringstream out;

	fs::current_path(argv[1]);
	
	out << headerCode;
	bool error = parseDirectory(".", out, descs);
	out << std::endl;

	out << "std::map<std::string, CompiledScript> _compiledScripts = {" << std::endl;
	for (size_t i = 0; i < descs.size(); ++i) {
		out << "\t{ \"" << descs[i].name << "\", { " << getScriptVarName(descs[i].name) << ", " << descs[i].size << " } }," << std::endl;
	}
	out << "};" << std::endl << std::endl;

	out << "std::vector<const char*> _scriptNames = {" << std::endl;
	for (size_t i = 0; i < descs.size(); ++i) {
		out << "\t\"" << descs[i].name << "\"," << std::endl;
	}
	out << "};" << std::endl;

	out << loaderCode;
	out << "#endif" << std::endl;

	{
		std::ofstream outf(argv[2]);
		outf << out.str();
	}

	return error == false ? 0 : 1;
}
