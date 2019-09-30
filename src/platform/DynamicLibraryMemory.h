#pragma once

#include <windows.h>
#include <string>
#include <assert.h>
//#include "error.h"
#include "resource.h"
#include <iostream>
#include "MemoryModule.h"

static HINSTANCE GetHInstance()
{
	MEMORY_BASIC_INFORMATION mbi;
	CHAR szModule[MAX_PATH];

	SetLastError(ERROR_SUCCESS);
	if (VirtualQuery(GetHInstance, &mbi, sizeof(mbi)))
	{
		if (GetModuleFileName((HINSTANCE)mbi.AllocationBase, szModule, sizeof(szModule)))
		{
			return (HINSTANCE)mbi.AllocationBase;
		}
	}
	return NULL;
}

class DynamicLibraryMemory {
private:
	HMEMORYMODULE _handle = nullptr;

public:
	DynamicLibraryMemory() {}
	~DynamicLibraryMemory() { close(); }

	void load(int resourceId) {
		HINSTANCE hinst = GetHInstance();
		HRSRC resourceHandle = ::FindResource(hinst, MAKEINTRESOURCE(resourceId), RT_RCDATA);
		HGLOBAL dataHandle = ::LoadResource(hinst, resourceHandle);
		DWORD size = ::SizeofResource(hinst, resourceHandle);
		void* data = ::LockResource(dataHandle);
		load((const char*)data, size);
	}

	void load(const char* data, size_t size) {
		_handle = MemoryLoadLibrary(data, size);
		assert(_handle);
		if (!_handle) {
			// Error!
			return;
		}
	}

	template <typename T>
	T get(const std::string& name) {
		T ptr = (T)MemoryGetProcAddress(_handle, (LPCSTR)name.c_str());
		if (!ptr) {
			//ErrorExit((LPTSTR)TEXT("DLL"));
		}

		return ptr;
	}

	template <typename T>
	void get(const std::string& name, T& target) {
		target = get<T>(name);
	}

	void close() {
		if (_handle) {
			MemoryFreeLibrary(_handle);
		}
	}
};
