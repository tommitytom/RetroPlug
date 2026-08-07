# wjwwood/serial: cross-platform serial-port library (submodule at deps/serial),
# built as a static library for the Everdrive N8 Pro bridge (retroplug-cli n8-bridge).
# Its own CMake is catkin-flavored, so we compile the handful of sources directly here
# (mirrors cmake/sameboy.cmake). Linux enumeration is sysfs/glob-based, so there is no
# libudev dependency; macOS uses IOKit, Windows uses SetupAPI.

set(SERIAL_DIR "${CMAKE_SOURCE_DIR}/deps/serial")

if(NOT EXISTS "${SERIAL_DIR}/include/serial/serial.h")
    message(FATAL_ERROR
        "deps/serial is empty. Run: git submodule update --init --recursive")
endif()

set(SERIAL_SOURCES "${SERIAL_DIR}/src/serial.cc")
if(WIN32)
    list(APPEND SERIAL_SOURCES
        "${SERIAL_DIR}/src/impl/win.cc"
        "${SERIAL_DIR}/src/impl/list_ports/list_ports_win.cc")
elseif(APPLE)
    list(APPEND SERIAL_SOURCES
        "${SERIAL_DIR}/src/impl/unix.cc"
        "${SERIAL_DIR}/src/impl/list_ports/list_ports_osx.cc")
else()
    list(APPEND SERIAL_SOURCES
        "${SERIAL_DIR}/src/impl/unix.cc"
        "${SERIAL_DIR}/src/impl/list_ports/list_ports_linux.cc")
endif()

add_library(serial STATIC ${SERIAL_SOURCES})
target_include_directories(serial PUBLIC "${SERIAL_DIR}/include")
set_target_properties(serial PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 11)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Vendored upstream lib — silence all its warnings (we don't own them).
    target_compile_options(serial PRIVATE -w)
endif()

# Platform link libraries that wjwwood/serial's own CMake would otherwise add.
if(WIN32)
    target_link_libraries(serial PUBLIC setupapi)
elseif(APPLE)
    target_link_libraries(serial PUBLIC "-framework IOKit" "-framework CoreFoundation")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(serial PUBLIC Threads::Threads)
endif()
