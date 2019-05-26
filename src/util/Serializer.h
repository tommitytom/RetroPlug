#pragma once

#include "tao/json.hpp"
#include "plugs/RetroPlug.h"
#include "config.h"
#include "plugs/SameBoyPlug.h"
#include <string>
#include "base64.h"
#include "roms/Lsdj.h"

static void Serialize(IByteChunk& chunk, const RetroPlug& plug) {
	const SameBoyPlugPtr* plugs = plug.plugs();
	tao::json::value root = {
		{ "instances", tao::json::value::array({}) }
	};

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		const SameBoyPlugPtr plug = plugs[i];
		if (plug) {
			size_t stateSize = plug->saveStateSize();
			tao::binary d;
			d.resize(stateSize);
			plug->saveState((char*)d.data(), stateSize);

			tao::json::value settings = {
				{ "sameBoy", {
					{ "colorCorrection", "emulateHardware" },
					{ "highpassFilter", "accurate" }
				} }
			};

			const Lsdj& lsdj = plug->lsdj();
			if (lsdj.found) {
				const tao::json::value lsdjSettings = {
					{ "syncMode", syncModeToString(lsdj.syncMode) },
					{ "autoPlay", lsdj.autoPlay.load() },
					{ "keyboardShortcuts", lsdj.keyboardShortcuts.load() }
				};

				settings.emplace("lsdj", lsdjSettings);
			}

			const tao::json::value instRoot = {
				{ "version", PLUG_VERSION_STR },
				{ "romPath", plug->romPath() },
				{ "settings", settings },
				{ "state", {
					{ "type", "state" },
					{ "data", d }
				} }
			};

			root.at("instances").get_array().push_back(instRoot);
		}

		std::string data = tao::json::to_string<tao::json::events::binary_to_base64>(root);
		chunk.PutStr(data.c_str());
	}
}

static void DeserializeInstance(const tao::json::value& instRoot, RetroPlug& plug) {
	const std::string& romPath = instRoot.at("romPath").get_string();
	const auto& state = instRoot.at("state").get_object();
	const std::string& stateDataStr = state.at("data").get_string();
	std::string stateData = base64_decode(stateDataStr);

	if (std::filesystem::exists(romPath)) {
		SameBoyPlugPtr plugPtr = plug.addInstance(EmulatorType::SameBoy);
		plugPtr->loadState((char*)stateData.data(), stateData.size());

		const tao::json::value* settings = instRoot.find("settings");
		if (settings) {
			const tao::json::value* lsdjSettings = settings->find("lsdj");
			if (lsdjSettings) {
				const std::string& syncMode = lsdjSettings->at("syncMode").get_string();
				plugPtr->lsdj().syncMode = syncModeFromString(syncMode);

				const tao::json::value* autoPlay = lsdjSettings->find("autoPlay");
				if (autoPlay) {
					plugPtr->lsdj().autoPlay = autoPlay->get_boolean();
				}

				const tao::json::value* keyboardShortcuts = lsdjSettings->find("keyboardShortcuts");
				if (keyboardShortcuts) {
					plugPtr->lsdj().keyboardShortcuts = keyboardShortcuts->get_boolean();
				}
			}
		}
	}
}

static int Deserialize(const IByteChunk& chunk, RetroPlug& plug, int pos) {
	WDL_String data;
	pos = chunk.GetStr(data, pos);

	const tao::json::value root = tao::json::from_string(data.Get());
	const std::string& version = root.at("version").get_string();
	if (version == "0.0.3") {
		const tao::json::value* instances = root.find("instances");
		if (instances) {
			for (auto& instance : instances->get_array()) {
				DeserializeInstance(instance, plug);
			}
		}
	} else {
		DeserializeInstance(root, plug);
	}

	return pos;
}
