# Build SameBoy's boot ROMs from the submodule's BootROMs/*.asm sources.
# Requires RGBDS (rgbasm, rgblink, rgbgfx). Emits one .h per ROM with the
# byte array + length symbol the wrapper code expects.

find_program(RGBASM rgbasm)
find_program(RGBLINK rgblink)
find_program(RGBGFX rgbgfx)

if(NOT RGBASM OR NOT RGBLINK OR NOT RGBGFX)
    message(FATAL_ERROR
        "RGBDS (rgbasm, rgblink, rgbgfx) is required to build the SameBoy "
        "boot ROMs. Install from https://github.com/gbdev/rgbds/releases "
        "or your distribution's package manager.")
endif()

set(BR_SRC "${SAMEBOY_DIR}/BootROMs")
set(BR_OBJ "${CMAKE_BINARY_DIR}/sameboy-bootroms")
set(BR_OUT "${CMAKE_BINARY_DIR}/generated/system/sameboy/bootroms")
file(MAKE_DIRECTORY "${BR_OBJ}")
file(MAKE_DIRECTORY "${BR_OUT}")

# pb12 logo compressor (host-native helper that RUNS during the build). Upstream's pb12.c is POSIX-only
# (unistd.h / read / write / void* arithmetic) and won't compile under MSVC, so on Windows we build
# RetroPlug's portable port instead of the SameBoy submodule source. Both emit an identical pb12 stream.
#
# Cross-compile knob (mirrors TJSC_EXECUTABLE): a target-arch pb12 can't run on the host, so a cross-build
# points at a host-built one; leave empty for a native build (the in-tree target is built + run as usual).
set(SAMEBOY_PB12_EXECUTABLE "" CACHE FILEPATH
    "Path to a host-built sameboy_pb12 for cross-compilation. Leave empty to build + run the in-tree tool.")
if(SAMEBOY_PB12_EXECUTABLE)
    set(_PB12_COMMAND "${SAMEBOY_PB12_EXECUTABLE}")
    set(_PB12_DEP "${SAMEBOY_PB12_EXECUTABLE}")
else()
    if(WIN32)
        add_executable(sameboy_pb12 "${CMAKE_SOURCE_DIR}/cmake/pb12.c")
    else()
        add_executable(sameboy_pb12 "${BR_SRC}/pb12.c")
    endif()
    set_target_properties(sameboy_pb12 PROPERTIES
        C_STANDARD 99
        RUNTIME_OUTPUT_DIRECTORY "${BR_OBJ}"
    )
    set(_PB12_COMMAND sameboy_pb12)
    set(_PB12_DEP sameboy_pb12)
endif()

# SameBoyLogo PNG -> 2bpp -> pb12. Both intermediates live in BR_OBJ where
# the rgbasm --include path will look for SameBoyLogo.pb12.
add_custom_command(
    OUTPUT "${BR_OBJ}/SameBoyLogo.2bpp"
    COMMAND "${RGBGFX}" -Z -u -c embedded
        -o "${BR_OBJ}/SameBoyLogo.2bpp" "${BR_SRC}/SameBoyLogo.png"
    DEPENDS "${BR_SRC}/SameBoyLogo.png"
    COMMENT "rgbgfx SameBoyLogo.2bpp"
    VERBATIM
)
add_custom_command(
    OUTPUT "${BR_OBJ}/SameBoyLogo.pb12"
    COMMAND "${_PB12_COMMAND}" < "${BR_OBJ}/SameBoyLogo.2bpp"
        > "${BR_OBJ}/SameBoyLogo.pb12"
    DEPENDS ${_PB12_DEP} "${BR_OBJ}/SameBoyLogo.2bpp"
    COMMENT "pb12 SameBoyLogo.pb12"
    VERBATIM
)

set(SAMEBOY_BOOTROMS
    dmg_boot
    mgb_boot
    cgb_boot
    cgb0_boot
    cgb_boot_fast
    agb_boot
    sgb_boot
    sgb2_boot
)

set(_bootrom_headers "")
foreach(name IN LISTS SAMEBOY_BOOTROMS)
    set(asm "${BR_SRC}/${name}.asm")
    set(obj "${BR_OBJ}/${name}.o")
    set(bin "${BR_OBJ}/${name}.bin")
    set(hdr "${BR_OUT}/${name}.h")

    add_custom_command(
        OUTPUT "${bin}"
        COMMAND "${RGBASM}" --include "${BR_OBJ}" --include "${BR_SRC}"
            -o "${obj}" "${asm}"
        COMMAND "${RGBLINK}" -x -o "${bin}" "${obj}"
        DEPENDS "${asm}" "${BR_OBJ}/SameBoyLogo.pb12"
        COMMENT "rgbasm ${name}.bin"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${hdr}"
        COMMAND "${CMAKE_COMMAND}"
            -DINPUT=${bin}
            -DSYMBOL=${name}
            -DOUTPUT=${hdr}
            -P "${CMAKE_SOURCE_DIR}/cmake/bin2h.cmake"
        DEPENDS "${bin}" "${CMAKE_SOURCE_DIR}/cmake/bin2h.cmake"
        COMMENT "bin2h ${name}.h"
        VERBATIM
    )
    list(APPEND _bootrom_headers "${hdr}")
endforeach()

add_custom_target(sameboy_bootroms DEPENDS ${_bootrom_headers})

# NOTE: the `sameboy` target's dependency on sameboy_bootroms and the generated
# include dir are wired in cmake/sameboy.cmake AFTER the sameboy library is
# created — this file only produces the bootrom headers + the sameboy_bootroms
# target, so it can be include()'d before `sameboy` exists (needed by the
# Windows clang-cl path, where the core objects depend on sameboy_bootroms).
