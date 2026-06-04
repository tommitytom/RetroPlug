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

# pb12 logo compressor (host-native helper).
add_executable(sameboy_pb12 "${BR_SRC}/pb12.c")

set(_sameboy_pb12_properties
    C_STANDARD 99
    RUNTIME_OUTPUT_DIRECTORY "${BR_OBJ}"
)
if(APPLE AND CMAKE_OSX_ARCHITECTURES AND CMAKE_HOST_SYSTEM_PROCESSOR)
    set(_sameboy_pb12_host_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if(_sameboy_pb12_host_arch STREQUAL "aarch64")
        set(_sameboy_pb12_host_arch "arm64")
    endif()
    set(_sameboy_pb12_supported_archs x86_64 arm64)
    if(_sameboy_pb12_host_arch IN_LIST _sameboy_pb12_supported_archs)
        list(APPEND _sameboy_pb12_properties
            OSX_ARCHITECTURES "${_sameboy_pb12_host_arch}"
        )
    endif()
endif()
set_target_properties(sameboy_pb12 PROPERTIES ${_sameboy_pb12_properties})

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
    COMMAND sameboy_pb12 < "${BR_OBJ}/SameBoyLogo.2bpp"
        > "${BR_OBJ}/SameBoyLogo.pb12"
    DEPENDS sameboy_pb12 "${BR_OBJ}/SameBoyLogo.2bpp"
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

# Make the generated dir visible to anyone linking `sameboy`, and force the
# headers to exist before `sameboy` (and its dependents) build.
target_include_directories(sameboy PUBLIC "${CMAKE_BINARY_DIR}/generated")
add_dependencies(sameboy sameboy_bootroms)
