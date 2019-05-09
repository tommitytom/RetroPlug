#pragma once

#include <ShObjIdl.h>
#include <vector>
#include <string>

struct FileDialogFilters {
	std::wstring name;
	std::wstring extensions;
};

static std::wstring BasicFileOpen(const std::vector<FileDialogFilters>& filters) {
	COMDLG_FILTERSPEC* targetFilters = new COMDLG_FILTERSPEC[filters.size()];
	for (size_t i = 0; i < filters.size(); i++) {
		targetFilters[i].pszName = filters[i].name.c_str();
		targetFilters[i].pszSpec = filters[i].extensions.c_str();
	}

	std::wstring ret;
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
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
			pFileOpen->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
			pFileOpen->SetFileTypes(filters.size(), targetFilters);
			hr = pFileOpen->Show(NULL);

			// Get the file name from the dialog box.
			if (SUCCEEDED(hr))
			{
				IShellItem* pItem;
				hr = pFileOpen->GetResult(&pItem);
				if (SUCCEEDED(hr))
				{
					PWSTR pszFilePath;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

					// Display the file name to the user.
					if (SUCCEEDED(hr))
					{
						ret = std::wstring(pszFilePath);
						CoTaskMemFree(pszFilePath);
					}
					pItem->Release();
				}
			}
			pFileOpen->Release();
		}
		CoUninitialize();
	}

	delete[] targetFilters;
	return ret;
}

static std::wstring BasicFileSave(const std::vector<FileDialogFilters>& filters) {
	COMDLG_FILTERSPEC* targetFilters = new COMDLG_FILTERSPEC[filters.size()];
	for (size_t i = 0; i < filters.size(); i++) {
		targetFilters[i].pszName = filters[i].name.c_str();
		targetFilters[i].pszSpec = filters[i].extensions.c_str();
	}

	std::wstring ret;
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(hr))
	{
		IFileSaveDialog* pFileSave;

		// Create the FileOpenDialog object.
		hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL,
			IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));

		if (SUCCEEDED(hr))
		{
			DWORD dwFlags;
			pFileSave->GetOptions(&dwFlags);
			pFileSave->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
			pFileSave->SetFileTypes(filters.size(), targetFilters);
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
						ret = std::wstring(pszFilePath);
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
	return ret;
}
