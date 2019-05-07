#pragma once

#include <windows.h>
#include <string>
#include <iostream>
#include "Error.h"
#include "resource.h"

class DynamicLibrary {
private:
	HINSTANCE _instance = nullptr;

public:
	DynamicLibrary() {}
	DynamicLibrary(const std::wstring& path) { load(path); }
	~DynamicLibrary() { close(); }

	void load(const std::wstring& path) {
		_instance = LoadLibraryW(path.c_str());
		if (!_instance) {
			ErrorExit((LPTSTR)TEXT("DLL"));
		}
	}

	template <typename T>
	T get(const std::string& name) {
		T ptr = (T)GetProcAddress(_instance, (LPCSTR)name.c_str());
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
		if (_instance) {
			FreeLibrary((HMODULE)_instance);
		}
	}
};
