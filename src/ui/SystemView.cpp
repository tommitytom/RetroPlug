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

SystemView::SystemView() : GridItem() {

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
	const fw::Image& frameBuffer = _system->getFrameBuffer();

	if (frameBuffer.dimensions() != fw::Dimension::zero) {
		size_t dataSize = frameBuffer.getBuffer().size() * 4;
		std::vector<uint8> data(dataSize);
		memcpy(data.data(), frameBuffer.getData(), dataSize);

		if (_texture.isValid() && (fw::Dimension)_textureArea.dimensions == frameBuffer.dimensions()) {
			[[likely]]
			getResourceManager().update(_texture, fw::TextureDesc{
				.dimensions = frameBuffer.dimensions(),
				.depth = 4,
				.data = std::move(data)
			});
		} else {
			_texture = getResourceManager().create<fw::Texture>(fw::TextureDesc{
				.dimensions = frameBuffer.dimensions(),
				.depth = 4,
				.data = std::move(data)
			});

			_textureArea = { 0.0f, 0.0f, (f32)frameBuffer.dimensions().w, (f32)frameBuffer.dimensions().h };
		}
	}
}

void SystemView::onRender(fw::Canvas& canvas) {
	if (_texture.isValid()) {
		[[likely]]
		canvas.texture(_texture, getDimensionsF(), fw::Color4F(1, 1, 1, getAlpha()));
	} else {
		canvas.fillRect(_textureArea, fw::Color4F(1, 1, 1, getAlpha()));
	}
}

void SystemView::processButtons(const fw::ButtonWriter& stream) {
	_system->getButtons().push_back(stream.data());
}

void SystemView::createMenu(fw::Menu& target) {
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
