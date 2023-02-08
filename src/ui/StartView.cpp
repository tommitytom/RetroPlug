#include "StartView.h"

#include <sol/sol.hpp>

#include "foundation/FsUtil.h"
#include "foundation/SolUtil.h"

#include "core/FileManager.h"
#include "core/Project.h"
#include "ui/FileDialog.h"
#include "ui/MenuBuilder.h"

#include "sameboy/SameBoySystem.h"
#include "util/LoaderUtil.h"

#include "roms/mgb.h"

using namespace rp;

void StartView::setupMenu() {
	fw::MenuPtr menuRoot = std::make_shared<fw::Menu>();
	fw::Menu& menu = *menuRoot;

	menu.title("RetroPlug v0.4.0")
		.separator()
		.action("Load...", [&](fw::MenuContext& ctx) {
			ctx.retain();

			std::vector<std::string> files;
			if (fw::FileDialog::basicFileOpen(nullptr, files, { ROM_FILTER, PROJECT_FILTER }, true, false)) {
				LoaderUtil::handleLoad(files, getState<FileManager>(), getState<Project>());
				ctx.close();
			}
		});

	MenuBuilder::populateRecent(menu.subMenu("Load Recent"), getState<FileManager>(), getState<Project>(), nullptr);

	menu
		.action("Load MGB", [this]() {
			Project& project = getState<Project>();

			SystemPtr system = project.addSystem(0x5A8EB011, {
				.desc = {
					.paths = {
						.romPath = "mgb.gb"
					}
				},
				.romBuffer = std::make_shared<fw::Uint8Buffer>(mgb, mgb_len)
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
	return LoaderUtil::handleLoad(paths, getState<FileManager>(), getState<Project>());
}
