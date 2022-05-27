#pragma once

#include <thread>
#include <spdlog/spdlog.h>

#ifdef RP_WINDOWS
#include <windows.h>
#endif

#include "platform/Types.h"

namespace rp::ThreadUtil {
	static bool setPriority(std::thread& thread, uint32 priority) {
#ifdef RP_WINDOWS
		/* List of possible priority classes:
		https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass
		And respective thread priority numbers:
		https://docs.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities
		*/

		DWORD dwPriorityClass = REALTIME_PRIORITY_CLASS;
		int nPriorityNumber = THREAD_PRIORITY_TIME_CRITICAL;
		HANDLE handle = reinterpret_cast<HANDLE>(thread.native_handle());

		int result = SetPriorityClass(handle, dwPriorityClass);

		if (result != 0) {
			spdlog::error("Failed to set thread priority class.  Error code: {}", GetLastError());
			return false;
		}

		result = SetThreadPriority(handle, nPriorityNumber);

		if (result != 0) {
			spdlog::error("Failed to set thread priority number.  Error code: {}", GetLastError());
			return false;
		}

		return true;
#endif

		return false;
	}
}
