#include "SystemView.h"

#include <spdlog/spdlog.h>

#include "foundation/KeyToButton.h"

#include "core/Project.h"

#include "audio/AudioManager.h"

#include "ui/ViewManager.h"
#include "ui/FileDialog.h"
#include "ui/MenuBuilder.h"
#include "ui/MenuView.h"
#include "ui/SamplerView.h"

#include "sameboy/SameBoySystem.h"

using namespace rp;

SystemView::SystemView() : TextureView() {

}

bool SystemView::onDrop(const std::vector<std::string>& paths) {
	return false;
}

bool SystemView::onKey(const fw::KeyEvent& ev) {
	if (ev.key == fw::VirtualKey::Tab) {
		// TODO: This is temporary.  Ideally there will be a global key handler that picks up tabs for moving between instances etc!
		return false;
	}

	if (ev.key == fw::VirtualKey::Esc) {
		if (ev.down) {
			// Generate menu
			fw::MenuPtr menu = std::make_shared<fw::Menu>();
			buildMenu(*menu);

			MenuViewPtr menuView = addChild<MenuView>("Menu");
			menuView->fitToParent();
			menuView->setMenu(menu);
			menuView->focus();

			subscribe<fw::DismountEvent>(menuView, [this]() { getState<Project>().setDirty(); });
		}
	} else {
		fw::ButtonType button = fw::keyToButton(ev.key);

		if (button != fw::ButtonType::MAX) {
			SystemIoPtr io = _system->getIo();
			_system->setButtonState(button, ev.down);
		}
	}

	return true;
}

bool SystemView::onButton(const fw::ButtonEvent& ev) {
	SystemIoPtr io = _system->getIo();

	if (io) {
		io->input.buttons.push_back(ButtonStream<8>{
			.presses = { (int)ev.button, ev.down },
			.pressCount = 1
		});
	}

	return true;
}

void SystemView::onUpdate(f32 delta) {
	if (_system->getFrameBuffer().dimensions() != fw::Dimension::zero) {
		setImage(_system->getFrameBuffer());
	}
}

void SystemView::buildMenu(fw::Menu& target) {
	FileManager& fileManager = getState<FileManager>();
	Project& project = getState<Project>();
	ProjectState& projectState = project.getState();

	fw::Menu& root = target.title(fmt::format("RetroPlug v{} - {}", RP_VERSION, _system->getRomName())).separator();
	MenuBuilder::systemLoadMenu(root, fileManager, project, _system);
	MenuBuilder::systemAddMenu(root, fileManager, project, _system);
	MenuBuilder::systemSaveMenu(root, fileManager, project, _system);

	int audioDevice = 0;

	//fw::audio::AudioManager& audioManager = project.getAudioManager();

	//std::vector<std::string> audioDevices;
	//audioManager.getDeviceNames(audioDevices);

	root.separator()
		.action("Reset System", [this]() {
			_system->reset();
		})
		.action("Remove System", [this, &project]() {
			if (project.getSystems().size() > 1) {
				project.removeSystem(_system->getId());
				this->remove();
			}
		}, project.getSystems().size() > 1)
		.separator();

	fw::Menu& settingsMenu = root.subMenu("Settings");

	const bool hasSram = _system->getMemory(MemoryType::Sram, AccessType::Read).getSize();

	settingsMenu.multiSelect("Save Type", { "SRAM", "State" }, (int)_system->getDesc().settings.saveType - 1, [system = std::weak_ptr(_system)](int value) {
		SystemPtr systemPtr = system.lock();
		SystemDesc desc = systemPtr->getDesc();
		desc.settings.saveType = (SaveStateType)(value + 1);
		systemPtr->setDesc(std::move(desc));
	}, hasSram);

	for (fw::ViewPtr child : getChildren()) {
		child->onMenu(settingsMenu);
	}

	fw::Menu& globalSettingsMenu = root.subMenu("Global Settings");

	#ifndef RP_WEB
	globalSettingsMenu
		.subMenu("Audio")
		/*.multiSelect("Device", audioDevices, audioDevice, [project](int v) {
			if (v >= 0) {
				project.getAudioManager().setAudioDevice((uint32)v);
			}
		})*/
		.parent();
#endif

	globalSettingsMenu
		.multiSelect("MIDI", { "Send To All", "Four Channels Per Instance", "One Channel Per Instance" }, &projectState.settings.midiRouting)
		.multiSelect("Audio Routing", { "Stereo Mix Down", "Two Channels Per Instance", "Two Channels Per Channel" }, &projectState.settings.audioRouting)
		.parent();

	globalSettingsMenu
		.multiSelect("Zoom", { "1x", "2x", "3x", "4x", "5x", "6x" }, &projectState.settings.zoom)
		.multiSelect("Layout", { "Auto", "Row", "Column", "Grid" }, (int)projectState.settings.layout, [this, &project](int layout) {
			project.getState().settings.layout = (SystemLayout)layout;
			setLayoutDirty();
		})
		.subMenu("Save Options...")
			.select("Auto Save", &projectState.settings.autoSave)
			.select("Include ROM", &projectState.settings.includeRom)
			.parent()
		.parent()
		.separator()
		.select("Game Link", _system->getGameLink(), [&](bool selected) {
			_system->setGameLink(selected);
		})
		.separator()
		.action("Exit", [&]() {
			this->findParent<fw::ViewManager>()->closeWindow(false);
		});
}
