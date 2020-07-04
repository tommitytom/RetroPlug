#pragma once

#include <array>

struct RawScript {
	const char* path;
	const char* content;
};

const std::array<RawScript, 2>& getRawScripts();
