# Unit tests for the framework-side primitives. Deliberately does NOT link
# DPF, LVGL, txiki.js, or SameBoy — these are pure C++ tests of the
# transport queues, the lock-free framebuffer, and the project lifecycle.
# Anything that needs a host (audio thread, JS engine, LVGL display) is out
# of scope here; those are integration concerns.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(retroplug-tests
    CommandQueueTests.cpp
    EventQueueTests.cpp
    FrameBufferTripleTests.cpp
    ProjectTests.cpp
)

target_include_directories(retroplug-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(retroplug-tests PRIVATE
    Catch2::Catch2WithMain
    rpcpp
)

# rpcpp is INTERFACE-linked here for its transitive reflectcpp + yyjson — used
# by ProjectSerialization (rfl::json::read/write) and the TaggedUnion in
# SystemConfig.hpp.

include(Catch)
catch_discover_tests(retroplug-tests)
