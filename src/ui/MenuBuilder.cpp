#include "MenuBuilder.h"

#include <unordered_set>

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/SolUtil.h"
#include "foundation/StlUtil.h"
#include "foundation/Shell.h"

#include "core/FileManager.h"
#include "core/InputManager.h"
#include "core/RetroPlugConfig.h"
#include "core/Project.h"
#include "core/ProjectExporter.h"
#include "core/System.h"
#include "util/LoaderUtil.h"
#include "util/RecentUtil.h"
#include "audio/AudioManager.h"

#include "sameboy/SameBoySystem.h"
#include "sameboy/Constants.h"

using namespace rp;

void loadRomDialog(Project& project, SystemPtr system) {
	std::vector<std::string> files;

	if (fw::FileDialog::openFile(files, { ROM_FILTER }, false)) {
		LoadConfig loadConfig = LoadConfig{
			.desc = {
				.paths = {
					.romPath = files[0]
				}
			},
			.romBuffer = std::make_shared<fw::Uint8Buffer>(),
			.sramBuffer = std::make_shared<fw::Uint8Buffer>()
		};

		if (!fw::FsUtil::readFile(files[0], loadConfig.romBuffer.get())) {
			return;
		}

		if (system) {
			SystemDesc desc = system->getDesc();
			desc.paths.romPath = files[0];

			system->setDesc(std::move(desc));
			system->load(std::move(loadConfig));
		} else {
			project.addSystem(SAMEBOY_GUID, std::move(loadConfig));
		}
	}
}

void loadSavDialog(Project& project, SystemPtr system) {
	std::vector<std::string> files;

	if (fw::FileDialog::openFile(files, { SAV_FILTER }, false)) {
		LoadConfig config{
			.sramBuffer = std::make_shared<fw::Uint8Buffer>()
		};

		if (!fw::FsUtil::readFile(files[0], config.sramBuffer.get())) {
			return;
		}

		SystemDesc desc = system->getDesc();
		desc.paths.sramPath = files[0];

		system->setDesc(std::move(desc));
		system->load(std::move(config));
	}
}

bool saveProject(Project& project, FileManager& fileManager, bool forceDialog) {
	std::string path;

	if (!forceDialog) {
		if (project.getState().path == "") {
			forceDialog = true;
		} else {
			path = project.getState().path;
		}
	}

	if (forceDialog) {
		if (!fw::FileDialog::saveFile(path, { PROJECT_FILTER })) {
			return false;
		}
	}

	fileManager.addRecent(RecentFilePath {
		.type = "project",
		.name = project.getName(),
		.path = path,
	});

	return project.save(path);
}

bool saveSram(Project& project, SystemPtr system, bool forceDialog) {
	const SystemDesc& settings = system->getDesc();
	std::string path;

	if (!forceDialog) {
		if (settings.paths.sramPath == "") {
			forceDialog = true;
		} else {
			path = settings.paths.sramPath;
		}
	}

	if (forceDialog) {
		if (!fw::FileDialog::saveFile(path, { SAV_FILTER })) {
			return false;
		}
	}

	spdlog::info("Saving SRAM to {}", path);

	fw::Uint8Buffer target;
	if (system->saveSram(target)) {
		if (fw::FsUtil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write SRAM to file");
	} else {
		spdlog::error("Failled to save SRAM: Failed to get state from system");
	}

	return false;
}

bool saveState(Project& project, SystemPtr system) {
	std::string path;

	if (!fw::FileDialog::saveFile(path, { STATE_FILTER })) {
		return false;
	}

	spdlog::info("Saving state to {}", path);

	fw::Uint8Buffer target;
	if (system->saveState(target)) {
		if (fw::FsUtil::writeFile(path, (const char*)target.data(), target.size())) {
			return true;
		}

		spdlog::error("Failled to write state to file");
	} else {
		spdlog::error("Failled to save state: Failed to get state from system");
	}

	return false;
}

bool handleSystemLoad(const fs::path& romPath, const fs::path& savPath, SystemPtr system) {
	std::vector<std::byte> fileData = fw::FsUtil::readFile(romPath);

	SystemDesc desc = system->getDesc();
	desc.paths.romPath = romPath.string();

	system->load({
		.desc = std::move(desc),
		.romBuffer = std::make_shared<fw::Uint8Buffer>((uint8*)fileData.data(), fileData.size()),
		.reset = true
	});

	return true;
}

void MenuBuilder::projectMenu(fw::Menu& root, FileManager& fileManager, Project& project, System& system) {
	root
		.action("New", [&project]() { project.clear(); })
		.action("Load...", [&fileManager, &project](fw::MenuContext& ctx) {
			ctx.retain();

			std::vector<std::string> files;
			if (fw::FileDialog::openFile(files, { ROM_FILTER, PROJECT_FILTER }, true, false)) {
				LoaderUtil::handleLoad(files, fileManager, project);
				ctx.close();
			}
		})
		.action("Save", [&fileManager, &project]() {
			if (saveProject(project, fileManager, false)) {
				spdlog::info("Project saved successfully");
			} else {
				spdlog::error("Failed to save project");
			}
		})
		.action("Save As...", [&fileManager, &project]() {
			if (saveProject(project, fileManager, true)) {
				spdlog::info("Project saved successfully");
			} else {
				spdlog::error("Failed to save project");
			}
		})
		.action("Export all ROMs + SAVs", [&project]() {
			fw::Uint8Buffer target;
			if (ProjectExporter::exportRomsAndSavs(project, target)) {
				fw::FileDialog::saveFileData(target, { ZIP_FILTER }, project.getName() + ".zip");
			}
		})
		.separator()
		.multiSelect("Zoom", { "1x", "2x", "3x", "4x", "5x", "6x" }, &project.getState().settings.zoom)
		.multiSelect("Layout", { "Auto", "Row", "Column", "Grid" }, (int)project.getState().settings.layout, [&project](int layout) {
			project.getState().settings.layout = (SystemLayout)layout;
		})
		.multiSelect("MIDI", { "Send To All", "Four Channels Per Instance", "One Channel Per Instance" }, &project.getState().settings.midiRouting)
		.multiSelect("Audio Routing", { "Stereo Mix Down", "Two Channels Per Instance", "Two Channels Per Channel" }, &project.getState().settings.audioRouting);
}

void MenuBuilder::systemMenu(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system) {
	root
		.select("Game Link", system->getGameLink(), [system](bool val) { system->setGameLink(val); })
		.separator()
		.action("Load ROM...", [&project]() { loadRomDialog(project, nullptr); })
		.action("Reset", []() {})
		.separator()
		.action("Clear SRAM", []() {})
		.action("Save SRAM", []() {})
		.action("Save SRAM As...", []() {})
		.separator()
		.action("Clear State", []() {})
		.action("Save State", []() {})
		.action("Save State as...", []() {});
}

struct SettingsMenuState {
	std::vector<std::string> audioOut;
	std::vector<std::string> inputConfigs;
	
	int32 audioOutDeviceId = 0;
	int32 keyConfigId = 0;
	int32 padConfigId = 0;
};

void MenuBuilder::settingsMenu(fw::Menu& root, InputManager& inputManager, Project& project, GlobalSettings& settings, fw::audio::AudioManager& audioManager) {
	auto state = std::make_shared<SettingsMenuState>();

#ifndef RP_WEB
	std::vector<std::string> audioIn;
	audioManager.getDeviceNames(audioIn, state->audioOut);

	std::string activeAudioDeviceName = audioManager.getActiveOutputName();
	spdlog::info("Active audio device: {}", activeAudioDeviceName);

	int32 audioOutDevice = fw::StlUtil::getVectorIndex(state->audioOut, activeAudioDeviceName);
	if (audioOutDevice < 0) {
		audioOutDevice = 0; // Default to first device if not found
	}

	state->audioOutDeviceId = (uint32)audioOutDevice;

	root.multiSelect("Audio Out", state->audioOut, audioOutDevice, [state](int v) { state->audioOutDeviceId = (uint32)v; })
		.separator();
#endif

	for (const auto& config : inputManager.getAvailableConfigs()) {
		state->inputConfigs.push_back(config.name + (config.valid ? "" : " [!]"));
	}

	state->keyConfigId = inputManager.getSelectedIndex(InputType::Key);
	state->padConfigId = inputManager.getSelectedIndex(InputType::Pad);

	root
		.multiSelect("Keyboard", state->inputConfigs, state->keyConfigId, [state](int idx) { state->keyConfigId = idx; })
		.multiSelect("Pad", state->inputConfigs, state->padConfigId, [state](int idx) { state->padConfigId = idx; })
		.separator()
		.action("Apply", [&audioManager, &inputManager, state](fw::MenuContext& ctx) {
			auto inputConfigs = inputManager.getAvailableConfigs();
			if (state->keyConfigId >= 0 && state->keyConfigId < (int32)inputConfigs.size()) {
				auto config = inputConfigs[state->keyConfigId];
				if (config.valid) {
					inputManager.load(inputConfigs[state->keyConfigId].name, InputType::Key);
				}
			}

			if (state->padConfigId >= 0 && state->padConfigId < (int32)inputConfigs.size()) {
				auto config = inputConfigs[state->padConfigId];
				if (config.valid) {
					inputManager.load(inputConfigs[state->padConfigId].name, InputType::Pad);
				}
			}

			audioManager.setAudioDevice((uint32)state->audioOutDeviceId);

			ctx.retain();
		})
		.separator()
		.action("Open Settings Folder", []() { fw::openShellFolder(FileManager::getContentPath().string()); });
}

void MenuBuilder::commonMenu(fw::Menu& root, FileManager& fileManager, Project& project, System& system) {
	root.subMenu("Add System")
			.action("Duplicate Current", [&system, &fileManager, &project]() {
				SystemDesc desc = system.getDesc();
				desc.paths.sramPath = fileManager.getUniqueFilename(desc.paths.sramPath).string();
				project.duplicateSystem(system.getId()/*, desc*/);
			})
			.action("ROM...", [&project]() { loadRomDialog(project, nullptr); })
			.parent()
		.action("Remove System", [&project, &system]() {
			if (project.getSystems().size() > 1) {
				project.removeSystem(system.getId());
			}
		}, project.getSystems().size() > 1)
		;
}

void MenuBuilder::populateRecent(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system) {
	std::vector<RecentFilePath> paths;
	fileManager.loadRecent(paths);

	for (const RecentFilePath& path : paths) {
		root.action(path.name, [p = path, &fileManager, &project, system]() {
			spdlog::info("Loading {}", p.path.string());

			if (p.type == "project") {
				LoaderUtil::handleLoad(std::vector<std::string> { p.path.string() }, fileManager, project);
			} else {
				spdlog::error("Failed to load recent file: File type {} unknown", p.type);
			}

			/*if (system) {
				handleSystemLoad(p.path, "", system);
			} else {

			}*/
		});
	}
}

void MenuBuilder::systemAddMenu(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system) {
	fw::Menu& loadRoot = root.subMenu("Add");

	loadRoot.action("Duplicate Current", [&fileManager, &project, system]() {
		SystemDesc desc = system->getDesc();
		desc.paths.sramPath = fileManager.getUniqueFilename(desc.paths.sramPath).string();
		project.duplicateSystem(system->getId()/*, desc*/);
	});

	//populateRecent(loadRoot.subMenu("Recent"), fileManager, project, nullptr);

	loadRoot
		.action("ROM...", [&project]() { loadRomDialog(project, nullptr); })
		.parent();
}

void MenuBuilder::systemLoadMenu(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system) {
	fw::Menu& loadRoot = root.subMenu("Load");

	populateRecent(loadRoot.subMenu("Recent"), fileManager, project, system);

	loadRoot
		.action("Project...", [&](fw::MenuContext& ctx) {
			ctx.retain();

			std::vector<std::string> files;
			if (fw::FileDialog::openFile(files, { ROM_FILTER, PROJECT_FILTER }, true, false)) {
				LoaderUtil::handleLoad(files, fileManager, project);
				ctx.close();
			}
		})
		.action("ROM...", [&project, system]() { loadRomDialog(project, system); })
		.action("SAV...", [&project, system]() { loadSavDialog(project, system); })
		.parent();
}

void MenuBuilder::systemSaveMenu(fw::Menu& root, FileManager& fileManager, Project& project, SystemPtr system) {
	root.subMenu("Save")
		/*.action("Project", [&project, fileManager]() { saveProject(project, fileManager, false); })
		.action("Project As...", [&project, fileManager]() { saveProject(project, fileManager, true); })
		.action("SAV", [&project, system]() { saveSram(project, system, false); })
		.action("SAV As...", [&project, system]() { saveSram(project, system, true); })
		.action("State As...", [&project, system]() { saveState(project, system); })*/
		.action("All ROMs + SAVs", [&project]() {
			fw::Uint8Buffer target;
			if (ProjectExporter::exportRomsAndSavs(project, target)) {
				fw::FileDialog::saveFileData(target, { ZIP_FILTER }, project.getName() + ".zip");
			}
		})
		.parent();
}
