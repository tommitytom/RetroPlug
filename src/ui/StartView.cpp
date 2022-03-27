#include "StartView.h"

#include <sol/sol.hpp>

#include "core/FileManager.h"
#include "core/Project.h"
#include "platform/FileDialog.h"
#include "roms/mgb.h"
#include "sameboy/SameBoySystem.h"
#include "ui/MenuBuilder.h"
#include "util/fs.h"
#include "util/SolUtil.h"

using namespace rp;

void StartView::setupMenu() {
	spdlog::info("setting up menu");

	MenuPtr menuRoot = std::make_shared<Menu>();
	Menu& menu = *menuRoot;

	menu.title("RetroPlug v0.4.0")
		.separator()
		.action("Load...", [&](MenuContext& ctx) {
			ctx.retain();

			std::vector<std::string> files;
			if (FileDialog::basicFileOpen(nullptr, files, { ROM_FILTER, PROJECT_FILTER }, true, false)) {
				MenuBuilder::handleLoad(files, *getShared<FileManager>(), *getShared<Project>());
				ctx.close();
			}
		});

	MenuBuilder::populateRecent(menu.subMenu("Load Recent"), getShared<FileManager>(), getShared<Project>(), nullptr);

	menu
		.action("Load MGB", [this]() {
			Project* project = getShared<Project>();

			SystemWrapperPtr system = project->addSystem<SameBoySystem>({
				.romPath = "mgb.gb"
			}, {
				.romBuffer = std::make_shared<Uint8Buffer>(mgb, mgb_len) 
			});

			std::string systemName = fmt::format("System {}", system->getId());
			std::shared_ptr<SystemView> view = getParent()->addChild<SystemView>(systemName);
			view->setSystem(system);

			this->remove();
		})
		.separator()
		.subMenu("Settings")
		.parent();

	setMenu(menuRoot);
	setAutoClose(false);
}

bool StartView::onDrop(const std::vector<std::string>& paths) {
	return MenuBuilder::handleLoad(paths, *getShared<FileManager>(), *getShared<Project>());
}
