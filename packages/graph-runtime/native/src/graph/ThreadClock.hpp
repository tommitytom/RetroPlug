#pragma once

// The calling thread's CPU time, in microseconds (0 where it cannot be read): what a desktop host times a script
// node's deadline by when it offers no clock of its own (script/MqjsHost.hpp), and the clock the exporter's trial load
// offers (graph/PlanExport.hpp). A preemption, or any stall that is not the thread's own work, costs it nothing.
// Windows counts it in scheduler ticks of about 15.6 ms, so a thread not yet charged a tick reads 0 there; the reading
// is offset by 1 so that it is still a reading (a deadline started at 0 would run untimed, and a node that spins would
// spin for ever). Callers only take differences, which the offset leaves alone. Desktop only.

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace rp::graph {

inline std::uint64_t threadCpuMicros(void* = nullptr) {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetThreadTimes(::GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
    const auto us = [](const FILETIME& f) {
        return ((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10;
    };
    return us(kernel) + us(user) + 1;
#else
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
    return static_cast<std::uint64_t>(t.tv_sec) * 1000000 + static_cast<std::uint64_t>(t.tv_nsec) / 1000;
#endif
}

}  // namespace rp::graph
