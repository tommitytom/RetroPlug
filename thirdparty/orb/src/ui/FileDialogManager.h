#pragma once

#include <variant>
#include <portable-file-dialogs.h>
#include "ui/FileDialog.h"

namespace orb {
	class FileDialogManager {
	private:
		using Callback = std::function<void(std::vector<std::string>&&)>;
		std::optional<std::variant<pfd::open_file>> _currentDialog;
		Callback _currentCallback;

	public:
		FileDialogManager() { pfd::settings::verbose(true); }

		void openFile(const std::vector<FileDialogFilter>& filters, pfd::opt options, Callback&& callback);

		void saveFile() {

		}

		void update();
	};
}