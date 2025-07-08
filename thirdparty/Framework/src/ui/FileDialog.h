#pragma once

#ifdef FW_OS_WINDOWS
// FIXME: For some reason this has to be defined in the header, which is a bit nasty.  I think it's
// due to something else being included (possibly windows.h) in another file
#include <ShObjIdl.h>
#endif

#include <functional>
#include <string>
#include <vector>

#include "foundation/Types.h"
#include "foundation/DataBuffer.h"

#ifdef FW_OS_WINDOWS
namespace fw {
	using UiHandle = void;
}
#else
namespace iplug {
	namespace igraphics {
		class IGraphics;
	}
}

using UiHandle = iplug::igraphics::IGraphics;
#endif

namespace fw {
	struct FileDialogFilter {
		std::string name;
		std::string extensions;
	};

	enum class DialogType {
		None,
		Load,
		Save,
		Directory
	};

	struct DialogRequest {
		DialogType type = DialogType::None;
		std::vector<FileDialogFilter> filters;
		std::string fileName;
		bool multiSelect;
	};

	class File {
	public:
		std::string path;
		fw::Uint8Buffer data;
	};

	namespace FileDialog {
		using Callback = std::function<void(std::vector<std::string>&, bool)>;

		bool openFile(std::vector<std::string>& target, const std::vector<FileDialogFilter>& filters, bool multiSelect = false, bool foldersOnly = false);
		bool openFileAsync(const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly, Callback&& cb);

		bool saveFile(std::string& target, const std::vector<FileDialogFilter>& filters, const std::string& fileName = "");
		bool saveFileData(const fw::Uint8Buffer& data, const std::vector<FileDialogFilter>& filters, const std::string& fileName = "");
	}
}
