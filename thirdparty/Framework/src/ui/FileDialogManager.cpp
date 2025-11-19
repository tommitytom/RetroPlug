#include "FileDialogManager.h"

namespace orb {
	void FileDialogManager::openFile(const std::vector<FileDialogFilter>& filters, pfd::opt options, Callback&& callback) {
		assert(!_currentDialog.has_value());

		std::vector<std::string> stringFilters;

		for (const FileDialogFilter& filter : filters) {
			std::string extensions;
			for (const std::string& ext : filter.extensions) {
				if (!extensions.empty()) {
					extensions += " ";
				}
				extensions += ext;
			}

			std::string name = filter.name + " (" + extensions + ")";

			stringFilters.push_back(name);
			stringFilters.push_back(extensions);
		}

		callback(pfd::open_file("Open File", "", stringFilters, options).result());

		//_currentCallback = std::move(callback);
		//_currentDialog = pfd::open_file("Open File", "", stringFilters, options);
	}
	
	void FileDialogManager::update() {
		if (_currentDialog.has_value()) {
			auto& dialog = std::get<pfd::open_file>(*_currentDialog);
			if (dialog.ready(1000)) {
				_currentCallback(dialog.result());

				_currentDialog.reset();
				_currentCallback = nullptr;
			}
		}
	}
}
