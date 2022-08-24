#include <algorithm>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <sol/sol.hpp>

#include "threadpool.h"
#include "util.h"
#include "templates.h"
#include "logger.h"

struct ModuleDesc {
	std::string name;
	fs::path rootPath;
	std::vector<ScriptDesc> scripts;
	bool compile;
	bool valid = true;
};

struct CompilerState {
	std::vector<ModuleDesc> modules;
	ThreadPool pool;
};

void compileScript(const std::string& path, std::vector<u8>& data) {
	sol::state ctx;
	sol::load_result lr = ctx.load_file(path);
	if (lr.valid()) {
		sol::protected_function target = lr.get<sol::protected_function>();
		sol::bytecode byteCode = target.dump();
		std::string_view view = byteCode.as_string_view();

		data.resize(view.size());
		memcpy(data.data(), view.data(), view.size());
	} else {
		std::cout << "Failed to compile " << path << std::endl;
	}
}

void loadScript(const std::string& path, std::vector<u8>& target) {
	std::string data = readTextFile(path);
	target.resize(data.size());
	memcpy(target.data(), data.data(), data.size());
}

void processModule(ModuleDesc& mod, CompilerState& state) {
	for (size_t i = 0; i < mod.scripts.size(); ++i) {
		ScriptDesc* s = &mod.scripts[i];
		std::string fullPath = (mod.rootPath / s->path).make_preferred().string();
		//Logger::log(mod.name + "::" + s->name);
		if (mod.compile) {
			compileScript(fullPath, s->data);
		} else {
			loadScript(fullPath, s->data);
		}
	}
}

void writeHeaderFile(CompilerState& state, const fs::path& targetDir) {
	fs::path targetHeaderPath = targetDir / "CompiledScripts.h";

	std::stringstream ss;
	ss << HEADER_CODE_TEMPLATE;

	std::sort(state.modules.begin(), state.modules.end(), [](ModuleDesc& l, ModuleDesc& r) {
		return l.name < r.name;
	});

	for (auto& mod : state.modules) {
		if (mod.valid) {
			ss << "namespace " << mod.name << " {";
			ss << HEADER_FUNCS_TEMPLATE << "}" << std::endl << std::endl;
		}
	}

	ss << "}" << std::endl;

	writeIfDifferent(targetHeaderPath, ss.str());
}

void writeSourceFile(const ModuleDesc& mod, const fs::path& targetDir, const std::string& postfix) {
	fs::path targetFile = targetDir / "CompiledScripts_";
	targetFile += mod.name + postfix + ".cpp";

	std::stringstream vars;
	std::stringstream lookup;

	lookup << "ScriptLookup _lookup = {" << std::endl;

	for (const ScriptDesc& compiled : mod.scripts) {
		if (compiled.data.size() > 0) {
			vars << "const std::uint8_t " << compiled.varName << "[] = { ";
			for (size_t i = 0; i < compiled.data.size(); ++i) {
				if (i != 0) vars << ", ";
				vars << (u32)compiled.data[i];
			}

			vars << " };" << std::endl;

			lookup << "\t{ \"" << 
				compiled.name << "\", { " << 
				compiled.varName << ", " << 
				compiled.data.size() << ", " << 
				(mod.compile ? "true" : "false") << " } }," << 
				std::endl;
		}
	}

	lookup << "};" << std::endl;

	std::stringstream ss;
	ss << SOURCE_HEADER_TEMPLATE;
	ss << mod.name << " {" << std::endl << std::endl;
	ss << vars.str() << std::endl;
	ss << lookup.str();
	ss << SOURCE_FOOTER_TEMPLATE;

	writeIfDifferent(targetFile, ss.str());
}

int main(int argc, char** argv) {
	auto startTime = std::chrono::high_resolution_clock::now();

	{
		CompilerState state;

		fs::path configPath = parsePath(argv[1]);
		fs::path configDir = configPath.parent_path();

		std::string postfix;
		if (argc == 3) {
			postfix += "_" + std::string(argv[2]);
		}

		if (!fs::exists(configPath)) {
			std::cout << "No config found at " << configPath << std::endl;
			return 1;
		}

		sol::state ctx;
		sol::protected_function_result res = ctx.do_file(configPath.string());
		if (!res.valid()) {
			sol::error err = res;
			std::string what = "Failed to load config: ";
			throw std::runtime_error(what + err.what());
		}

		sol::table obj = res.get<sol::table>();

		sol::table settings = obj["settings"];
		sol::table modules = obj["modules"];

		fs::path targetDir = (configDir / settings["outDir"].get<std::string>()).make_preferred();
		fs::create_directories(targetDir);

		size_t objSize = 0;
		for (const auto& v : modules) objSize++;

		state.modules.resize(objSize);

		size_t idx = 0;
		for (const auto& v : modules) {
			ModuleDesc& mod = state.modules[idx++];

			std::string name = v.first.as<std::string>();
			sol::table settings = v.second.as<sol::table>();

			fs::path path = settings["path"].get<std::string>();
			sol::optional<bool> compileOpt = settings["compile"];

			mod.name = name;
			mod.compile = !compileOpt.has_value() || compileOpt.value();
			mod.rootPath = configDir / path.make_preferred();

			if (fs::exists(mod.rootPath)) {
				parseDirectory(mod.rootPath.string(), mod.scripts);
			} else {
				std::cout << "Failed to compile module: folder does not exist.  " << mod.rootPath << std::endl;
				mod.valid = false;
				return 1;
			}
		}

		size_t threadCount = std::min(state.modules.size(), (size_t)std::thread::hardware_concurrency());
		state.pool.start(threadCount);

		for (auto& ns : state.modules) {
			if (ns.valid) {
				state.pool.enqueue([&]() {
					processModule(ns, state);
					writeSourceFile(ns, targetDir, postfix);
				});
			}
		}

		writeHeaderFile(state, targetDir);

		state.pool.wait();

		auto endTime = std::chrono::high_resolution_clock::now();
		auto ms = endTime - startTime;
		std::cout << "Lua compile time: " << std::chrono::duration_cast<std::chrono::milliseconds>(ms).count() << "ms" << std::endl;
	}

	return 0;
}
