# packages/native: the C++ side of RetroPlug. It is the shell, which is the backend static library, the hosts built on it, the
# DPF plugin, and the shell's test binaries. A console lives in its package (packages/sameboy, packages/mesen), the
# host's I/O nodes in packages/host-io, the contract in packages/system.
#
# This file holds the libraries and the straightforward executables. The parts that are large or
# self-contained are include()d from cmake/ so this stays readable top to bottom:
#
#   cmake/node-addon.cmake    retroplug-node, the N-API host (opt-in)
#   cmake/bundles.cmake       the embedded control-plane, React-UI and script-node bytecode
#   cmake/plugin.cmake        dpf_add_plugin + per-platform packaging
#   cmake/tests.cmake         the shell's eleven native test binaries (the console packages declare two more)
#   cmake/warnings.cmake      /W4 /WX on our own targets (MSVC)
#
# They are include()d, not add_subdirectory()'d, which matters: include() keeps the CURRENT
# directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still resolves to packages/native/ inside them and
# every relative source path reads as it did when it was inline. add_subdirectory would open a child
# scope, re-root that variable, and scope target properties differently.
#
# ORDER IS SEMANTIC. CMake runs top to bottom and a target must exist before anything modifies it: a
# block that calls target_sources() on retroplug-cli has to come after retroplug-cli is defined. Move
# an include() and the failure is a confusing "unknown target", not a clear one.

# quickjs.h's directory. Spelled out at six call sites before; it is deep enough that a typo in one
# of them would fail confusingly, and short enough here to read.
set(RP_QUICKJS_INCLUDE "${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs")

# reflect-cpp's templates instantiate enough COMDAT sections that any TU leaning on it (RenderHost, the
# host and CLI mains, ...) passes COFF's 2^16 section limit (C1128). /bigobj only lifts that limit, so
# set it directory-wide rather than chasing each TU that crosses it.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()

# Threads: the Launchpad scan thread needs pthreads, and so does efsw-static, which retroplug-file-watcher links along
# with Threads::Threads (efsw's CMake gives its shared library that alone). Found once, for this directory. (The DSP
# package finds its own, for r8brain, and the N8 pack its own, for N8Link's serial thread.)
find_package(Threads REQUIRED)

# A product's embedded bundles (the control plane, the UI, the script nodes and the render worker) are
# cmake/product-bundles.cmake's, declared there before the first product below: the script nodes and the render worker
# are target sources of each product now (the backend declares their arrays extern), so a product's script host loads
# its own node set rather than one every product shared.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/product-bundles.cmake)

# --- retroplug-tjs-runtime and retroplug-file-watcher: the two host runtime libraries
# (docs/modular-graph-phase-f2-design.md section 6). Each is a library of its own, linked by the hosts that use it, so
# a binary that needs neither the txiki control-plane host nor a filesystem watcher links neither: the backend (and
# every test or embedder that links it) is bare QuickJS and nothing else.

# txiki.js's host (dpf.js), which the control planes of the plugin, the SDL standalone, the headless host, the CLI and
# the class-id test instantiate. PUBLIC because its includers write "dpfjs/host/TjsHostRuntime.hpp", and because the
# library's own header is what brings tjs.h/quickjs.h to them; `tjs` is PUBLIC for the same reason (its runtime types
# are in TjsHostRuntime's interface).
add_library(retroplug-tjs-runtime STATIC ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp)
target_include_directories(retroplug-tjs-runtime PUBLIC ${DPFJS_PATH}/src)
target_link_libraries(retroplug-tjs-runtime PUBLIC tjs)
set_target_properties(retroplug-tjs-runtime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)  # linked into the shared plugin module

# efsw behind HostRpcService's FileWatcher interface: the plugin's watcher (config.json, bindings/, registered ROMs).
# Only a host that wants one links it, and it is handed to HostRpcService as the interface, so the service and every
# includer of it stay clear of efsw's headers. It is efsw-static's only consumer here, so it adds the threads efsw's
# own CMake gives its shared library alone, and its link interface is whole for a host that takes it and nothing else.
add_library(retroplug-file-watcher STATIC src/host/rpc/NativeFileWatcher.cpp)
target_include_directories(retroplug-file-watcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-file-watcher PUBLIC efsw-static Threads::Threads)
set_target_properties(retroplug-file-watcher PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

# retroplug-backend: the Backend as a static library — everything except a host
# entry point, so both the test host (retroplug-host) and the DPF plugin compose the SAME
# Engine/Backend/ScriptHost. Includes + links are PUBLIC so consumers inherit them.
add_library(retroplug-backend STATIC
    # The decoded-audio cache: an offline job's file inputs decode through it (host/engine/OfflineJobs.cpp), which is how
    # every kit compile reads a source. Console- and format-neutral; its content hash is the DSP package's util/Hash.hpp,
    # whose include root LIBS below takes.
    src/kit/SampleCache.cpp
    # fsyncPath needs <windows.h> (FlushFileBuffers), so it stays out of line rather than in every includer.
    src/util/FileSync.cpp
    # lodepng (vendored under lv_binding_js): one PNG encoder, compiled directly so consumers don't pull the full
    # lvgl-js-native static lib for it.
    ${DPFJS_PATH}/deps/lv_binding_js/src/render/native/core/img/png/lodepng.cpp
    src/host/rpc/HostRpcService.cpp         # fs / config / zip (a host's file watcher is handed to it: FileWatcher)
    src/host/rpc/EngineRpcService.cpp       # emulator surface over Engine + Invoker
    src/host/rpc/DebugRpcService.cpp        # live-core inspection over Engine (CLI-only facet)
    src/host/rpc/AudioDriverRpcService.cpp  # background audio thread + observation atomics
    src/host/engine/Engine.cpp              # the engine: the graph + the script host + the staged MIDI, runs blocks
    src/host/engine/SnapshotRegistry.cpp    # owned id-keyed frame/state/SRAM store — the control plane's one read door
    src/host/engine/EngineInvoker.cpp       # QueuedInvoker - the one mutation path to the Engine
    src/host/engine/OfflineJobs.cpp         # offline jobs: a document run to completion on a worker host of its own
    src/host/IdleReclaimProbe.cpp           # hosts' idle-tick self-test (HostServices::reclaimOnIdle), smoke-driven
    src/host/dsp/ScriptCompiler.cpp         # bare-QuickJS bytecode compiler (script packs' source, the tests' toys)
    src/host/script/ScriptHost.cpp          # the script-node runtimes: TS nodes behind the C ABI, a pair per pack
    src/host/render/RenderHost.cpp          # bare-QuickJS render host (own Engine, runs the worker bundle)
)
target_include_directories(retroplug-backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # `native/core/img/png/lodepng.h` lives under this root (in dpf.js).
    ${DPFJS_PATH}/deps/lv_binding_js/src/render
    # MinizZip.hpp -> miniz.h (txiki's v3.1.0; matches the linked `miniz` lib)
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/miniz
    # quickjs.h: the script host, the compiler and the render host are bare QuickJS (the `qjs` library below).
    ${RP_QUICKJS_INCLUDE}
)

target_link_libraries(retroplug-backend PUBLIC
    # The system contract every console implements and the engine drives (packages/system), and the build request a
    # system node's own package carries: the engine's own headers are its public headers.
    retroplug-system
    # The host I/O package's service struct and staging bounds, which the engine implements and sizes from
    # (packages/host-io). The console packages are NOT linked here: a product links the ones it lists
    # (retroplug_product), and the engine reaches their node types through its generated rp_register_packages().
    retroplug-host-io
    retroplug-graph     # the graph executor, planner and GraphHost the engine runs on (packages/graph-runtime)
    # The core and DSP packages: the offline job pool registers its fixed subset of them itself (OfflineJobs.cpp),
    # where the engine registers the product's list (rp_register_packages, graph/Packages.hpp).
    retroplug-core-nodes
    retroplug-dsp-nodes
    retroplug-dsp-resample-nodes
    qjs                 # bare QuickJS: the script host, the compiler and the render host (no txiki, no libuv)
    rpcpp               # transitively pulls reflectcpp
    miniaudio           # header-only audio decoder used by SampleCache
    miniz               # used by projectConfigToZip for --save-rplg
    Threads::Threads
)
# SampleCache's content hash is util/Hash.hpp's FNV-1a, which lives in the DSP package beside the dither kernel that draws
# its seeds with it. The shell takes that header alone from there, its include root and not the kernels' library, which
# nothing here calls.
target_include_directories(retroplug-backend PRIVATE
    $<TARGET_PROPERTY:retroplug-dsp-kernels,INTERFACE_INCLUDE_DIRECTORIES>)

set_target_properties(retroplug-backend PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON  # linked into the shared plugin module (below)
)

# retroplug-host: the test host — the backend lib + a main() that evals a TS bundle over the
# __rpcSend bridge. Output binary lands in build/bin/.
add_executable(retroplug-host src/main.cpp)

# The txiki host it runs its control plane on (src/main.cpp): its own library, not the backend's.
target_link_libraries(retroplug-host PRIVATE retroplug-backend retroplug-tjs-runtime)
# The staging surface link it stages control-surface messages into (src/main.cpp): the pack's harness, wired into the
# backend's hook. The backend itself links no Launchpad target (the configure check refuses one).
target_link_libraries(retroplug-host PRIVATE retroplug-launchpad-harness)
# Its own N8 provider over a fake cart and its own Launchpad facet over fake ports (src/main.cpp): a test host starts the
# providers it needs itself, over fakes, rather than rp_start_providers over real hardware.
retroplug_product(retroplug-host PACKAGES ${RP_DESKTOP_PACKAGES} PROVIDERS n8 launchpad midi)
# FakeN8.hpp, the model of the cart this test host starts the N8 provider over, FakePorts.hpp and FakeMidiBackend.hpp, the
# scripted Launchpad and MIDI system its facets serve (src/main.cpp): test headers, in this binary alone, so no product
# carries a fake device.
target_include_directories(retroplug-host PRIVATE ${CMAKE_SOURCE_DIR}/packages/n8/native/test
                                                  ${CMAKE_SOURCE_DIR}/packages/launchpad/native/test
                                                  ${CMAKE_SOURCE_DIR}/packages/midi/native/test-support)

set_target_properties(retroplug-host PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# retroplug-render-host-test: submits renders from JSON job specs as engine jobs on an OfflineJobs pool (one,
# several in parallel, or one cancelled), a manual smoke beside the [engine-jobs] cases and a binary to build in
# build-tsan/ and build-asan/. EXCLUDE_FROM_ALL: built by name, not part of a normal build.
add_executable(retroplug-render-host-test EXCLUDE_FROM_ALL test/render-host-main.cpp)
target_link_libraries(retroplug-render-host-test PRIVATE retroplug-backend)
retroplug_product(retroplug-render-host-test PACKAGES ${RP_DESKTOP_PACKAGES})
set_target_properties(retroplug-render-host-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The root CLI dispatcher bundle — cli/cli.ts → tjsc bytecode C array, COMPILED INTO retroplug-cli
# (mirrors the plugin's cp-/ui-bundle recipe). It owns command routing + all help; esbuild follows its
# imports (cli.ts → tools.ts → sessions/render.ts → session.ts). tjsc derives the array name from the
# output stem → rp_cli_bundle[] +
# rp_cli_bundle_size. Derived; never committed. Adding a command is pure TS (cli/tools.ts) — no change here.
set(CLI_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/cli-bundle.js")
set(CLI_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/cli-bundle_data.c")
set_source_files_properties(${CLI_BUNDLE_C} PROPERTIES GENERATED TRUE)  # produced before the CLI compiles
add_custom_target(retroplug-cli-bundle
    BYPRODUCTS ${CLI_BUNDLE_JS} ${CLI_BUNDLE_C} ${CLI_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CMAKE_SOURCE_DIR}/packages/retroplug/cli/cli.ts ${CLI_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${CLI_BUNDLE_C}.new ${CLI_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLI_BUNDLE_C}.new ${CLI_BUNDLE_C}
    COMMENT "Bundling retroplug-cli dispatcher (esbuild + bytecode)"
    VERBATIM)
# Order against the in-tree tjsc target only; a cross-build's -DTJSC_EXECUTABLE is a host file
# path (not a target) — the custom command's DEPENDS already tracks it.
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-cli-bundle ${_TJSC_DEP})
endif()
# ...and against the shell's generator: cli.ts imports cli/generated/tools.ts and the dispatcher's own modules import
# src/generated, which a fresh tree has not written yet. Unbundled from this edge, a parallel build from a tree with no
# src/generated can bundle the CLI before the generator has written it (the desktop's set: these bundles are its).
add_dependencies(retroplug-cli-bundle ${RP_BUNDLE_desktop_GENERATOR})

# The TypeScript type stripper — ts-blank-space + the TS parser → tjsc bytecode C array, COMPILED INTO
# retroplug-cli so `retroplug-cli test/run` can execute `.ts` directly with no Node, npm or bundler on
# the consumer's machine. GLOBAL CODE (tjsc WITHOUT -m, like the render worker): cli/main.cpp loads it
# with JS_ReadObject + JS_EvalFunction on demand, so the ~4 MB never touches the startup path of
# `render` / `n8-bridge` / any other command. Array name derives from the output stem →
# rp_ts_stripper_bundle[] + _size. Derived; never committed.
set(TS_STRIPPER_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/ts-stripper-bundle.js")
set(TS_STRIPPER_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/ts-stripper-bundle_data.c")
set_source_files_properties(${TS_STRIPPER_BUNDLE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-ts-stripper-bundle
    BYPRODUCTS ${TS_STRIPPER_BUNDLE_JS} ${TS_STRIPPER_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-ts-stripper.mjs ${TS_STRIPPER_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${TS_STRIPPER_BUNDLE_C}.new ${TS_STRIPPER_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TS_STRIPPER_BUNDLE_C}.new ${TS_STRIPPER_BUNDLE_C}
    COMMENT "Bundling the TypeScript type stripper (esbuild + global-code bytecode)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-ts-stripper-bundle ${_TJSC_DEP})
endif()

# The test SDK, embedded twice, for two different consumers:
#
#   rp_retroplug_cli[]  MODULE bytecode, compiled with `tjsc -n retroplug-cli` so the module NAME is
#                       exactly `retroplug-cli`. cli/main.cpp JS_ReadObject's it before evaluating a
#                       session, which registers it in QuickJS's loaded-module table; an
#                       `import ... from "retroplug-cli"` then resolves from that table, because
#                       js_host_resolve_imported_module looks there BEFORE calling the module loader
#                       (quickjs.c: "first look at the loaded modules"). So the SDK never touches the
#                       filesystem - there is no .js to generate, sync, or let go stale.
#   rp_sdk_dts[]        the .d.ts as raw text. Still materialized next to a consumer's tests, because
#                       editors and `tsc` read declarations off disk; a bare specifier resolves to it
#                       via a tsconfig `paths` entry.
#
# The relative-import form could never work this way: tjs_module_normalizer only consults the import
# map for BARE specifiers (modules.c, `if (name[0] != '.')`), so "../sdk/x.js" always hits the disk.
set(SDK_ASSETS_C "${CMAKE_BINARY_DIR}/native/sdk-assets_data.c")
set(SDK_MODULE_C "${CMAKE_BINARY_DIR}/native/sdk-module_data.c")
set_source_files_properties(${SDK_ASSETS_C} ${SDK_MODULE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-cli-sdk-assets
    BYPRODUCTS ${SDK_ASSETS_C} ${SDK_ASSETS_C}.new ${SDK_MODULE_C} ${SDK_MODULE_C}.new
    # Builds build/cli-sdk/{retroplug-cli.js,.d.ts}, then embeds the .d.ts + a content stamp.
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed-sdk.mjs
            ${CMAKE_BINARY_DIR}/cli-sdk ${SDK_ASSETS_C}.new
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK_ASSETS_C}.new ${SDK_ASSETS_C}
    # ...and compiles the same bundle to module bytecode under the name a test imports.
    COMMAND ${_TJSC_COMMAND} -m -n retroplug-cli -s -p rp_ -o ${SDK_MODULE_C}.new
            ${CMAKE_BINARY_DIR}/cli-sdk/retroplug-cli.js
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK_MODULE_C}.new ${SDK_MODULE_C}
    COMMENT "Embedding the retroplug-cli test SDK (module bytecode + .d.ts)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-cli-sdk-assets ${_TJSC_DEP})
endif()
# The SDK's bundle embeds the CLI's dispatcher (tools/embed-sdk.mjs), which imports the generated tools and modules: the
# generator must have written them first, as for the dispatcher's own bundle.
add_dependencies(retroplug-cli-sdk-assets ${RP_BUNDLE_desktop_GENERATOR})

# retroplug-cli: the standalone runner — the backend lib + a main() that evals either the COMPILED-IN
# dispatcher bundle (commands + help) or, when argv[1] is a `.js`, a session file by path. No Node at
# runtime. Output binary lands in build/bin/.
add_executable(retroplug-cli cli/main.cpp ${CLI_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C} ${SDK_ASSETS_C} ${SDK_MODULE_C})

target_link_libraries(retroplug-cli PRIVATE retroplug-backend retroplug-tjs-runtime)
retroplug_product(retroplug-cli PACKAGES ${RP_DESKTOP_PACKAGES} PROVIDERS n8 launchpad midi)
add_dependencies(retroplug-cli retroplug-cli-bundle)             # the embedded dispatcher bytecode must exist first
add_dependencies(retroplug-cli retroplug-ts-stripper-bundle)     # ...and the stripper bytecode
add_dependencies(retroplug-cli retroplug-cli-sdk-assets)         # ...and the embedded SDK

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The Node-API addon host (opt-in: -DRETROPLUG_NODE_ADDON=ON).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/node-addon.cmake)

# The example sessions, esbuild-bundled to runnable .js (mirroring the cp-bundle recipe). A package's examples come
# from its manifest's `retroplug.examples` list, in product order, and the three the shell owns - the ones that name no
# cart - stay here. A normal build yields both the binary and demos the binary can run, e.g.
# build/bin/retroplug-cli build/cli/mgb-smoke.js.
set(CLI_SESSIONS "${CMAKE_SOURCE_DIR}/packages/retroplug/cli/sessions")
retroplug_examples(EXAMPLE_NAMES EXAMPLE_ENTRIES)
list(APPEND EXAMPLE_NAMES render-rom render-song rom-test)
list(APPEND EXAMPLE_ENTRIES
    "${CLI_SESSIONS}/render-rom.ts" "${CLI_SESSIONS}/render-song.ts" "${CLI_SESSIONS}/rom-test.ts")
# One bundle per name: the shell's three are appended to the packages' list, so a name declared twice (by two packages,
# or by a package and the shell) would have one session overwrite another's build/cli/<name>.js.
list(LENGTH EXAMPLE_NAMES _example_name_count)
list(REMOVE_DUPLICATES EXAMPLE_NAMES)
list(LENGTH EXAMPLE_NAMES _example_name_unique)
if(NOT _example_name_count EQUAL _example_name_unique)
    message(FATAL_ERROR "two example sessions share a name, so one would overwrite the other's build/cli/<name>.js")
endif()
set(EXAMPLE_SESSION_OUTPUTS "")
set(EXAMPLE_SESSION_COMMANDS "")
list(LENGTH EXAMPLE_NAMES _example_count)
math(EXPR _example_last "${_example_count} - 1")
foreach(_i RANGE ${_example_last})
    list(GET EXAMPLE_NAMES ${_i} _example_name)
    list(GET EXAMPLE_ENTRIES ${_i} _example_entry)
    list(APPEND EXAMPLE_SESSION_OUTPUTS "${CMAKE_BINARY_DIR}/cli/${_example_name}.js")
    list(APPEND EXAMPLE_SESSION_COMMANDS
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
                "${_example_entry}" "${CMAKE_BINARY_DIR}/cli/${_example_name}.js")
endforeach()
add_custom_target(retroplug-example-session ALL
    BYPRODUCTS ${EXAMPLE_SESSION_OUTPUTS}
    ${EXAMPLE_SESSION_COMMANDS}
    COMMENT "Bundling CLI example sessions (esbuild)"
    VERBATIM)
# An example session imports the shell's modules, which import src/generated: a fresh tree's parallel build can reach
# the session before the generator has written them.
add_dependencies(retroplug-example-session ${RP_BUNDLE_desktop_GENERATOR})

# The embedded control-plane, React-UI and script-node bytecode bundles.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundles.cmake)

# --- retroplug-sdl: the SDL2 standalone for framebuffer handhelds (Anbernic / muOS) + plain Linux.
# muOS has no X11 (fbdev + Mali EGL only), so the DPF/pugl JACK standalone can't run there. This host
# reuses the WHOLE core — retroplug-backend (Engine + cores + txiki) + lvgl-js-native (the LVGL/React
# bridge) + the SAME embedded cp/ui bundles the plugin links — and provides its own SDL2 window / audio /
# Part of the default `all` target, so build.sh builds it and CI covers it. Its deps are in-tree submodules
# (portaudio, and rtmidi through the midi package) or already required (SDL2 at root scope for gamepad input);
# PortAudio adds no new system lib to build - ALSA is present, and libpipewire-0.3 (if installed) just upgrades it
# to the native PipeWire host API, else it falls back to ALSA.
add_executable(retroplug-sdl
    sdl/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/GamepadManager.cpp  # SDL gamepad poll (shared UI-thread input)
    ${CP_BUNDLE_C}                # the embedded control-plane bytecode (rp_cp_bundle)
    ${UI_BUNDLE_C})               # the embedded React UI bytecode (rp_ui_bundle)

# `retroplug-audio` and `retroplug-midi` are unlisted library targets, linked by name (their manifests name no
# `native` item for them: listing one would link AudioHost/RtMidi into every product). The two BACKENDS are linked by
# retroplug_product: AUDIO_BACKENDS names them, so CI's `all` compiles both on every platform and no other product
# links either.
target_link_libraries(retroplug-sdl PRIVATE
    lvgl-js-native                # LvglJsEngine + lvgl + lv_binding_js native + lodepng
    retroplug-backend             # Engine + cores + rpcpp
    retroplug-tjs-runtime         # the txiki host its control plane runs on (sdl/main.cpp)
    retroplug-audio               # AudioHost, the backend interface and the null device
    retroplug-midi                # the host's MIDI stream over the midi package's backend (RtMidi, linked there)
    PkgConfig::SDL2)              # window + present + gamepad
retroplug_product(retroplug-sdl PACKAGES ${RP_DESKTOP_PACKAGES} PROVIDERS n8 launchpad AUDIO_BACKENDS portaudio sdl)

target_include_directories(retroplug-sdl PRIVATE
    # quickjs.h for the JS trampolines (lvgl-js-native PUBLIC-propagates the lv_binding_js render include
    # dir, so "native/..." resolves transitively; retroplug-backend brings "host/..." + "dpfjs/...").
    ${RP_QUICKJS_INCLUDE}
    # DistrhoUtils.hpp (header-only d_stderr logging) for GamepadManager.cpp — the one DPF header the
    # shared gamepad poller pulls in; no DPF linkage.
    ${DPFJS_PATH}/deps/dpf/distrho
    # portable-file-dialogs (header-only): the OS-native file picker backing __rp_openFileBrowser. No link
    # libs on Linux/macOS (it fork/execs zenity/kdialog/osascript); Windows pulls comdlg32/ole32 via its header.
    ${CMAKE_SOURCE_DIR}/deps/portable-file-dialogs)

add_dependencies(retroplug-sdl retroplug-cp-bundle retroplug-ui-bundle)  # embedded bundles must exist first

# Statically link libstdc++ on Linux so the binary doesn't depend on the target device's libstdc++
# version — handhelds (muOS/Buildroot) may ship an older one. But DO NOT static-link libgcc: the C++
# exception unwinder lives there, and a program that throws across a DSO boundary (we throw, and we load
# SDL2) needs ONE shared unwinder. Statically embedding libgcc gives a second unwinder whose .eh_frame
# registration collides with the system libgcc_s, corrupting unwinding into a SIGILL at startup on the
# device (observed on muOS/Cortex-A53). libgcc_s stays dynamic (present on the device, ABI-stable). glibc
# stays dynamic too (linked ≤ the build's version).
if(UNIX AND NOT APPLE)
    target_link_options(retroplug-sdl PRIVATE -static-libstdc++)
endif()

set_target_properties(retroplug-sdl PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# The DPF plugin (clap/vst3/vst2/au/jack) + per-platform packaging.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin.cmake)

# --- UI test harness -------------------------------------------------------------------------------
# retroplug-ui-test boots the React UI on a headless software LVGL display
# (RenderCore) driven by the BackendFacade RPC (UiHarness) — the counterpart of the
# legacy retroplug-ui-test. Only built on demand (not ALL). Reuses UI_BUNDLE_C + the ui-bundle target
# defined above the plugin.

add_executable(retroplug-ui-test
    test/ui/UiRunner.cpp
    test/ui/UiHarness.cpp
    test/ui/RenderCore.cpp
    ${UI_BUNDLE_C})               # the embedded UI bytecode (rp_ui_bundle)

target_link_libraries(retroplug-ui-test PRIVATE
    lvgl-js-native                # LvglJsEngine + lvgl + lv_binding_js native + lodepng (+ tjs)
    retroplug-backend) # BackendFacade + rpcpp + the cores + the txiki host
retroplug_product(retroplug-ui-test PACKAGES ${RP_DESKTOP_PACKAGES})

target_include_directories(retroplug-ui-test PRIVATE
    # quickjs.h for the JS trampolines in RenderCore/UiRunner (lvgl-js-native
    # PUBLIC-propagates the lv_binding_js render include dir, so "native/..." resolves transitively).
    ${RP_QUICKJS_INCLUDE})

set_target_properties(retroplug-ui-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The embedded bundle must exist before the test binary compiles it.
add_dependencies(retroplug-ui-test retroplug-ui-bundle)

# The shell's eleven native test binaries (EXCLUDE_FROM_ALL; built by name), and the configure checks the packages'
# manifests, boundary fixtures and device entries are held to.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests.cmake)

# Every product declared above, with its packages' entries, for the registration golden (test/engine-graph/
# Packages.test.cpp). After the last retroplug_product(), which tests.cmake and plugin.cmake hold.
retroplug_product_table(retroplug-engine-graph-test)

# Every binary that reaches the engine is a product, refused if it is not (cmake/packages.cmake). Deferred to the end of
# the root's CMakeLists, so a binary defined after this line, in any directory, is checked too. The carriers are
# computed from the backend, and no binary here is exempt: a test that needs a host runtime library links that library
# (retroplug-tjs-runtime, retroplug-file-watcher) instead of the backend, and is nobody's carrier.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL retroplug_check_products
    LIBRARIES retroplug-backend)

# And the bare-QuickJS ones stay bare: no txiki host, libuv or efsw in the backend, the Node addon, the render host or
# the two engine test binaries (cmake/packages.cmake's retroplug_check_runtime_libraries), refused at configure naming
# the target, the library and every step of the path that brought it in.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL retroplug_check_runtime_libraries)

# /W4 /WX on our own targets (MSVC), with per-number suppressions for what we do not own.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/warnings.cmake)
