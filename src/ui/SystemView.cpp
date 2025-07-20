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
#include "ui/SystemOverlay.h"

#include "sameboy/SameBoySystem.h"

using namespace rp;

SystemView::SystemView() : GridItem() {

}

bool SystemView::onDrop(const std::vector<std::string>& paths) {
	for (const std::string& path : paths) {
		if (fw::FsUtil::getFileExt(path, false) == ".sav") {
			fw::Uint8Buffer buffer;
			if (fw::FsUtil::readFile(path, &buffer)) {
				_system->loadSram(std::move(buffer));
				_system->reset();
			}

			break;
		}
	}

	return false;
}

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

void SystemView::processInput(std::vector<fw::StreamButtonPress>& stream, std::vector<std::string>& actions) {
	for (const auto& child : getChildren()) {
		if (child->isType<MenuView>()) {
			// MenuView will handle its own input
			continue;
		}
		const SystemOverlayPtr& overlay = child->asShared<SystemOverlay>();
		overlay->processInput(*_system, stream, actions);
		
	}
	_system->processInput(stream, actions);
}

void SystemView::createMenu(fw::Menu& target) {
	FileManager& fileManager = getState<FileManager>();
	fw::FileDialogManager& dialogManager = getState<fw::FileDialogManager>();
	InputManager& inputManager = getState<InputManager>();
	Project& project = getState<Project>();
	RetroPlugConfig& config = getState<RetroPlugConfig>();
	const fw::TypeRegistry& typeReg = getState<const fw::TypeRegistry>();
	fw::audio::AudioManagerPtr* audioManagerPtr = tryGetState<fw::audio::AudioManagerPtr>();

	fw::Menu& root = target.title(fmt::format("RetroPlug v{} - {}", rp::RP_VERSION, _system->getRomName())).separator();
	MenuBuilder::populateRecent(root.subMenu("Recent"), fileManager, project, _system);
	root.separator();
	MenuBuilder::commonMenu(root, dialogManager, fileManager, project, *_system);
	root.separator();
	MenuBuilder::projectMenu(root.subMenu("Project"), typeReg, fileManager, project, *_system);
	MenuBuilder::systemMenu(root.subMenu("System"), dialogManager, fileManager, project, _system);
	MenuBuilder::settingsMenu(root.subMenu("Settings"), typeReg, inputManager, project, config, audioManagerPtr != nullptr ? audioManagerPtr->get() : nullptr);

	if (getChildren().size() > 0) {
		root.separator();

		for (fw::ViewPtr child : getChildren()) {
			child->onMenu(target);
		}
	}

	/*root.separator()
		.action("Close", []() {
			
		});*/
}
