#pragma once

#include <iostream>
#include "util/fs.h"
#include "util/File.h"
#include "ConfigScripts.h"

namespace ConfigScriptWriter {
	void write(fs::path configDir) {
		fs::create_directories(configDir);

		auto& configScripts = getRawScripts();

		for (RawScript f : configScripts) {
			fs::path path = (configDir / f.path).make_preferred();
			fs::path dir = path.parent_path();

			if (!fs::exists(dir)) {
				if (!fs::create_directories(dir)) {
					std::cout << "Failed to create directory " << dir << std::endl;
				}
			}

			if (!fs::exists(path)) {
				if (!writeFile(tstr(path.wstring()), f.content)) {
					std::cout << "Failed to write file " << f.path << std::endl;
				}
			}
		}
	}
}
