#include "SystemView.h"

#include <spdlog/spdlog.h>

#include "foundation/KeyToButton.h"

#include "core/Constants.h"
#include "core/InputManager.h"
#include "core/Project.h"
#include "core/ProjectSerializer.h"

#include "audio/AudioManager.h"

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

/*bool SystemView::onKey(const fw::KeyEvent& ev) {
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
		InputManager& inputManager = getState<InputManager>();
		inputManager.processKey(ev.key, ev.down);
	}

	return true;
}*/

void SystemView::onUpdate(f32 delta) {
	if (_system->getFrameBuffer().dimensions() != fw::Dimension::zero) {
		setImage(_system->getFrameBuffer());
	}
}

void SystemView::processButtons(const fw::ButtonWriter& stream) {
	spdlog::info("Buttons: {}, {}", stream.data().presses[0].button, stream.data().presses[0].down);
	_system->getButtons().push_back(stream.data());
}

void SystemView::buildMenu(fw::Menu& target) {
	FileManager& fileManager = getState<FileManager>();
	Project& project = getState<Project>();
	GlobalSettings& globalSettings = getState<GlobalSettings>();
	ProjectState& projectState = project.getState();
	fw::audio::AudioManagerPtr& audioManager = getState<fw::audio::AudioManagerPtr>();

	fw::Menu& root = target.title(fmt::format("RetroPlug v{} - {}", rp::RP_VERSION, _system->getRomName())).separator();
	MenuBuilder::populateRecent(root.subMenu("Recent"), fileManager, project, _system);
	root.separator();
	MenuBuilder::projectMenu(root.subMenu("Project"), fileManager, project, *_system);
	MenuBuilder::systemMenu(root.subMenu("System"), fileManager, project, _system);
	MenuBuilder::settingsMenu(root.subMenu("Settings"), fileManager, project, globalSettings, *audioManager);

	if (getChildren().size() > 0) {
		root.separator();
	}

	for (fw::ViewPtr child : getChildren()) {
		child->onMenu(target);
	}
}
