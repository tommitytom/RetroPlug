#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include <string>
#include <iostream>

static void consoleLog(const std::string& msg) {
#ifdef WIN32
	OutputDebugStringA(msg.c_str());
#endif

	std::cout << msg;
}

static void consoleLogLine(const std::string& msg) {
	consoleLog(msg + "\r\n");
}
