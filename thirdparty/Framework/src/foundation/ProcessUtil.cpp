#include "ProcessUtil.h"

#ifdef RP_WINDOWS
#include <fcntl.h>
#include <windows.h>
#include <io.h>
#define STDERR_FILENO 2
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <stdlib.h>
#include <iostream>
#endif

#include <stdio.h>
#include <spdlog/spdlog.h>

#include <sstream>

#include "foundation/StringUtil.h"

using namespace fw;

std::string quoteIfNecessary(const std::string& toQuote) {
	if (toQuote.find(" ") != std::string::npos) {
		return '\"' + toQuote + '\"';
	}

	return toQuote;
}

// Get a command line with the program and its arguments, like you'd type into a shell or pass to CreateProcess()
// Arguments with spaces will be double quoted.
std::string getCommandlineString(const std::string& path, const std::vector<std::string>& args) {
	std::stringstream cmdline;

	cmdline << quoteIfNecessary(path);

	for (const auto& arg : args) {
		cmdline << " " << quoteIfNecessary(arg);
	}

	return cmdline.str();
}

int32 ProcessUtil::runProcess(const std::string& path, const std::vector<std::string>& args, bool silent) {
#ifdef RP_WINDOWS
	STARTUPINFO startInfo;

	ZeroMemory(&startInfo, sizeof(startInfo));
	startInfo.cb = sizeof(startInfo);
	startInfo.dwFlags = STARTF_USESTDHANDLES;

	if (!silent) {
		startInfo.hStdInput = (HANDLE)_get_osfhandle(STDIN_FILENO);
		startInfo.hStdOutput = (HANDLE)_get_osfhandle(STDOUT_FILENO);
		startInfo.hStdError = (HANDLE)_get_osfhandle(STDERR_FILENO);
	}

	std::string cmdLine = getCommandlineString(path, args);

	PROCESS_INFORMATION procInfo;
	if (CreateProcessA(NULL, const_cast<char*>(cmdLine.c_str()), NULL, NULL, true, 0, NULL, NULL, &startInfo, &procInfo) == 0) {
		int lasterror = GetLastError();

		LPTSTR strErrorMessage = NULL;

		// the next line was taken from GitHub
		FormatMessage(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			NULL,
			lasterror,
			0,
			(LPTSTR)(&strErrorMessage),
			0,
			NULL);

		spdlog::error("CreateProcess({}) failed with error {}: {}", cmdLine, lasterror, strErrorMessage);
		return -1;
	}

	// Wait until child process exits.
	WaitForSingleObject(procInfo.hProcess, INFINITE);

	DWORD exitCode;
	GetExitCodeProcess(procInfo.hProcess, &exitCode);

	// Close process and thread handles.
	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);

	return exitCode;
#else
	std::string cmdLine = getCommandlineString(path, args);
	FILE* stream = popen(cmdLine.c_str(), "r");

	if (!stream) {
		spdlog::error("Failed to execute {}", cmdLine);
		return 1;
	}

	int ch = fgetc(stream);
	while (ch != EOF) {
		if (!silent) {
			std::cout << ch;
		}

		ch = fgetc(stream);
	}

	pclose(stream);

	return 0;
#endif
}
