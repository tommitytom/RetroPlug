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

namespace rp {
	using SystemId = uint32;
	constexpr SystemId INVALID_SYSTEM_ID = 0;

	//using SystemIndex = int32;
	//const SystemIndex INVALID_SYSTEM_IDX = -1;

	using RomIndex = int32;
	const RomIndex INVALID_ROM_IDX = -1;

	template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
	template<class... Ts> overload(Ts...)->overload<Ts...>;
}

#ifdef RP_WINDOWS
using tstring = std::wstring;
#define TSTR(str) L##str
#else
using tstring = std::string;
#define TSTR(str) str
#endif
