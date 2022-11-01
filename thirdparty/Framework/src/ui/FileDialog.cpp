#include "FileDialog.h"

#include "foundation/FsUtil.h"

using namespace fw;

#ifdef RP_WINDOWS

#include <string>
#include <vector>
#include <codecvt>
#include <locale>

#include <algorithm>
#include <cctype>

#include <ShObjIdl.h>
#include <windows.h>

static std::wstring s2ws(const std::string& str) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.from_bytes(str);
}

static std::string ws2s(const std::wstring& wstr) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes(wstr);
}

static std::string ws2s(const std::string& str) {
	return str;
}

#ifndef WIN32 // NOTE: Do a proper check here for wide strings!
static tstring tstr(const std::string& str) {
	return s2ws(str);
}

static const tstring& tstr(const std::wstring& str) {
	return str;
}
#else
static const tstring& tstr(const std::string& str) {
	return str;
}

static tstring tstr(const std::wstring& str) {
	return ws2s(str);
}
#endif

bool FileDialog::basicFileOpen(void* ui, std::vector<std::string>& target, const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly) {
	std::vector<std::pair<std::wstring, std::wstring>> wideFilters;

	COMDLG_FILTERSPEC* targetFilters = new COMDLG_FILTERSPEC[filters.size()];
	for (size_t i = 0; i < filters.size(); i++) {
		wideFilters.push_back({ s2ws(filters[i].name), s2ws(filters[i].extensions) });

		targetFilters[i].pszName = wideFilters.back().first.c_str();
		targetFilters[i].pszSpec = wideFilters.back().second.c_str();
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(hr))
	{
		IFileOpenDialog* pFileOpen;

		// Create the FileOpenDialog object.
		hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
			IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

		if (SUCCEEDED(hr))
		{
			DWORD dwFlags;
			pFileOpen->GetOptions(&dwFlags);

			if (multiSelect) {
				dwFlags |= FOS_ALLOWMULTISELECT;
			}

			if (foldersOnly) {
				dwFlags |= FOS_PICKFOLDERS;
			}

			pFileOpen->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
			pFileOpen->SetFileTypes((UINT)filters.size(), targetFilters);
			hr = pFileOpen->Show(NULL);

			// Get the file name from the dialog box.
			if (SUCCEEDED(hr))
			{
				IShellItemArray* items;
				hr = pFileOpen->GetResults(&items);
				if (SUCCEEDED(hr))
				{
					DWORD itemCount;
					items->GetCount(&itemCount);
					for (DWORD i = 0; i < itemCount; i++) {
						IShellItem* item;
						hr = items->GetItemAt(i, &item);
						if (SUCCEEDED(hr)) {
							PWSTR pszFilePath;
							hr = item->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

							if (SUCCEEDED(hr)) {
								target.push_back(ws2s(pszFilePath));
								CoTaskMemFree(pszFilePath);
							}
						}
					}

					items->Release();
				}
			}
			pFileOpen->Release();
		}
		CoUninitialize();
	}

	delete[] targetFilters;

	return target.size() > 0;
}

bool FileDialog::fileSaveData(void* ui, const fw::Uint8Buffer& data, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	std::string target;
	if (FileDialog::basicFileSave(ui, target, filters, fileName)) {
		return fw::FsUtil::writeFile(target, data);
	}

	return false;
}

bool FileDialog::basicFileSave(void* ui, std::string& target, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	std::vector<std::pair<std::wstring, std::wstring>> wideFilters;
	std::wstring wideFilename = s2ws(fileName);

	COMDLG_FILTERSPEC* targetFilters = new COMDLG_FILTERSPEC[filters.size()];
	for (size_t i = 0; i < filters.size(); i++) {
		wideFilters.push_back({ s2ws(filters[i].name), s2ws(filters[i].extensions) });

		targetFilters[i].pszName = wideFilters.back().first.c_str();
		targetFilters[i].pszSpec = wideFilters.back().second.c_str();
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(hr)) {
		IFileSaveDialog* pFileSave;

		// Create the FileOpenDialog object.
		hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL,
			IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));

		if (SUCCEEDED(hr)) {
			DWORD dwFlags;
			pFileSave->GetOptions(&dwFlags);
			pFileSave->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
			pFileSave->SetFileTypes((UINT)filters.size(), targetFilters);
			pFileSave->SetFileName(wideFilename.c_str());
			hr = pFileSave->SetDefaultExtension(wideFilters[0].second.c_str());
			hr = pFileSave->Show(NULL);

			// Get the file name from the dialog box.
			if (SUCCEEDED(hr))
			{
				IShellItem* pItem;
				hr = pFileSave->GetResult(&pItem);
				if (SUCCEEDED(hr))
				{
					PWSTR pszFilePath;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

					// Display the file name to the user.
					if (SUCCEEDED(hr))
					{
						target = ws2s(pszFilePath);
						CoTaskMemFree(pszFilePath);
					}
					pItem->Release();
				}
			}
			pFileSave->Release();
		}
		CoUninitialize();
	}

	delete[] targetFilters;

	return target.size() > 0;
}

bool FileDialog::fileOpenAsync(const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly, Callback&& cb) {
	std::vector<std::string> target;
	if (basicFileOpen(nullptr, target, filters, multiSelect, foldersOnly)) {
		cb(target, true);
		return true;
	}

	cb(target, false);
	return false;
}

/*#elif RP_MACOS

#undef T
#include "IGraphics.h"

using namespace iplug;
using namespace igraphics;

namespace igraphics {
struct IGraphics;
}

std::vector<tstring> FileDialog::basicFileOpen(IGraphics* ui, const std::vector<FileDialogFilters>& filters, bool multiSelect, bool foldersOnly) {
	WDL_String fileName;
	WDL_String path;
	ui->PromptForFile(fileName, path, EFileAction::Open);

	std::vector<tstring> ret;
	if (path.GetLength() > 0 && fileName.GetLength() > 0) {
		ret.push_back(tstr(fileName.Get()));
	}

	return ret;
}

tstring FileDialog::basicFileSave(IGraphics* ui, const std::vector<FileDialogFilters>& filters, const tstring& fileName) {
	WDL_String f(ws2s(fileName).c_str());
	WDL_String path;
	ui->PromptForFile(f, path, EFileAction::Save);

	return tstr(f.Get());
}*/

#elif RP_WEB

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

bool FileDialog::fileSaveData(UiHandle* ui, const fw::Uint8Buffer& data, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	std::string filePath = "/.file-save-dialog/" + fileName;

	fs::create_directories("/.file-save-dialog/");

	if (!fw::FsUtil::writeFile(filePath, data)) {
		spdlog::error("Failed to save {}", filePath);
		return false;
	}

	saveWebFileDialog(filePath.c_str());

	return true;
}

bool FileDialog::fileOpenAsync(const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly, Callback&& cb) {
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

bool FileDialog::basicFileOpen(UiHandle* ui, std::vector<std::string>& target, const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly) {
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

bool FileDialog::basicFileSave(UiHandle* ui, std::string& target, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	return false;
}

#else

bool FileDialog::fileSaveData(UiHandle* ui, const fw::Uint8Buffer& data, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	return false;
}

bool FileDialog::fileOpenAsync(const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly, Callback&& cb) {
	return false;
}

bool FileDialog::basicFileOpen(UiHandle* ui, std::vector<std::string>& target, const std::vector<FileDialogFilter>& filters, bool multiSelect, bool foldersOnly) {
	return false;
}

bool FileDialog::basicFileSave(UiHandle* ui, std::string& target, const std::vector<FileDialogFilter>& filters, const std::string& fileName) {
	return false;
}

#endif