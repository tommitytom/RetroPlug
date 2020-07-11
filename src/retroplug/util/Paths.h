#pragma once

#include <sstream>
#include <semver/semver.hpp>
#include "platform/Path.h"
#include "fs.h"
#include "config.h"

static fs::path getConfigPath() {
	semver::version v{ PLUG_VERSION_STR };
	std::stringstream ss;
	ss << std::to_string(v.major) << "." << std::to_string(v.minor);
	std::string s = ss.str();
	return getContentPath(fs::path(ss.str()));
}
