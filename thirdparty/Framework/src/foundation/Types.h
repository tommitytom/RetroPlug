#pragma once

#include <cstdint>
#include <stddef.h>
#include <string>

typedef std::uint8_t uint8;
typedef std::uint16_t uint16;
typedef std::uint32_t uint32;
typedef std::uint64_t uint64;

typedef std::int8_t int8;
typedef std::int16_t int16;
typedef std::int32_t int32;
typedef std::int64_t int64;

typedef float f32;
typedef double f64;

#ifdef FW_WINDOWS
using tstring = std::wstring;
#define TSTR(str) L##str
#else
using tstring = std::string;
#define TSTR(str) str
#endif
