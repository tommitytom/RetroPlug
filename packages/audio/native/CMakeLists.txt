# The audio package (docs/modular-graph-phase-f5b-design.md sections 5.1 and 5.2): the backend interface, the null
# device, and AudioHost - the open / start / stop / reconfigure sequence, the muted fallback with a status, a lost
# device, the no-pull watchdog, render-ahead and the stats. Added by retroplug_package(audio) (cmake/packages.cmake).
#
# `retroplug-audio` is NOT a manifest `native` item, as `retroplug-midi` is not: listing it would link AudioHost into
# every product that lists the package, and its only product is the SDL standalone, which links it by name. A product
# that names no backend links no audio library at all (cmake/packages.cmake's AUDIO_BACKENDS).
#
# The fake is test support, not part of the library: it sits under native/test-support, an include root only the test
# binary adds (FakeAudioBackend.hpp), so a product cannot compile it.
if(RP_DEVICE_BUILD)
    return()
endif()

find_package(Threads REQUIRED)

retroplug_package_library(retroplug-audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/NullBackend.cpp)
target_include_directories(retroplug-audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-audio PRIVATE Threads::Threads)

# --- retroplug-audio-test-support: the fake's include root, for the binary that scripts a backend.
add_library(retroplug-audio-test-support INTERFACE)
target_include_directories(retroplug-audio-test-support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test-support)

# --- the package's test (cmake/test-binary.cmake), built by name: retroplug-audio-test is the per-channel tap suite's
# name (cmake/tests.cmake), so this one is retroplug-audio-host-test (design row 28, R15). Build it in build-tsan/ too:
# the
# render-ahead case is the one that would show a ring raced with a device callback.
retroplug_add_test_binary(
    NAME     retroplug-audio-host-test
    SOURCES  test/AudioHost.test.cpp
    LIBS     retroplug-audio               # the host, the interface and the null device
             retroplug-audio-test-support)  # audio/FakeAudioBackend.hpp
