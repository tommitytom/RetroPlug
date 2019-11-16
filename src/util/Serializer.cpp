#include "Serializer.h"

#include "RetroPlug.h"
#include "config.h"
#include "plugs/SameBoyPlug.h"
#include "base64.h"
#include "roms/Lsdj.h"
#include "fs.h"
#include "util/crc32.h"

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"

#include "config/version.h"

std::string layoutToString(InstanceLayout layout) {
	switch (layout) {
	case InstanceLayout::Auto: return "auto";
	case InstanceLayout::Row: return "row";
	case InstanceLayout::Column: return "column";
	case InstanceLayout::Grid: return "grid";
	}

	return "auto";
}

InstanceLayout layoutFromString(const std::string& layout) {
	if (layout == "auto") return InstanceLayout::Auto;
	if (layout == "row") return InstanceLayout::Row;
	if (layout == "column") return InstanceLayout::Column;
	if (layout == "grid") return InstanceLayout::Grid;
	return InstanceLayout::Auto;
}

std::string modelToString(GameboyModel model) {
	switch (model) {
	case GameboyModel::DmgB: return "DMG_B";
	case GameboyModel::CgbC: return "CGB_C";
	case GameboyModel::CgbE: return "CGB_E";
	case GameboyModel::Agb: return "AGB";
	default: return "CGB_E";
	}
}

GameboyModel stringToModel(const std::string & model) {
	if (model == "DMG_B") return GameboyModel::DmgB;
	if (model == "CGB_C") return GameboyModel::CgbC;
	if (model == "CGB_E") return GameboyModel::CgbE;
	if (model == "AGB") return GameboyModel::Agb;
	return GameboyModel::Auto;
}

std::string saveTypeToString(SaveStateType type) {
	switch (type) {
	case SaveStateType::State: return "state";
	case SaveStateType::Sram: return "sram";
	}

	return "state";
}

SaveStateType stringToSaveType(const std::string & model) {
	if (model == "state") return SaveStateType::State;
	if (model == "sram") return SaveStateType::Sram;
	return SaveStateType::State;
}

std::string audioRoutingToString(AudioChannelRouting  type) {
	switch (type) {
	case AudioChannelRouting::StereoMixDown: return "stereoMixDown";
	case AudioChannelRouting::TwoChannelsPerInstance: return "twoChannelsPerInstance";
	case AudioChannelRouting::TwoChannelsPerChannel: return "twoChannelsPerChannel";
	}

	return "stereoMixDown";
}

AudioChannelRouting stringToAudioRouting(const std::string & model) {
	if (model == "stereoMixDown") return AudioChannelRouting::StereoMixDown;
	if (model == "twoChannelsPerInstance") return AudioChannelRouting::TwoChannelsPerInstance;
	if (model == "twoChannelsPerChannel") return AudioChannelRouting::TwoChannelsPerChannel;
	return AudioChannelRouting::StereoMixDown;
}

std::string midiRoutingToString(MidiChannelRouting  type) {
	switch (type) {

	case MidiChannelRouting::OneChannelPerInstance: return "oneChannelPerInstance";
	case MidiChannelRouting::FourChannelsPerInstance: return "fourChannelsPerInstance";
	case MidiChannelRouting::SendToAll:
	default: return "sendToAll";
	}
}

MidiChannelRouting stringToMidiRouting(const std::string & model) {
	if (model == "fourChannelsPerInstance") return MidiChannelRouting::FourChannelsPerInstance;
	if (model == "oneChannelPerInstance") return MidiChannelRouting::OneChannelPerInstance;
	if (model == "sendToAll") return MidiChannelRouting::SendToAll;
	return MidiChannelRouting::FourChannelsPerInstance;
}

void serialize(std::string & target, const RetroPlug & manager) {
	const SameBoyPlugPtr* plugs = manager.plugs();

	rapidjson::Document root(rapidjson::kObjectType);
	auto& a = root.GetAllocator();

	root.AddMember("version", PLUG_VERSION_STR, a);
	root.AddMember("layout", layoutToString(manager.layout()), a);
	root.AddMember("saveType", saveTypeToString(manager.saveType()), a);
	root.AddMember("audioRouting", audioRoutingToString(manager.audioRouting()), a);
	root.AddMember("midiRouting", midiRoutingToString(manager.midiRouting()), a);

	if (!manager.projectPath().empty()) {
		root.AddMember("lastProjectPath", ws2s(manager.projectPath()), a);
	}

	rapidjson::Value instances(rapidjson::kArrayType);

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		const SameBoyPlugPtr plug = plugs[i];
		if (plug) {
			rapidjson::Value gb(rapidjson::kObjectType);
			gb.AddMember("model", modelToString(plug->model()), a);
			gb.AddMember("gameLink", plug->gameLink(), a);

			rapidjson::Value sb(rapidjson::kObjectType);
			sb.AddMember("colorCorrection", "emulateHardware", a);
			sb.AddMember("highpassFilter", "accurate", a);

			rapidjson::Value rp(rapidjson::kObjectType);
			sb.AddMember("watchRom", plug->watchRom(), a);

			rapidjson::Value settings(rapidjson::kObjectType);
			settings.AddMember("gameBoy", gb, a);
			settings.AddMember("sameBoy", sb, a);
			settings.AddMember("retroPlug", rp, a);

			const Lsdj& lsdj = plug->lsdj();
			if (lsdj.found) {
				rapidjson::Value l(rapidjson::kObjectType);
				l.AddMember("syncMode", syncModeToString(lsdj.syncMode), a);
				l.AddMember("autoPlay", lsdj.autoPlay.load(), a);
				l.AddMember("keyboardShortcuts", lsdj.keyboardShortcuts.load(), a);

				rapidjson::Value kits(rapidjson::kObjectType);
				for (size_t i = 0; i < lsdj.kitData.size(); ++i) {
					auto kit = lsdj.kitData[i];
					if (kit) {
						rapidjson::Value id;
						id.SetString(std::to_string(i), a);

						rapidjson::Value kitData(rapidjson::kObjectType);
						kitData.AddMember("name", kit->name, a);
						kitData.AddMember("checksum", kit->hash, a);

						std::string kitDataStr = base64_encode((const unsigned char*)kit->data.data(), kit->data.size());
						kitData.AddMember("data", kitDataStr, a);

						kits.AddMember(id, kitData, a);
					}
				}

				l.AddMember("kits", kits, a);

				settings.AddMember("lsdj", l, a);
			}

			std::vector<std::byte> saveState;
			if (plug->active()) {
				if (manager.saveType() == SaveStateType::State) {
					plug->saveState(saveState);
				} else {
					plug->saveBattery(saveState);
				}
			}

			std::string saveStateStr = base64_encode((const unsigned char*)saveState.data(), saveState.size());

			rapidjson::Value instRoot(rapidjson::kObjectType);
			instRoot.AddMember("romPath", ws2s(plug->romPath()), a);
			instRoot.AddMember("settings", settings, a);

			rapidjson::Value s(rapidjson::kObjectType);
			s.AddMember("data", saveStateStr, a);
			instRoot.AddMember("state", s, a);

			if (plug->savePath().size() > 0) {
				instRoot.AddMember("lastSramPath", ws2s(plug->savePath()), a);
			}

			instances.PushBack(instRoot, a);
		}
	}

	root.AddMember("instances", instances, a);

	rapidjson::StringBuffer sb;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
	root.Accept(writer);

	target = sb.GetString();
}

void deserializeInstance(const rapidjson::Value& instRoot, RetroPlug& plug, SaveStateType saveType) {
	const std::string& romPath = instRoot["romPath"].GetString();
	const auto& state = instRoot["state"].GetObject();
	const std::string& stateDataStr = state["data"].GetString();
	std::vector<std::byte> stateData = base64_decode(stateDataStr);

	GameboyModel model = GameboyModel::Auto;
	const auto& settings = instRoot.FindMember("settings");
	if (settings != instRoot.MemberEnd()) {
		const auto& gameboySettings = settings->value.FindMember("gameBoy");
		if (gameboySettings != settings->value.MemberEnd()) {
			const auto& modelName = gameboySettings->value.FindMember("model");

			if (modelName != gameboySettings->value.MemberEnd()) {
				model = stringToModel(modelName->value.GetString());
			}
		}
	}

	SameBoyPlugPtr plugPtr = plug.addInstance(EmulatorType::SameBoy);
	plugPtr->setModel(model);

	// TODO: Load and patch rom before loading in SameBoy! Patching also needs to be accounted
	// for when the rom is not found and is loaded later.  This would probably be implemented
	// when replacing and patching a rom

	if (fs::exists(romPath)) {
		plugPtr->init(tstr(romPath), model, true);
	} else {
		plugPtr->setRomPath(tstr(romPath));
	}

	plug.setSaveType(saveType);
	switch (saveType) {
	case SaveStateType::State: plugPtr->loadState((std::byte*)stateData.data(), stateData.size()); break;
	case SaveStateType::Sram: plugPtr->loadBattery((std::byte*)stateData.data(), stateData.size(), true); break;
	}

	const auto& savePath = instRoot.FindMember("lastSramPath");
	if (savePath != instRoot.MemberEnd()) {
		plugPtr->setSavePath(tstr(savePath->value.GetString()));
	}

	if (settings != instRoot.MemberEnd()) {
		const auto& gameboySettings = settings->value.FindMember("gameBoy");
		if (gameboySettings != settings->value.MemberEnd()) {
			const auto& gameLink = gameboySettings->value.FindMember("gameLink");
			if (gameLink != gameboySettings->value.MemberEnd()) {
				plugPtr->setGameLink(gameLink->value.GetBool());
			}
		}

		const auto& rpSettings = settings->value.FindMember("retroPlug");
		if (rpSettings != settings->value.MemberEnd()) {
			const auto& watchRom = rpSettings->value.FindMember("watchRom");
			if (watchRom != rpSettings->value.MemberEnd()) {
				plugPtr->setWatchRom(watchRom->value.GetBool());
			}
		}

		const auto& lsdjSettings = settings->value.FindMember("lsdj");
		if (lsdjSettings != settings->value.MemberEnd()) {
			plugPtr->lsdj().clearKits();

			const std::string& syncMode = lsdjSettings->value["syncMode"].GetString();
			plugPtr->lsdj().syncMode = syncModeFromString(syncMode);

			const auto& autoPlay = lsdjSettings->value.FindMember("autoPlay");
			if (autoPlay != lsdjSettings->value.MemberEnd()) {
				plugPtr->lsdj().autoPlay = autoPlay->value.GetBool();
			}

			const auto& keyboardShortcuts = lsdjSettings->value.FindMember("keyboardShortcuts");
			if (keyboardShortcuts != lsdjSettings->value.MemberEnd()) {
				plugPtr->lsdj().keyboardShortcuts = keyboardShortcuts->value.GetBool();
			}

			const auto& kits = lsdjSettings->value.FindMember("kits"); 
			if (kits != lsdjSettings->value.MemberEnd()) {
				auto& kitsData = plugPtr->lsdj().kitData;
				for (auto it = kits->value.MemberBegin(); it != kits->value.MemberEnd(); ++it) {
					int idx = std::stoi(it->name.GetString());
					
					const auto& kitName = it->value.FindMember("name");
					const auto& kitData = it->value.FindMember("data");
					auto kitDecoded = base64_decode(kitData->value.GetString());

					if (kitName != it->value.MemberEnd() && kitData != it->value.MemberEnd()) {
						kitsData[idx] = std::make_shared<NamedHashedData>(NamedHashedData {
							kitName->value.GetString(),
							kitDecoded,
							crc32::update(kitDecoded)
						});
					}
				}
			}

			plugPtr->lsdj().patchKits(plugPtr->romData());
			plugPtr->updateRom();
		}
	}
}

int versionToInt(const std::string& v) {
	auto items = split(v, ".");
	if (items.size() != 3) {
		return 0;
	}

	int version = 0;
	for (int i = 0; i < 3; i++) {
		int vi = std::stoi(items[i]);
		if (vi > 255) {
			return 0;
		}

		version |= vi << ((2 - i) * 8);
	}

	return version;
}

void deserialize(const char * data, RetroPlug& plug) {
	plug.clear();

	try {
		rapidjson::Document root;
		if (root.Parse(data).HasParseError()) {
			return;
		}

		const std::string& versionStr = root["version"].GetString();
		int version = versionToInt(versionStr);
		if (version >= VERSION_INT(0, 1, 0)) {
			SaveStateType saveType = SaveStateType::State;
			const auto& saveTypeStr = root.FindMember("saveType");
			if (saveTypeStr != root.MemberEnd()) {
				saveType = stringToSaveType(saveTypeStr->value.GetString());
			}

			plug.setSaveType(saveType);

			const auto& instances = root.FindMember("instances");
			if (instances != root.MemberEnd()) {
				for (auto& instance : instances->value.GetArray()) {
					deserializeInstance(instance, plug, saveType);
				}
			}

			const auto& projPath = root.FindMember("lastProjectPath");
			if (projPath != root.MemberEnd()) {
				plug.setProjectPath(tstr(projPath->value.GetString()));
			}

			const auto& layout = root.FindMember("layout");
			if (layout != root.MemberEnd()) {
				InstanceLayout layoutType = layoutFromString(layout->value.GetString());
				plug.setLayout(layoutType);
			}

			const auto& audioRouting = root.FindMember("audioRouting");
			if (audioRouting != root.MemberEnd()) {
				AudioChannelRouting mode = stringToAudioRouting(audioRouting->value.GetString());
				plug.setAudioRouting(mode);
			}

			const auto& midiRouting = root.FindMember("midiRouting");
			if (midiRouting != root.MemberEnd()) {
				MidiChannelRouting mode = stringToMidiRouting(midiRouting->value.GetString());
				plug.setMidiRouting(mode);
			}
		} else {
			deserializeInstance(root, plug, SaveStateType::State);
		}
	} catch (...) {
		// Fail
	}
}
