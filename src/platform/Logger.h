#pragma once

#include <windows.h>
#include <string>

static void consoleLog(const std::string& msg) {
	OutputDebugStringA(msg.c_str());
}

static void consoleLogLine(const std::string& msg) {
	consoleLog(msg + "\r\n");
}
