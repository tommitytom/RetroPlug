#pragma once

#include "tao/json.hpp"
#include "plugs/RetroPlug.h"
#include "config.h"
#include "plugs/SameBoyPlug.h"
#include <string>
#include "base64.h"
#include "roms/Lsdj.h"

static void Serialize(IByteChunk& chunk, const RetroPlug& plug) {
	const SameBoyPlugPtr plugPtr = plug.plug();

	size_t stateSize = plugPtr->saveStateSize();
	tao::binary d;
	d.resize(stateSize);
	plugPtr->saveState((char*)d.data(), stateSize);

	tao::json::value settings = {
		{ "sameBoy", {
			{ "colorCorrection", "emulateHardware" },
			{ "highpassFilter", "accurate" }
		} }
	};

	const Lsdj& lsdj = plug.lsdj();
	if (lsdj.found) {
		std::string syncMode = syncModeToString(lsdj.syncMode);
		if (syncMode.size() > 0) {
			const tao::json::value lsdjSettings = {
				{ "syncMode", syncMode },
				{ "autoPlay", lsdj.autoPlay.load() },
				{ "keyboardShortcuts", lsdj.keyboardShortcuts.load() }
			};

			settings.emplace("lsdj", lsdjSettings);
		}
	}

	const tao::json::value root = {
		{ "version", PLUG_VERSION_STR },
		{ "romPath", plug.romPath() },
		{ "settings", settings },
		{ "state", {
			{ "type", "state" },
			{ "data", d }
		} }
	};

	std::string data = tao::json::to_string<tao::json::events::binary_to_base64>(root);
	chunk.PutStr(data.c_str());
}

static int Deserialize(const IByteChunk& chunk, RetroPlug& plug, int pos) {
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
		
		const tao::json::value* settings = root.find("settings");
		if (settings) {
			const tao::json::value* lsdjSettings = settings->find("lsdj");
			if (lsdjSettings) {
				const std::string& syncMode = lsdjSettings->at("syncMode").get_string();
				plug.lsdj().syncMode = syncModeFromString(syncMode);

				const tao::json::value* autoPlay = lsdjSettings->find("autoPlay");
				if (autoPlay) {
					plug.lsdj().autoPlay = autoPlay->get_boolean();
				}

				const tao::json::value* keyboardShortcuts = lsdjSettings->find("keyboardShortcuts");
				if (autoPlay) {
					plug.lsdj().keyboardShortcuts = keyboardShortcuts->get_boolean();
				}
			}
		}
	}

	return pos;
}
