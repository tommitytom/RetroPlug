extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;

using u8 = unsigned char;

const char* headerCode = R"(#include <map>
#include <string>
#include "CompiledLua.h"

struct CompiledScript {
	const char* data;
	size_t size;
};

)";

const char* loaderCode = R"(
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

struct CompiledScript {
	std::string name;
	size_t size;
};

std::string getScriptVarName(std::string name) {
	return "_" + name + "_LUA_";
}

int main() {
	std::cout << fs::current_path() << std::endl;

	std::vector<CompiledScript> descs;

	std::stringstream out;
	out << headerCode;

	for (auto& p : fs::directory_iterator("..\\src\\scripts")) {
		fs::path path = p.path();
		std::cout << "Compiling " << path.string() << std::endl;

		std::string target;
		{
			std::ifstream f(p.path());
			std::stringstream ss;
			ss << f.rdbuf();
			target = ss.str(); 
		}
		
		std::vector<u8> data;
		{
			lua_State* L = luaL_newstate();
			luaL_loadstring(L, target.c_str());
			lua_dump(L, writer, &data, 0);
			lua_close(L);
		}

		CompiledScript s = { path.filename().replace_extension().string(), data.size() };
		descs.push_back(s);

		out << "const char " << getScriptVarName(s.name) << "[" << s.size << "] = { ";
		for (size_t i = 0; i < data.size(); ++i) {
			if (i != 0) out << ", ";
			out << (unsigned int)data[i];
		}

		out << " };" << std::endl;	
	}

	out << std::endl;

	out << "std::map<std::string, CompiledScript> _compiledScripts = {" << std::endl;
	for (size_t i = 0; i < descs.size(); ++i) {
		out << "\t{ \"" << descs[i].name << "\", { " << getScriptVarName(descs[i].name) << ", " << descs[i].size << " } }," << std::endl;
	}
	
	out << "};" << std::endl;

	out << loaderCode;

	{
		std::ofstream outf("..\\src\\model\\CompiledLua.cpp");
		outf << out.str();
	}
}
