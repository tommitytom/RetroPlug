#pragma once

// RP_NOINLINE: the never-inlined form of a function, so its frame is on the stack only while it runs. What a device
// build uses to keep a large local (an error kept for a caller that asked for none, a whole console line, a file path)
// off the frame of the function that calls it, which -Wstack-usage measures (cmake/device-image.cmake).
#if defined(_MSC_VER)
#define RP_NOINLINE __declspec(noinline)
#else
#define RP_NOINLINE __attribute__((noinline))
#endif
