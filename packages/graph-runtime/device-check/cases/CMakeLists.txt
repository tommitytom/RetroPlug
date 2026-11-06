# rp-device-check-cases (main.cpp): the desktop half of the device check (docs/modular-graph-phase-e1-design.md section
# 7), which writes the header the device image is built against: every type the device builds, its default settings as
# msgpack and the layout they give. The root CMakeLists adds this directory once the packages it lists are added.
#
# A device product of the desktop build (cmake/packages.cmake) over the device check's own list (../packages.cmake), so
# it registers the same device entries the image does, built for the desktop, which can write their defaults and their
# layouts. The header names those entries (RP_DEVICE_CHECK_ENTRIES), and the image refuses one made for others. Built
# by name: tools/build-device-check.sh builds and runs it before configuring the image, and retroplug-device-mode-test
# runs the device check on the desktop against its output.
include(${CMAKE_CURRENT_SOURCE_DIR}/../packages.cmake)
add_executable(rp-device-check-cases EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../DeviceCheck.cpp)
set_target_properties(rp-device-check-cases PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
# Its cases are the device's C++ types: the packages' script packs are not on its list (NO_SCRIPTS).
retroplug_product(rp-device-check-cases PACKAGES ${RP_DEVICE_CHECK_PACKAGES} DEVICE NO_SCRIPTS)
get_property(_rp_entries GLOBAL PROPERTY RP_DEVICE_PRODUCT_rp-device-check-cases_ENTRIES)
list(JOIN _rp_entries " " _rp_entries)
target_compile_definitions(rp-device-check-cases PRIVATE "RP_DEVICE_CHECK_ENTRIES=\"${_rp_entries}\"")
# graph/Packages.hpp, the graph-runtime include root's, and node-sdk's descriptor parser and msgpack converter; the
# device check's own source for the host it creates the types under, the one the device creates them under too.
target_link_libraries(rp-device-check-cases PRIVATE retroplug-graph-exec retroplug-node-sdk)
target_include_directories(rp-device-check-cases PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
