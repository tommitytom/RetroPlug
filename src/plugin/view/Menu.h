#pragma once

#include "platform/Menu.h"

namespace iplug::igraphics {
	class IPopupMenu;
}

namespace MenuTool {
	void createMenu(iplug::igraphics::IPopupMenu* target, Menu* source, MenuCallbackMap& callbacks);
}
