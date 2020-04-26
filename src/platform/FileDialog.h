#pragma once

#ifdef WIN32
// FIXME: For some reason this has to be defined in the header, which is a bit nasty.  I think it's
// due to something else being included (possibly windows.h) in another file
#include <ShObjIdl.h>
#endif

#include <vector>
#include <string>
#include "util/xstring.h"
#include "Constants.h"

namespace iplug {
	namespace igraphics {
		class IGraphics;
	}
}

struct FileDialogFilters {
	tstring name;
	tstring extensions;
};

struct DialogRequest {
	DialogType type = DialogType::None;
	std::vector<FileDialogFilters> filters;
	bool multiSelect;
};

std::vector<tstring> BasicFileOpen(iplug::igraphics::IGraphics* ui, const std::vector<FileDialogFilters>& filters, bool multiSelect = false, bool foldersOnly = false);
tstring BasicFileSave(iplug::igraphics::IGraphics* ui, const std::vector<FileDialogFilters>& filters, const tstring& fileName = TSTR(""));
