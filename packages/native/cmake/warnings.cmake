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
        retroplug-backend retroplug-host retroplug-cli retroplug-sdl
        # The two host runtime libraries (cmake/packages.cmake's shell): a vendored txiki host file's compile is judged
        # at the level the backend's was, and the watcher's own code is ours.
        retroplug-tjs-runtime retroplug-file-watcher
        retroplug retroplug-dsp retroplug-ui
        # The workspace packages' targets (cmake/packages.cmake), defined in their own directories: a target's options
        # can be set from any directory, so the list stays here, where every one of our targets is named.
        retroplug-node-sdk retroplug-graph-exec retroplug-graph retroplug-core-nodes retroplug-system
        retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes retroplug-dsp-resample-nodes
        retroplug-n8 retroplug-n8-desktop retroplug-device-io-nodes retroplug-devtest-nodes
        # The console packages and the host I/O one: each core's own code, the build function, the node type.
        retroplug-sameboy retroplug-mesen retroplug-host-io
        # The Mega Drive console package: the system, the node and the build function.
        retroplug-blastem
        # The console packages' test binaries, whose cases build their cores directly and link no shell target.
        retroplug-sameboy-test retroplug-mesen-test retroplug-blastem-test
        # The Launchpad pack: its transport and its facet (PortClaim.cpp is the one file whose Windows half only this
        # compiler ever sees), and its own test binary.
        retroplug-launchpad retroplug-launchpad-desktop retroplug-launchpad-test
        # The midi package: the stream's epoch handshake, the RtMidi backend (supportsVirtual's and the virtual port's
        # Windows halves) and the facet, with the test binary, whose cases carry the _putenv_s half.
        retroplug-midi retroplug-midi-facet retroplug-midi-test
        # The audio package, its two backends and the AudioHost test: AudioHost's render-ahead ring, the PortAudio
        # backend (whose quietening is Linux code and whose Windows half is the PaUtil declaration) and the SDL one,
        # whose callback is SDLCALL and whose SDL_GetDefaultAudioInfo call is 2.24's.
        retroplug-audio retroplug-audio-portaudio retroplug-audio-sdl retroplug-audio-host-test
        # The device build of the node SDK, tested on the desktop (cmake/tests.cmake).
        retroplug-device-mode-objects retroplug-device-mode-test retroplug-box-usb-cdc
        # The device check's case generator (packages/graph-runtime/device-check/cases).
        rp-device-check-cases
        # The executor and the plan loader built as a device builds them, and the plan exporter's tool with its device
        # builds of the packages it lists (packages/graph-runtime).
        retroplug-graph-exec-device retroplug-export-plan retroplug-export-plan-device
        # The MicroQuickJS script host, its device-mode build and its runner (packages/graph-runtime).
        retroplug-mqjs-host retroplug-mqjs-host-device retroplug-mqjs-run)
    # And each device entry's device-mode compile (cmake/packages.cmake's _retroplug_device_check).
    get_property(_rp_device_checks GLOBAL PROPERTY RP_DEVICE_CHECK_TARGETS)
    list(APPEND _rp_werror_targets ${_rp_device_checks})
    # And a product's embedded bundle arrays (packages/native/cmake/product-bundles.cmake names them by the set, which
    # is a list's name, so the list stays wherever they are made).
    get_property(_rp_bundle_objects GLOBAL PROPERTY RP_BUNDLE_OBJECTS_TARGETS)
    list(APPEND _rp_werror_targets ${_rp_bundle_objects})
    foreach(_t IN LISTS _rp_werror_targets)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
            # The tree deliberately uses the portable CRT (fopen/strncpy/...) rather than the _s
            # variants, which do not exist off Windows. C4996 on them is advice we are declining.
            target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()

    # retroplug-r8brain-base is deliberately NOT on that list: its one source is vendored r8brain code (R8bBase.cpp
    # includes r8bbase.cpp), so it is built at the compiler's default level, the treatment the deps themselves get.

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

    # reflect-cpp is header-heavy and its templates instantiate inside OUR TUs, so /W4 judges its
    # code wherever we include it (0.26 trips C4244 in rfl/Literal.hpp). Mark it SYSTEM: consumers
    # then see its include dirs as -external:I, which the -external:W0 already on the command line
    # silences -- the same treatment sameboy/mesen/vcpkg get -- without a /wd4244 that would also
    # hide narrowing in our own code.
    if(TARGET reflectcpp)
        set_target_properties(reflectcpp PROPERTIES SYSTEM ON)
    endif()
endif()
