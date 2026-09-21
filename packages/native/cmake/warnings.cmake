# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# --- warning level + warnings-as-errors (MSVC) ------------------------------------------------------
# Windows is the one platform whose compiler ever sees the `#if defined(_WIN32)` half of a platform
# guard, so a warning there is often the only signal that that half has rotted while linux/macos
# stayed green. /W4 + /WX makes the windows job say so. (It already earned its keep: it caught a
# std::string passed to a printf-style variadic in PluginDSP.cpp.)
#
# Scoped to RETROPLUG'S OWN targets -- never global: the vendored subtrees (mesen, sameboy,
# txiki/lvgl/libuv, portaudio, rtmidi, wjwwood/serial) warn freely and are silenced with per-number
# /wd#### rather than fixed, so a global /W4 /WX would fail on code we do not own.
#
# Note on /wd: MSVC rejects a SECOND /W-family flag with command-line warning D9025, which is not
# itself /wd-able -- so an unwanted warning must be disabled by NUMBER, never by lowering the level.
option(RETROPLUG_WERROR "Treat compiler warnings as errors in RetroPlug's own targets (MSVC)" ON)
if(MSVC)
    set(_rp_warn_flags
        /W4
        # Padding from an explicit alignas() is the POINT of the alignas (SpscRing pads its head and
        # tail indices onto separate cache lines); warning about it is noise.
        /wd4324
        # The rest are warnings raised INSIDE dependency headers our TUs include -- not our code to
        # change, and each is pinned to exactly one header so a new one still shows up:
        /wd4201   # nameless struct/union      -- r8brain CDSPSincFilterGen.h
        /wd4200   # zero-sized array in struct -- libwebsockets lws-dht.h (via txiki)
        /wd4245   # enum -> uint32_t mismatch  -- DPF DistrhoDetails.hpp
        /wd4206   # empty translation unit     -- DPF-generated _dpf_empty.c
        # MSVC version-dependent: 14.44 reports this in rpcpp, 14.51 does not. CI and the dev box
        # both run 14.51 now (windows-2025-vs2026), so this one is for anyone still on a 14.44
        # toolchain -- and a reminder that the two versions disagree about what a warning is.
        /wd4127)  # conditional expression is constant -- rpcpp TypedRpcServer.h
    if(RETROPLUG_WERROR)
        list(APPEND _rp_warn_flags /WX)
    endif()
    set(_rp_werror_targets
        retroplug-core retroplug-backend retroplug-host retroplug-cli retroplug-sdl
        retroplug retroplug-dsp retroplug-ui)
    foreach(_t IN LISTS _rp_werror_targets)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
            # The tree deliberately uses the portable CRT (fopen/strncpy/...) rather than the _s
            # variants, which do not exist off Windows. C4996 on them is advice we are declining.
            target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()

    # Two VENDORED SOURCES are compiled straight into our targets (rather than linked from a dep
    # library), so the target's /W4 lands on them. They are not ours to fix, but list the numbers
    # rather than blanket-disabling: a NEW warning class in either still fails the build loudly.
    # (Per-source options are appended after the target's, so these win over the /W4 above.)
    set_source_files_properties(
        ${DPFJS_PATH}/deps/lv_binding_js/src/render/native/core/img/png/lodepng.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4267;/wd4334;/wd4505")
    set_source_files_properties(
        ${DPFJS_PATH}/deps/dpf-widgets/generic/LVGL.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4100")
endif()
