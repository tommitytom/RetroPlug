# Vendored SameBoy Core, built as a static lib for RetroPlug.
# Source layout mirrors the old Premake recipe (old/premake/dep/SameBoy.lua):
# - Compiles every Core/*.c except sm83_disassembler.c, symbol_hash.c, debugger.c
# - Builds with GB_INTERNAL, GB_DISABLE_TIMEKEEPING, GB_DISABLE_DEBUGGER
# - Exposes Core/ as a public include directory so consumers `#include <gb.h>`

# Read VERSION from version.mk (mirrors Premake's getVersion()).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.mk" _SAMEBOY_VERSION_RAW
     REGEX "^VERSION[ \t]*:=[ \t]*")
string(REGEX REPLACE "^VERSION[ \t]*:=[ \t]*" "" SAMEBOY_VERSION "${_SAMEBOY_VERSION_RAW}")
string(STRIP "${SAMEBOY_VERSION}" SAMEBOY_VERSION)

file(GLOB SAMEBOY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.c")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/sm83_disassembler\\.c$")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/symbol_hash\\.c$")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/debugger\\.c$")

add_library(sameboy STATIC ${SAMEBOY_SOURCES})

target_include_directories(sameboy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Core")

target_compile_definitions(sameboy PUBLIC
    GB_INTERNAL
    GB_DISABLE_TIMEKEEPING
    GB_DISABLE_DEBUGGER
    GB_VERSION="${SAMEBOY_VERSION}"
)

# SameBoy's source is upstream-tracked; suppress the warnings it triggers
# under -Wall rather than patching them.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sameboy PRIVATE
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-missing-braces
        -Wno-switch
        -Wno-int-in-bool-context
        -Wno-implicit-function-declaration
        -Wno-multichar
        -Wno-strict-aliasing
    )
endif()

set_target_properties(sameboy PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)
