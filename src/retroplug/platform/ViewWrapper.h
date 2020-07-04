#pragma once

#include "Menu.h"
#include "platform/FileDialog.h"

using DialogRequestPtr = std::shared_ptr<DialogRequest>;

class ViewWrapper {
private:
	Menu* _menu = nullptr;
	DialogRequestPtr _dialogRequest = nullptr;

public:
	ViewWrapper() {}
	~ViewWrapper() {}

	void requestDialog(DialogRequestPtr request) {
		_dialogRequest = request;
	}

	DialogRequestPtr fetchDialogRequest() {
		DialogRequestPtr req = _dialogRequest;
		_dialogRequest = nullptr;
		return req;
	}

	void requestMenu(Menu* menu) {
		_menu = menu;
	}

	Menu* fetchMenu() {
		Menu* menu = _menu;
		_menu = nullptr;
		return menu;
	}
};
