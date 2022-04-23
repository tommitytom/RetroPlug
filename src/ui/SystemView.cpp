#include "SystemView.h"

#include <spdlog/spdlog.h>

#include "platform/FileDialog.h"
#include "core/Project.h"
#include "ui/KeyToButton.h"
#include "ui/MenuView.h"
#include "ui/SamplerView.h"
#include "platform/AudioManager.h"
#include "sameboy/SameBoySystem.h"
#include "MenuBuilder.h"

using namespace rp;

SystemView::SystemView() : TextureView(), _frameBuffer(160, 144) {
	setType<SystemView>();
	setSizingMode(SizingMode::None);
}

bool SystemView::onDrop(const std::vector<std::string>& paths) {
	return false;
}

bool SystemView::onKey(VirtualKey::Enum key, bool down) {
	if (key == VirtualKey::Esc) {
		if (down) {
			// Generate menu
			MenuPtr menu = std::make_shared<Menu>();
			buildMenu(*menu);

			MenuViewPtr menuView = addChild<MenuView>("Menu");
			menuView->setMenu(menu);
			menuView->focus();
		}
	} else {
		ButtonType::Enum button = keyToButton(key);

		if (button != ButtonType::MAX) {
			SystemIoPtr& io = _system->getSystem()->getStream();
			_system->getSystem()->setButtonState(button, down);
		}
	}

	return true;
}

bool SystemView::onButton(ButtonType::Enum button, bool down) {
	SystemIoPtr& io = _system->getSystem()->getStream();

	if (io) {
		io->input.buttons.push_back(ButtonStream<8>{
			.presses = { (int)button, down },
			.pressCount = 1
		});
	}

	return true;
}

void SystemView::onUpdate(f32 delta) {
	SystemIoPtr& io = _system->getSystem()->getStream();
	if (!io) {
		return;
	}

	if (io->output.video) {
		_frameBuffer.write(io->output.video->getBuffer());
		setImage(*io->output.video);
	}
}

void loadRomDialog(Project* project) {
	std::vector<std::string> files;

	if (FileDialog::basicFileOpen(nullptr, files, { ROM_FILTER }, false)) {
		//project->addSystem<SameBoySystem>(files[0]);
	}
}

#include "core/ProjectExporter.h"

void SystemView::buildMenu(Menu& target) {
	FileManager* fileManager = getShared<FileManager>();
	Project* project = getShared<Project>();
	ProjectState& projectState = project->getState();

	Menu& root = target.title("RetroPlug v0.4.0 - " + _system->getSystem()->getRomName()).separator();
	MenuBuilder::systemLoadMenu(root, fileManager, project, _system);
	MenuBuilder::systemAddMenu(root, fileManager, project, _system);
	MenuBuilder::systemSaveMenu(root, fileManager, project, _system);

	int audioDevice = 0;

	AudioManager& audioManager = project->getAudioManager();

	std::vector<std::string> audioDevices;
	audioManager.getDeviceNames(audioDevices);

	root.separator()
		.action("Reset System", [this]() {
			_system->reset();
		})
		.action("Remove System", [this, project]() {
			if (project->getSystems().size() > 1) {
				project->removeSystem(_system->getId());
				this->remove();
			}
		})
		.separator();

	Menu& settingsMenu = root.subMenu("Settings");

	#ifndef RP_WEB
	settingsMenu
		.subMenu("Audio")
		.multiSelect("Device", audioDevices, audioDevice, [project](int v) {
			if (v >= 0) {
				project->getAudioManager().setAudioDevice((uint32)v);
			}
		})
		.parent();
	#endif

	settingsMenu
		.multiSelect("Zoom", { "1x", "2x", "3x", "4x", "5x", "6x" }, &projectState.settings.zoom)
		.subMenu("Save Options...")
			.multiSelect("Type", { "SRAM", "State" }, &projectState.settings.saveType)
			.select("Include ROM", &projectState.settings.includeRom)
			.parent()
		.parent()
	.separator()
	.action("Game Link", []() {})
	.action("Export project", [project]() {
		Uint8Buffer target;
		if (ProjectExporter::exportProject(*project, target)) {
			FileDialog::fileSaveData(nullptr, target, { ZIP_FILTER }, "project.rplg.zip");
		}		
	});

	for (ViewPtr child : getChildren()) {
		child->onMenu(target);
	}
}
