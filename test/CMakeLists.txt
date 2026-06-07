# Unit tests for the framework-side primitives. Deliberately does NOT link
# DPF, LVGL, txiki.js, or SameBoy — these are pure C++ tests of the
# transport queues, the lock-free framebuffer, and the project lifecycle.
# Anything that needs a host (audio thread, JS engine, LVGL display) is out
# of scope here; those are integration concerns.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(retroplug-tests
    ArduinoboyMasterTests.cpp
    CommandQueueTests.cpp
    EventQueueTests.cpp
    FrameBufferTripleTests.cpp
    KitUtilTests.cpp
    MemoryAccessorTests.cpp
    MemorySnapshotTripleTests.cpp
    ProjectTests.cpp
    RecentFilesTests.cpp
    RomSnifferTests.cpp
    SavCodecTests.cpp
    SystemBaseDefaultsTests.cpp
    UserConfigTests.cpp
    ../src/config/RecentFiles.cpp
    ../src/config/UserConfig.cpp
    ../src/config/UserConfigPaths.cpp
    ../src/system/sameboy/RomSniffer.cpp
    ../src/system/sameboy/roles/ArduinoboyMaster.cpp
    # LSDJ kit-compile chain — pure C++, links r8brain for resampling.
    ../src/lsdj/Effects.cpp
    ../src/lsdj/KitUtil.cpp
    # LSDJ sav codec (pure C++; reflectcpp via rpcpp).
    ../src/lsdj/codec/SongCodec.cpp
    ../src/lsdj/codec/Compression.cpp
    ../src/lsdj/codec/SavCodec.cpp
)

target_include_directories(retroplug-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Directory holding the generated per-version golden .sav corpus (sibling
# resources tree; see AGENTS.md). SavCodecTests decodes against it.
target_compile_definitions(retroplug-tests PRIVATE
    RETROPLUG_LSDJ_SAV_DIR="${CMAKE_SOURCE_DIR}/../resources/roms/lsdj"
)

target_include_directories(retroplug-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/deps/miniz
)

target_link_libraries(retroplug-tests PRIVATE
    Catch2::Catch2WithMain
    rpcpp
    r8brain
    efsw
    miniz
)

# rpcpp is INTERFACE-linked here for its transitive reflectcpp + yyjson — used
# by ProjectSerialization (rfl::json::read/write) and the TaggedUnion in
# SystemConfig.hpp. miniz is linked for ProjectSerialization's zip writer /
# reader (see src/util/MinizZip.hpp).

include(Catch)
catch_discover_tests(retroplug-tests)

# -----------------------------------------------------------------------------
# retroplug-mesen-tests — diagnostic probes of Mesen's process-global state
# under multi-instance use. Separate binary so retroplug-tests stays free of
# the heavy Mesen static lib (see comment at top of file).
# -----------------------------------------------------------------------------
add_executable(retroplug-mesen-tests
    MesenSingletonsTests.cpp
    MesenMultiInstanceTests.cpp
    MesenSystemBaseTests.cpp
    ../src/system/SystemBase.cpp
    ../src/system/mesen/MesenNesSystem.cpp
    ../src/system/mesen/MesenNesDebugSession.cpp
    ../src/system/mesen/MesenVideoDevice.cpp
    ../src/system/mesen/roles/NesN8MidiRole.cpp
)

target_include_directories(retroplug-mesen-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(retroplug-mesen-tests PRIVATE
    RETROPLUG_TEST_ROM_DIR="${CMAKE_SOURCE_DIR}/resources/roms"
)

target_link_libraries(retroplug-mesen-tests PRIVATE
    Catch2::Catch2WithMain
    rpcpp
    mesen
)

catch_discover_tests(retroplug-mesen-tests)

# -----------------------------------------------------------------------------
# retroplug-sameboy-tests — direct tests of SameBoySystem's SystemBase
# overrides (clone, SRAM/state I/O, restartEmulator, fast-boot toggle, reload-
# on-rom-change). Mirrors retroplug-mesen-tests; isolated binary so the main
# retroplug-tests stays free of SameBoy linkage.
#
# Requires a Game Boy ROM at the path baked into RETROPLUG_TEST_GB_ROM. The
# default is the sibling resources tree (`../resources/roms/lsdj/lsdj9_4_2.gb`)
# documented in AGENTS.md. Tests skip gracefully when the file is missing.
# -----------------------------------------------------------------------------
add_executable(retroplug-sameboy-tests
    SameBoySystemBaseTests.cpp
    ../src/system/SystemBase.cpp
    ../src/system/sameboy/LinkGroup.cpp
    ../src/system/sameboy/RomSniffer.cpp
    ../src/system/sameboy/SameBoySystem.cpp
    ../src/system/sameboy/roles/ArduinoboyMaster.cpp
    ../src/system/sameboy/roles/LsdjSyncRole.cpp
    ../src/system/sameboy/roles/LsdjKitPatchRole.cpp
    ../src/system/sameboy/roles/MgbPassthroughRole.cpp
    ../src/lsdj/Effects.cpp
    ../src/lsdj/KitUtil.cpp
)

target_include_directories(retroplug-sameboy-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

set(_RETROPLUG_TEST_GB_ROM
    "${CMAKE_SOURCE_DIR}/../resources/roms/lsdj/lsdj9_4_2.gb"
    CACHE FILEPATH
    "Path to a Game Boy ROM used by retroplug-sameboy-tests. Default is the \
sibling resources tree documented in AGENTS.md.")
target_compile_definitions(retroplug-sameboy-tests PRIVATE
    RETROPLUG_TEST_GB_ROM="${_RETROPLUG_TEST_GB_ROM}"
)

target_link_libraries(retroplug-sameboy-tests PRIVATE
    Catch2::Catch2WithMain
    rpcpp
    r8brain
    sameboy
)

catch_discover_tests(retroplug-sameboy-tests)

# -----------------------------------------------------------------------------
# retroplug-lsdj-diff-tests — differential oracle for the LSDJ sav codec.
# Compiles the vendored liblsdj (correct for song format versions <= 16) and
# decodes the same content-bearing savs with both liblsdj and our codec,
# asserting the semantic fields match. Isolated binary so retroplug-tests stays
# free of the legacy C library. (liblsdj's offsets/gates are the ground truth
# for fmt<=16; this is what proves the old-format `else` branches are correct.)
# -----------------------------------------------------------------------------
set(_LIBLSDJ_SRC ${CMAKE_SOURCE_DIR}/old/thirdparty/liblsdj/liblsdj/src)
add_executable(retroplug-lsdj-diff-tests
    LsdjDifferentialTests.cpp
    ../src/lsdj/codec/SongCodec.cpp
    ../src/lsdj/codec/Compression.cpp
    ../src/lsdj/codec/SavCodec.cpp
    # liblsdj (C), compiled directly into the test binary.
    ${_LIBLSDJ_SRC}/allocator.c        ${_LIBLSDJ_SRC}/bytes.c
    ${_LIBLSDJ_SRC}/chain.c            ${_LIBLSDJ_SRC}/compression.c
    ${_LIBLSDJ_SRC}/error.c            ${_LIBLSDJ_SRC}/groove.c
    ${_LIBLSDJ_SRC}/instrument.c       ${_LIBLSDJ_SRC}/instrument_kit.c
    ${_LIBLSDJ_SRC}/instrument_noise.c ${_LIBLSDJ_SRC}/instrument_pulse.c
    ${_LIBLSDJ_SRC}/instrument_wave.c  ${_LIBLSDJ_SRC}/phrase.c
    ${_LIBLSDJ_SRC}/project.c          ${_LIBLSDJ_SRC}/sav.c
    ${_LIBLSDJ_SRC}/song.c             ${_LIBLSDJ_SRC}/song_empty.c
    ${_LIBLSDJ_SRC}/speech.c           ${_LIBLSDJ_SRC}/synth.c
    ${_LIBLSDJ_SRC}/table.c            ${_LIBLSDJ_SRC}/vio.c
    ${_LIBLSDJ_SRC}/wave.c
)

target_include_directories(retroplug-lsdj-diff-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/old/thirdparty/liblsdj/liblsdj/include
    # liblsdj's .c files include their private headers bare ("speech.h").
    ${CMAKE_SOURCE_DIR}/old/thirdparty/liblsdj/liblsdj/include/lsdj
)

target_compile_definitions(retroplug-lsdj-diff-tests PRIVATE
    # Content-bearing savs spanning fmt3..fmt16 (liblsdj's own test fixtures).
    RETROPLUG_LSDJ_DIFF_SAV_DIR="${CMAKE_SOURCE_DIR}/old/thirdparty/liblsdj/resources/sav"
)

target_link_libraries(retroplug-lsdj-diff-tests PRIVATE
    Catch2::Catch2WithMain
    rpcpp
)

catch_discover_tests(retroplug-lsdj-diff-tests)

# -----------------------------------------------------------------------------
# retroplug-ui-test — headless LVGL/React UI tests. Boots the REAL UI bundle on
# LvglJsEngine + PluginJsBridge with no DPF / PUGL / OpenGL / Xvfb (the LVGL
# render is pure software; ui/UiTestHarness replicates only the non-GL subset of
# deps/dpf-widgets/generic/LVGL.cpp), then asserts on the rendered tree +
# lv_snapshot. Links lvgl-js-native (brings LvglJsEngine + lvgl + lv_binding_js
# native + lodepng) and the UI/bridge/project/system sources — but NOT DPF.
# Compiles the same generated UI bundle the plugin embeds.
# -----------------------------------------------------------------------------
add_executable(retroplug-ui-test
    ui/UiSmokeTests.cpp
    ui/UiTestHarness.cpp
    # UI/bridge surface (FILES_UI minus PluginUI.cpp, GamepadManager, and DPF's
    # LVGL widget — the harness supplies the headless display instead).
    ../src/PluginJsBridge.cpp
    ../src/PluginRpcService.cpp
    ../src/config/UserConfig.cpp
    ../src/config/UserConfigPaths.cpp
    ../src/config/RecentFiles.cpp
    ../src/project/Project.cpp
    ../src/lsdj/Effects.cpp
    ../src/lsdj/KitUtil.cpp
    ../src/lsdj/SampleCache.cpp
    ../src/lsdj/KitCompiler.cpp
    ../src/system/SystemBase.cpp
    ../src/system/sameboy/LinkGroup.cpp
    ../src/system/sameboy/RomSniffer.cpp
    ../src/system/sameboy/SameBoySystem.cpp
    ../src/system/sameboy/roles/ArduinoboyMaster.cpp
    ../src/system/sameboy/roles/LsdjSyncRole.cpp
    ../src/system/sameboy/roles/LsdjKitPatchRole.cpp
    ../src/system/sameboy/roles/MgbPassthroughRole.cpp
    ../src/system/mesen/MesenNesSystem.cpp
    ../src/system/mesen/MesenNesDebugSession.cpp
    ../src/system/mesen/MesenVideoDevice.cpp
    ../src/system/mesen/MesenGbaSystem.cpp
    ../src/system/mesen/roles/NesN8MidiRole.cpp
    # The embedded React UI bundle (QuickJS bytecode), same as the plugin.
    ${UI_BUNDLE_C}
)

# The bundle is generated by ui-regenerate; make sure it's fresh before linking.
add_dependencies(retroplug-ui-test ui-regenerate)

target_include_directories(retroplug-ui-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    # txiki.js / QuickJS headers for the JS trampoline in UiTestHarness.cpp.
    # (lvgl-js-native PUBLIC-propagates the lv_binding_js render include dir, so
    # "native/..." headers resolve transitively.)
    ${CMAKE_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/src
    ${CMAKE_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/deps/quickjs
    ${CMAKE_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/deps/miniz
)

target_compile_definitions(retroplug-ui-test PRIVATE
    RP_ROM_DIR="${CMAKE_SOURCE_DIR}/resources/roms"
)

target_link_libraries(retroplug-ui-test PRIVATE
    Catch2::Catch2WithMain
    lvgl-js-native   # LvglJsEngine + lvgl + lv_binding_js native + lodepng (+ tjs)
    rpcpp
    sameboy
    mesen
    r8brain
    enkiTS
    miniaudio
    efsw
    miniz
)

set_target_properties(retroplug-ui-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
)

catch_discover_tests(retroplug-ui-test)
