#pragma once

#include "Menu.h"
#include "IControl.h"
#include "platform/FileDialog.h"

using namespace iplug;
using namespace igraphics;

class ViewWrapper {
private:
	Menu* _menu = nullptr;
	DialogRequest* _dialogRequest = nullptr;

public:
	ViewWrapper() {}
	~ViewWrapper() {}

	void requestDialog(DialogRequest* request) {
		_dialogRequest = request;
	}

	DialogRequest* fetchDialogRequest() {
		DialogRequest* req = _dialogRequest;
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
