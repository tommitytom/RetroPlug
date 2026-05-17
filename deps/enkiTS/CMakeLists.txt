# Minimal CMake wrapper for the vendored enkiTS task scheduler.
# We only need the C++ interface, not the C bindings — so just TaskScheduler.cpp.
# Matches the legacy premake module (old/premake/dep/enkits.lua).
add_library(enkiTS STATIC src/TaskScheduler.cpp)
target_include_directories(enkiTS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(enkiTS PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14)
# enkiTS pulls in std::thread; on Linux that resolves to libpthread via
# Threads::Threads (the canonical CMake module).
find_package(Threads REQUIRED)
target_link_libraries(enkiTS PUBLIC Threads::Threads)
