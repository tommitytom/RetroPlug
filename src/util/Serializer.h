#pragma once

#include "tao/json.hpp"
#include "plugs/RetroPlug.h"
#include "config.h"
#include "plugs/SameBoyPlug.h"
#include <string>
#include "base64.h"
#include "roms/Lsdj.h"

static std::string layoutToString(InstanceLayout layout) {
	switch (layout) {
	case InstanceLayout::Auto: return "auto";
	case InstanceLayout::Row: return "row";
	case InstanceLayout::Column: return "column";
	case InstanceLayout::Grid: return "grid";
	}

	return "auto";
}

static InstanceLayout layoutFromString(const std::string& layout) {
	if (layout == "auto") return InstanceLayout::Auto;
	if (layout == "row") return InstanceLayout::Row;
	if (layout == "column") return InstanceLayout::Column;
	if (layout == "grid") return InstanceLayout::Grid;
	return InstanceLayout::Auto;
}

static void Serialize(std::string& target, const RetroPlug& plug) {
	const SameBoyPlugPtr* plugs = plug.plugs();
	tao::json::value root = {
		{ "version", PLUG_VERSION_STR },
		{ "layout", layoutToString(plug.layout()) },
		{ "instances", tao::json::value::array({}) }
	};

	if (!plug.projectPath().empty()) {
		root.emplace("lastProjectPath", ws2s(plug.projectPath()));
	}

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		const SameBoyPlugPtr plug = plugs[i];
		if (plug) {
			size_t stateSize = plug->saveStateSize();
			tao::binary d;
			d.resize(stateSize);
			plug->saveState((char*)d.data(), stateSize);

			tao::json::value settings = {
				{ "gameBoy", {
					{ "gameLink", plug->gameLink() }
				} },
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

			tao::json::value instRoot = {
				{ "romPath", ws2s(plug->romPath()) },
				{ "settings", settings },
				{ "state", {
					{ "type", "state" },
					{ "data", d }
				} }
			};

			if (plug->savePath().size() > 0) {
				instRoot.emplace("lastSramPath", ws2s(plug->savePath()));
			}

			root.at("instances").get_array().push_back(instRoot);
		}

		target = tao::json::to_string<tao::json::events::binary_to_base64>(root);
	}
}

static void DeserializeInstance(const tao::json::value& instRoot, RetroPlug& plug) {
	const std::string& romPath = instRoot.at("romPath").get_string();
	const auto& state = instRoot.at("state").get_object();
	const std::string& stateDataStr = state.at("data").get_string();
	std::string stateData = base64_decode(stateDataStr);

	if (std::filesystem::exists(romPath)) {
		SameBoyPlugPtr plugPtr = plug.addInstance(EmulatorType::SameBoy);
		plugPtr->init(s2ws(romPath), GameboyModel::Auto, false);
		plugPtr->loadState((char*)stateData.data(), stateData.size());

		const tao::json::value* savePath = instRoot.find("lastSramPath");
		if (savePath) {
			plugPtr->setSavePath(s2ws(savePath->get_string()));
		}

		const tao::json::value* settings = instRoot.find("settings");
		if (settings) {
			const tao::json::value* gameboySettings = settings->find("gameBoy");
			if (gameboySettings) {
				const tao::json::value* gameLink = gameboySettings->find("gameLink");
				if (gameLink) {
					plugPtr->setGameLink(gameLink->get_boolean());
				}
			}

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

static void Deserialize(const char* data, RetroPlug& plug) {
	plug.clear();

	try {
		const tao::json::value root = tao::json::from_string(data);
		const std::string& version = root.at("version").get_string();
		if (version == "0.1.0") {
			const tao::json::value* instances = root.find("instances");
			if (instances) {
				for (auto& instance : instances->get_array()) {
					DeserializeInstance(instance, plug);
				}
			}

			const tao::json::value* projPath = root.find("lastProjectPath");
			if (projPath) {
				plug.setProjectPath(s2ws(projPath->get_string()));
			}

			const tao::json::value* layout = root.find("layout");
			if (layout) {
				InstanceLayout layoutType = layoutFromString(layout->get_string());
				plug.setLayout(layoutType);
			}
		} else {
			DeserializeInstance(root, plug);
		}
	} catch (...) {
		// Fail
	}
}
