#pragma once
#include "pch.h"
#include "Debugger/Debugger.h"
#include "Shared/Emulator.h"

// Single-threaded build (RetroPlug): there is no separate emulation thread to
// pause, so this RAII guard is a no-op. Upstream Mesen used it from a UI/debug
// thread to BreakRequest() the emulation thread and busy-wait until it stopped
// (via IsEmulationThread()/BreakRequest()/IsExecutionStopped()). That
// two-thread coordination is gone; the guard is kept as an empty type so its
// ~40 construction sites compile unchanged.
class DebugBreakHelper
{
public:
	DebugBreakHelper(Debugger* /*debugger*/, bool /*breakBetweenInstructions*/ = false) {}
	~DebugBreakHelper() {}
};
