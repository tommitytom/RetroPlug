# The rp.devtest node types (src/devtest/DevtestNodes.cpp): what a device image's check runs a plan of, beside the
# portable packages' types, and what the device-mode test's exported-plan cases run. Added by retroplug_package(devtest)
# (cmake/packages.cmake). No product that ships lists it: the device check (packages/graph-runtime/device-check), whose
# image carries it, the plan exporter's tool, which exports that image's plan, and the device-mode test do.
retroplug_package_library(retroplug-devtest-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devtest/DevtestNodes.cpp)
target_include_directories(retroplug-devtest-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-devtest-nodes PUBLIC retroplug-node-sdk)
