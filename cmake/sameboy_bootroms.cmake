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

# pb12 logo compressor (host-native helper). Upstream's pb12.c is POSIX-only
# (unistd.h / read / write / void* arithmetic) and won't compile under MSVC, so
# on Windows we build RetroPlug's portable port instead of the SameBoy submodule
# source. Both emit an identical pb12 stream.
if(WIN32)
    add_executable(sameboy_pb12 "${CMAKE_SOURCE_DIR}/cmake/pb12.c")
else()
    add_executable(sameboy_pb12 "${BR_SRC}/pb12.c")
endif()
set_target_properties(sameboy_pb12 PROPERTIES
    C_STANDARD 99
    RUNTIME_OUTPUT_DIRECTORY "${BR_OBJ}"
)

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

# Stock SameBoy boot ROMs, assembled straight from the submodule sources.
# NOTE: cgb_boot_fast is intentionally NOT here — RetroPlug ships its own silent +
# flashless fast variants (below), and the RetroPlug cgb_boot_fast would collide with
# a submodule-built one on the generated header name.
set(SAMEBOY_BOOTROMS
    dmg_boot
    mgb_boot
    cgb_boot
    cgb0_boot
    agb_boot
    sgb_boot
    sgb2_boot
)

# RetroPlug-owned silent + flashless fast boot ROMs (cmake/bootroms/). These are thin
# stubs that DEF RP_FAST and include RetroPlug's own copies of the base ROMs (rp_*.asm);
# deps/sameboy is never edited. BR_RP_SRC is added to the rgbasm --include path so the
# stubs' `include "rp_*.asm"` and the base ROMs' `include "sameboot.inc"` both resolve.
set(BR_RP_SRC "${CMAKE_SOURCE_DIR}/cmake/bootroms")
set(RETROPLUG_BOOTROMS
    cgb_boot_fast
    cgb0_boot_fast
    agb_boot_fast
    dmg_boot_fast
    mgb_boot_fast
    sgb_boot_fast
    sgb2_boot_fast
)
# The owned base ROMs each fast stub `include`s — added to the fast variants' DEPENDS so
# an edit to a base retriggers the dependent stubs.
set(_rp_base_asm
    "${BR_RP_SRC}/rp_cgb_boot.asm"
    "${BR_RP_SRC}/rp_dmg_boot.asm"
    "${BR_RP_SRC}/rp_sgb_boot.asm"
)

set(_bootrom_headers "")
foreach(name IN LISTS SAMEBOY_BOOTROMS RETROPLUG_BOOTROMS)
    if(EXISTS "${BR_RP_SRC}/${name}.asm")
        set(asm "${BR_RP_SRC}/${name}.asm")
        set(_extra_dep ${_rp_base_asm})
    else()
        set(asm "${BR_SRC}/${name}.asm")
        set(_extra_dep "")
    endif()
    set(obj "${BR_OBJ}/${name}.o")
    set(bin "${BR_OBJ}/${name}.bin")
    set(hdr "${BR_OUT}/${name}.h")

    add_custom_command(
        OUTPUT "${bin}"
        COMMAND "${RGBASM}" --include "${BR_OBJ}" --include "${BR_SRC}" --include "${BR_RP_SRC}"
            -o "${obj}" "${asm}"
        COMMAND "${RGBLINK}" -x -o "${bin}" "${obj}"
        DEPENDS "${asm}" "${BR_OBJ}/SameBoyLogo.pb12" ${_extra_dep}
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
