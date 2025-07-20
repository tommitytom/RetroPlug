#include "DialogView.h"

namespace rp {
	void DialogView::onInitialize() {
		fw::MenuPtr menuRoot = std::make_shared<fw::Menu>();
		fw::Menu& menu = *menuRoot;

		if (_title.size()) {
			menu.title(_title).separator();
		}

		switch (_type) {
		case DialogType::OkCancel:
			menu.action("OK", [this]() { this->emit(DialogResult::Ok); });
			menu.action("Cancel", [this]() { this->emit(DialogResult::Cancel); });
			break;
		case DialogType::YesNo:
			menu.action("Yes", [this]() { this->emit(DialogResult::Yes); });
			menu.action("No", [this]() { this->emit(DialogResult::No); });
			break;
		case DialogType::YesNoCancel:
			menu.action("Yes", [this]() { this->emit(DialogResult::Yes); });
			menu.action("No", [this]() { this->emit(DialogResult::No); });
			menu.action("Cancel", [this]() { this->emit(DialogResult::Cancel); });
			break;
		}

		_menu = addChild<MenuView>("Menu");
		_menu->setMenu(menuRoot);
		_menu->setEscCloses(true);
		_menu->fitToParent();

		this->fitToParent();
	}

	void DialogView::processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) {
		_menu->processButtons(buttons);
	}
}
