#pragma once

#include "tao/json.hpp"
#include "plugs/RetroPlug.h"
#include "config.h"
#include "plugs/SameBoyPlug.h"
#include <string>
#include "base64.h"

void Serialize(IByteChunk& chunk, const RetroPlug& plug) {
	const SameBoyPlugPtr plugPtr = plug.plug();

	size_t stateSize = plugPtr->saveStateSize();
	tao::binary d;
	d.resize(stateSize);
	plugPtr->saveState((char*)d.data(), stateSize);

	const tao::json::value root = {
		{ "version", PLUG_VERSION_STR },
		{ "romPath", plug.romPath() },
		{ "state", {
			{ "type", "state" },
			{ "data", d }
		} }
	};

	std::string data = tao::json::to_string<tao::json::events::binary_to_base64>(root);
	chunk.PutStr(data.c_str());
}

int Deserialize(const IByteChunk& chunk, RetroPlug& plug, int pos) {
	WDL_String data;
	pos = chunk.GetStr(data, pos);

	const tao::json::value root =  tao::json::from_string< tao::json::events::binary_to_base64>(data.Get());
	const std::string& version = root.at("version").get_string();
	const std::string& romPath = root.at("romPath").get_string();
	const auto& state = root.at("state").get_object();
	const std::string& stateDataStr = state.at("data").get_string();
	std::string stateData = base64_decode(stateDataStr);

	if (std::filesystem::exists(romPath)) {
		plug.load(EmulatorType::SameBoy, romPath);
		const SameBoyPlugPtr plugPtr = plug.plug();
		plugPtr->loadState((char*)stateData.data(), stateData.size());
	}

	return pos;
}