# Minimal CMake wrapper for the vendored r8brain-free resampler.
# Only r8bbase.cpp needs to compile; the rest is header-only / .inc.
# Matches the legacy premake module (old/premake/dep/r8brain.lua).
add_library(r8brain STATIC r8bbase.cpp)
target_include_directories(r8brain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(r8brain PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14)
