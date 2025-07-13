#include "StartView.h"

#include <sol/sol.hpp>

#include "foundation/FsUtil.h"
#include "foundation/SolUtil.h"

#include "core/Constants.h"
#include "core/FileManager.h"
#include "core/RetroPlugConfig.h"
#include "core/Project.h"
#include "core/ProjectSerializer.h"
#include "ui/FileDialog.h"
#include "ui/MenuBuilder.h"

#include "sameboy/SameBoySystem.h"
#include "sameboy/Constants.h"
#include "util/LoaderUtil.h"

#include "roms/mgb.h"

using namespace rp;

void StartView::setupMenu() {
	fw::MenuPtr menuRoot = std::make_shared<fw::Menu>();
	fw::Menu& menu = *menuRoot;

	menu.title(fmt::format("RetroPlug v{}", rp::RP_VERSION))
		.separator()
		.action("Load...", [&](fw::MenuContext& ctx) {
			ctx.retain();

			fw::FileDialogManager& dialog = getState<fw::FileDialogManager>();
			dialog.openFile({ ROM_FILTER, PROJECT_FILTER }, pfd::opt::multiselect, [&](std::vector<std::string>&& files) {
				if (LoaderUtil::handleLoad(files, getState<FileManager>(), getState<Project>())) {
					ctx.close();
				}
			});
		});

	MenuBuilder::populateRecent(menu.subMenu("Load Recent"), getState<FileManager>(), getState<Project>(), nullptr);

	menu
		.action("Load MGB", [this]() {
			Project& project = getState<Project>();

			SystemPtr system = project.addSystem(SAMEBOY_GUID, {
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
		.separator();

	MenuBuilder::settingsMenu(
		menu.subMenu("Settings"), 
		getState<const fw::TypeRegistry>(), 
		getState<InputManager>(), 
		getState<Project>(), 
		getState<RetroPlugConfig>(), 
		getState<fw::audio::AudioManagerPtr>().get()
	);

	setMenu(menuRoot);
	setAutoClose(false);
}

bool StartView::onDrop(const std::vector<std::string>& paths) {
	return LoaderUtil::handleLoad(paths, getState<FileManager>(), getState<Project>());
}
