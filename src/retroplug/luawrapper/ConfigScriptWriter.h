#pragma once

#include <iostream>
#include "util/fs.h"
#include "util/File.h"
#include "generated/CompiledScripts.h"

namespace ConfigScriptWriter {
	static std::string getScriptPath(fs::path path) {
		std::string name = path.string();
		std::replace(name.begin(), name.end(), '.', '/');
		return name + ".lua";
	}

	void write(fs::path configDir) {
		if (!fs::exists(configDir)) {
			fs::create_directories(configDir);
		}

		std::vector<std::string_view> names;
		CompiledScripts::config::getScriptNames(names);

		for (std::string_view name: names) {
			std::string path = getScriptPath(name);

			fs::path fullPath = (configDir / path).make_preferred();
			fs::path dir = fullPath.parent_path();

			if (!fs::exists(dir)) {
				if (!fs::create_directories(dir)) {
					std::cout << "Failed to create directory " << dir << std::endl;
				}
			}

			if (!fs::exists(fullPath)) {
				const CompiledScripts::Script* script = CompiledScripts::config::getScript(name);

				if (!writeFile(tstr(fullPath.wstring()), (std::byte*)script->data, script->size, false)) {
					std::cout << "Failed to write file " << fullPath << std::endl;
				}
			}
		}
	}
}
