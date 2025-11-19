#pragma once

#if defined(__INTELLISENSE__) && defined(__clang__) && defined(_MSC_VER)

// These fix Intellisense parsing problems that affect many system/library headers
#define __bf16 unsigned short
#define __building_module(x) (0)

// To prevent use of __builtin_FUNCSIG() in std::source_location::current()
#define _USE_DETAILED_FUNCTION_NAME_IN_SOURCE_LOCATION 0

#endif
