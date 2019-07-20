#include "FileDialog.h"

#ifdef WIN32

#include <ShObjIdl.h>
#include <windows.h>

std::vector<tstring> BasicFileOpen(IGraphics* ui, const std::vector<FileDialogFilters>& filters, bool multiSelect, bool foldersOnly) {
	COMDLG_FILTERSPEC* targetFilters = new COMDLG_FILTERSPEC[filters.size()];
	for (size_t i = 0; i < filters.size(); i++) {
		targetFilters[i].pszName = filters[i].name.c_str();
		targetFilters[i].pszSpec = filters[i].extensions.c_str();
	}

	std::vector<tstring> ret;
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

			if (multiSelect) {
				dwFlags |= FOS_ALLOWMULTISELECT;
			}

			if (foldersOnly) {
				dwFlags |= FOS_PICKFOLDERS;
			}

			pFileOpen->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
			pFileOpen->SetFileTypes(filters.size(), targetFilters);
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
					for (int i = 0; i < itemCount; i++) {
						IShellItem* item;
						hr = items->GetItemAt(i, &item);
						if (SUCCEEDED(hr)) {
							PWSTR pszFilePath;
							hr = item->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

							// Display the file name to the user.
							if (SUCCEEDED(hr))
							{
								ret.push_back(pszFilePath);
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
	return ret;
}

tstring BasicFileSave(IGraphics* ui, const std::vector<FileDialogFilters>& filters, const tstring& fileName) {
	COMDLG_FILTERSPEC* targetFilters = new COMDLG_FILTERSPEC[filters.size()];
	for (size_t i = 0; i < filters.size(); i++) {
		targetFilters[i].pszName = filters[i].name.c_str();
		targetFilters[i].pszSpec = filters[i].extensions.c_str();
	}

	tstring ret;
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
			pFileSave->SetFileName(fileName.c_str());
			hr = pFileSave->SetDefaultExtension(filters[0].extensions.c_str());
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
						ret = tstring(pszFilePath);
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
#else

#undef T
#include "IGraphics.h"

std::vector<tstring> BasicFileOpen(IGraphics* ui, const std::vector<FileDialogFilters>& filters, bool multiSelect, bool foldersOnly) {
	WDL_String fileName;
	WDL_String path;
	ui->PromptForFile(fileName, path, EFileAction::Open);

	std::vector<tstring> ret;
	if (path.GetLength() > 0 && fileName.GetLength() > 0) {
		ret.push_back(tstr(fileName.Get()));
	}

	return ret;
}

tstring BasicFileSave(IGraphics* ui, const std::vector<FileDialogFilters>& filters, const tstring& fileName) {
	WDL_String f(ws2s(fileName).c_str());
	WDL_String path;
	ui->PromptForFile(f, path, EFileAction::Save);

	return tstr(f.Get());
}


#endif
