#include "CompactLayoutView.h"

#include "foundation/StlUtil.h"

namespace rp {
	void CompactLayoutView::onInitialize() {
		getLayout().setOverflow(fw::FlexOverflow::Visible);
		_grid = this->addChild<fw::GridView>("Grid");
		_gridOverlay = this->addChild<GridOverlay>("Grid Overlay");
		_gridOverlay->fitToParent();
		_gridOverlay->setGrid(_grid);

		subscribe<fw::ChildAddedEvent>(_grid, [this](const fw::ChildAddedEvent& ev) {
			_gridOverlay->setSelected((fw::ViewIndex)_grid->getChildren().size() - 1);
			_gridOverlay->refocus();
		});
		subscribe<fw::ChildRemovedEvent>(_grid, [this](const fw::ChildRemovedEvent& ev) {
			_gridOverlay->refocus();
		});
	}
	
	void CompactLayoutView::processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) {
		MenuViewPtr currentMenu = _menu.lock();
		if (currentMenu) {
			currentMenu->processButtons(buttons);

			if (fw::StlUtil::vectorContains(actions, std::string("RetroPlug.ToggleMenu"))) {
				currentMenu->remove();
				_menu.reset();
			}
		} else {
			GridItemPtr selected = _gridOverlay->getSelected();
			if (selected) {
				for (auto action : actions) {
					if (action == "RetroPlug.ToggleMenu") {
						fw::MenuPtr menu = std::make_shared<fw::Menu>();
						selected->createMenu(*menu);

						MenuViewPtr menuView = selected->addChild<MenuView>("Menu");
						menuView->fitToParent();
						menuView->setMenu(menu);
						menuView->focus();

						subscribe<fw::DismountEvent>(menuView, [this]() {
							//_project.setDirty();
						});

						_menu = menuView;
					} else if (action == "RetroPlug.NextSystem") {
						getGridOverlay()->incrementSelection();
					} else if (action == "RetroPlug.PreviousSystem") {
						getGridOverlay()->decrementSelection();
					} else if (action == "RetroPlug.SaveProject") {
						//_project.setDirty();
					}
				}

				selected->processInput(buttons, actions);
			}
		}
	}
}
