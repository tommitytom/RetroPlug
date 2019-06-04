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

static std::string modelToString(GameboyModel model) {
	switch (model) {
	case GameboyModel::DmgB: return "DMG_B";
	case GameboyModel::CgbC: return "CGB_C";
	case GameboyModel::CgbE: return "CGB_E";
	case GameboyModel::Agb: return "AGB";
	}

	return "CGB_E";
}

static GameboyModel stringToModel(const std::string& model) {
	if (model == "DMG_B") return GameboyModel::DmgB;
	if (model == "CGB_C") return GameboyModel::CgbC;
	if (model == "CGB_E") return GameboyModel::CgbE;
	if (model == "AGB") return GameboyModel::Agb;
	return GameboyModel::Auto;
}

static std::string saveTypeToString(SaveStateType type) {
	switch (type) {
	case SaveStateType::State: return "state";
	case SaveStateType::Sram: return "sram";
	}

	return "state";
}

static SaveStateType stringToSaveType(const std::string& model) {
	if (model == "state") return SaveStateType::State;
	if (model == "sram") return SaveStateType::Sram;
	return SaveStateType::State;
}

static std::string mutliChannelModeToString(MultiChannelMode type) {
	switch (type) {
	case MultiChannelMode::Channel: return "channel";
	case MultiChannelMode::Instance: return "instance";
	}

	return "off";
}

static MultiChannelMode stringToMutliChannelMode(const std::string& model) {
	if (model == "channel") return MultiChannelMode::Channel;
	if (model == "instance") return MultiChannelMode::Instance;
	return MultiChannelMode::Off;
}

static void Serialize(std::string& target, const RetroPlug& manager) {
	const SameBoyPlugPtr* plugs = manager.plugs();
	tao::json::value root = {
		{ "version", PLUG_VERSION_STR },
		{ "layout", layoutToString(manager.layout()) },
		{ "saveType", saveTypeToString(manager.saveType()) },
		{ "multiChannel", mutliChannelModeToString(manager.multiChannelMode()) },
		{ "instances", tao::json::value::array({}) }
	};

	if (!manager.projectPath().empty()) {
		root.emplace("lastProjectPath", ws2s(manager.projectPath()));
	}

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		const SameBoyPlugPtr plug = plugs[i];
		if (plug) {
			tao::json::value settings = {
				{ "gameBoy", {
					{ "model", modelToString(plug->model()) },
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

			tao::binary saveState;
			if (manager.saveType() == SaveStateType::State) {
				plug->saveState(saveState);
			} else {
				plug->saveBattery(saveState);
			}

			tao::json::value instRoot = {
				{ "romPath", ws2s(plug->romPath()) },
				{ "settings", settings },
				{ "state", {
					{ "data", saveState }
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

static void DeserializeInstance(const tao::json::value& instRoot, RetroPlug& plug, SaveStateType saveType) {
	const std::string& romPath = instRoot.at("romPath").get_string();
	const auto& state = instRoot.at("state").get_object();
	const std::string& stateDataStr = state.at("data").get_string();
	std::string stateData = base64_decode(stateDataStr);

	GameboyModel model = GameboyModel::Auto;
	const tao::json::value* settings = instRoot.find("settings");
	if (settings) {
		const tao::json::value* gameboySettings = settings->find("gameBoy");
		if (gameboySettings) {
			const tao::json::value* modelName = gameboySettings->find("model");
				
			if (modelName) {
				model = stringToModel(modelName->get_string());
			}
		}
	}

	SameBoyPlugPtr plugPtr = plug.addInstance(EmulatorType::SameBoy);
	plugPtr->setModel(model);

	if (std::filesystem::exists(romPath)) {
		plugPtr->init(s2ws(romPath), model, true);
	} else {
		plugPtr->setRomPath(s2ws(romPath));
	}

	plug.setSaveType(saveType);
	switch (saveType) {
	case SaveStateType::State: plugPtr->loadState((std::byte*)stateData.data(), stateData.size()); break;
	case SaveStateType::Sram: plugPtr->loadBattery((std::byte*)stateData.data(), stateData.size(), true); break;
	}

	const tao::json::value* savePath = instRoot.find("lastSramPath");
	if (savePath) {
		plugPtr->setSavePath(s2ws(savePath->get_string()));
	}

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

static void Deserialize(const char* data, RetroPlug& plug) {
	plug.clear();

	try {
		const tao::json::value root = tao::json::from_string(data);
		const std::string& version = root.at("version").get_string();
		if (version == "0.1.0") {
			SaveStateType saveType = SaveStateType::State;
			const tao::json::value* saveTypeStr = root.find("saveType");
			if (saveTypeStr) {
				saveType = stringToSaveType(saveTypeStr->get_string());
			}

			plug.setSaveType(saveType);

			const tao::json::value* instances = root.find("instances");
			if (instances) {
				for (auto& instance : instances->get_array()) {
					DeserializeInstance(instance, plug, saveType);
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

			const tao::json::value* mutliChannel = root.find("multiChannel");
			if (mutliChannel) {
				MultiChannelMode mode = stringToMutliChannelMode(mutliChannel->get_string());
				plug.setMultiChannelMode(mode);
			}
		} else {
			DeserializeInstance(root, plug, SaveStateType::State);
		}
	} catch (...) {
		// Fail
	}
}
