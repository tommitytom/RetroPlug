#include "Resource.h"

#include <windows.h>

static HINSTANCE GetHInstance() {
	MEMORY_BASIC_INFORMATION mbi;
	CHAR szModule[MAX_PATH];

	SetLastError(ERROR_SUCCESS);
	if (VirtualQuery(GetHInstance, &mbi, sizeof(mbi))) {
		if (GetModuleFileName((HINSTANCE)mbi.AllocationBase, szModule, sizeof(szModule))) {
			return (HINSTANCE)mbi.AllocationBase;
		}
	}
	return NULL;
}

Resource::Resource(int resourceId, const std::string& resourceClass) : _resourceId(resourceId), _resourceClass(resourceClass) {}

std::string_view Resource::getData() {
	HINSTANCE hinst = GetHInstance();
	HRSRC resourceHandle = ::FindResource(hinst, MAKEINTRESOURCE(_resourceId), _resourceClass.c_str());
	if (resourceHandle > 0) {
		_resourceHandle = ::LoadResource(hinst, resourceHandle);
		if (_resourceHandle) {
			void* data = ::LockResource(_resourceHandle);
			DWORD size = ::SizeofResource(hinst, resourceHandle);
			return std::string_view((const char*)data, size);
		}
	}

	return std::string_view();
}

Resource::~Resource() {
	if (_resourceHandle) {
		::FreeResource(_resourceHandle);
	}
}
