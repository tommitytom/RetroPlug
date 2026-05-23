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
    UserConfigTests.cpp
    ../src/config/RecentFiles.cpp
    ../src/config/UserConfig.cpp
    ../src/config/UserConfigPaths.cpp
    ../src/system/sameboy/RomSniffer.cpp
    ../src/system/sameboy/roles/ArduinoboyMaster.cpp
    # LSDJ kit-compile chain — pure C++, links r8brain for resampling.
    ../src/lsdj/Effects.cpp
    ../src/lsdj/KitUtil.cpp
)

target_include_directories(retroplug-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(retroplug-tests PRIVATE
    Catch2::Catch2WithMain
    rpcpp
    r8brain
    efsw
)

# rpcpp is INTERFACE-linked here for its transitive reflectcpp + yyjson — used
# by ProjectSerialization (rfl::json::read/write) and the TaggedUnion in
# SystemConfig.hpp.

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
    ../src/system/mesen/MesenSystem.cpp
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
