#include "FileDialog.h"

#include "foundation/FsUtil.h"
#include <portable-file-dialogs.h>

using namespace fw;

#ifdef FW_PLATFORM_WEB

#include <emscripten.h>
#include <spdlog/spdlog.h>

#include "foundation/StringUtil.h"

EM_ASYNC_JS(char*, openWebFileDialog, (), {
	const paths = await openFileDialog();

	if (paths.length > 0) {
		const lengthBytes = lengthBytesUTF8(paths) + 1;
		const stringOnWasmHeap = _malloc(lengthBytes);
		stringToUTF8(paths, stringOnWasmHeap, lengthBytes);
		return stringOnWasmHeap;
	}

	return null;
});

EM_ASYNC_JS(void, saveWebFileDialog, (const char* filePath), {
	await saveFileDialog(filePath);
});

bool FileDialog::saveFileData(const fw::Uint8Buffer& data, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	std::string filePath = "/.file-save-dialog/" + fileName;

	fs::create_directories("/.file-save-dialog/");

	if (!fw::FsUtil::writeFile(filePath, data)) {
		spdlog::error("Failed to save {}", filePath);
		return false;
	}

	saveWebFileDialog(filePath.c_str());

	return true;
}

bool FileDialog::openFileAsync(const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly, Callback&& cb) {
	char* paths = openWebFileDialog();
	std::vector<std::string> target;

	if (paths) {
		std::vector<std::string_view> splitPaths = StringUtil::split(paths, ";");

		for (std::string_view s : splitPaths) {
			target.push_back(std::string(s));
		}

		cb(target, true);

		free(paths);

		return true;
	}

	cb(target, false);
	return false;
}

bool FileDialog::openFile(std::vector<std::string>& target, const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly) {
	char* paths = openWebFileDialog();

	if (paths) {
		std::vector<std::string_view> splitPaths = StringUtil::split(paths, ";");

		for (std::string_view s : splitPaths) {
			target.push_back(std::string(s));
		}

		free(paths);

		return true;
	}

	return false;
}

bool FileDialog::saveFile(std::string& target, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	return false;
}

#else

bool FileDialog::saveFileData(const fw::Uint8Buffer& data, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	std::string target;
	if (FileDialog::saveFile(target, filters, fileName)) {
		return fw::FsUtil::writeFile(target, data);
	}

	return false;
}

bool FileDialog::openFileAsync(const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly, Callback&& cb) {
	return false;
}

bool FileDialog::openFile(std::vector<std::string>& target, const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly) {
	std::vector<std::string> stringFilters;

	for (const FileDialogFilter& filter : filters) {
		std::string name = filter.name + " (" + filter.extensions + ")";

		stringFilters.push_back(name);
		stringFilters.push_back(filter.extensions);
	}

	target = pfd::open_file("Open File", "", stringFilters, multiSelect ? pfd::opt::multiselect : pfd::opt::none).result();
	return !target.empty();
}

bool FileDialog::saveFile(std::string& target, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	std::vector<std::string> stringFilters;

	for (const FileDialogFilter& filter : filters) {
		stringFilters.push_back(filter.name);
		stringFilters.push_back(filter.extensions);
	}

	target = pfd::save_file("Save File", "", stringFilters).result();
	return !target.empty();
}

#endif
