#pragma once

struct LuaScriptData {
	const std::uint8_t* data;
	size_t size;
	bool compiled;
};

using ScriptLookup = std::unordered_map<std::string_view, LuaScriptData>;
