# SameBoy: upstream emulator core (submodule at deps/sameboy), built as a
# static library for RetroPlug. Compiles every Core/*.c except the
# disassembler / debugger / symbol-hash files, with the same compile defs.

set(SAMEBOY_DIR "${CMAKE_SOURCE_DIR}/deps/sameboy")

if(NOT EXISTS "${SAMEBOY_DIR}/Core/gb.h")
    message(FATAL_ERROR
        "deps/sameboy is empty. Run: git submodule update --init --recursive")
endif()

file(STRINGS "${SAMEBOY_DIR}/version.mk" _SAMEBOY_VERSION_RAW
     REGEX "^VERSION[ \t]*:=[ \t]*")
string(REGEX REPLACE "^VERSION[ \t]*:=[ \t]*" "" SAMEBOY_VERSION "${_SAMEBOY_VERSION_RAW}")
string(STRIP "${SAMEBOY_VERSION}" SAMEBOY_VERSION)

file(GLOB SAMEBOY_SOURCES CONFIGURE_DEPENDS "${SAMEBOY_DIR}/Core/*.c")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/sm83_disassembler\\.c$")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/symbol_hash\\.c$")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/debugger\\.c$")
# cheat_search.c references struct fields that gb.h removes when
# GB_DISABLE_DEBUGGER is set; gb.h auto-defines GB_DISABLE_CHEAT_SEARCH in
# that case, and gb.c's call sites are correctly guarded — but the .c file
# itself isn't, so exclude it from the build.
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/cheat_search\\.c$")

add_library(sameboy STATIC ${SAMEBOY_SOURCES})

target_include_directories(sameboy PUBLIC "${SAMEBOY_DIR}/Core")

target_compile_definitions(sameboy PUBLIC
    GB_INTERNAL
    GB_DISABLE_TIMEKEEPING
    GB_DISABLE_DEBUGGER
    GB_VERSION="${SAMEBOY_VERSION}"
)

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
