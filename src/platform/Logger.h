#pragma once

#include <windows.h>
#include <string>
#include <iostream>

static void consoleLog(const std::string& msg) {
	OutputDebugStringA(msg.c_str());
	std::cout << msg << std::endl;
}

static void consoleLogLine(const std::string& msg) {
	consoleLog(msg + "\r\n");
}
