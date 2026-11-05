# retroplug-export-plan (main.cpp): the plan exporter's build-time tool (docs/modular-graph-phase-e3-design.md section
# 4). A device image's build runs it over the image's plan.json and embeds the header it writes, so a plan the device
# cannot run, or one its arena cannot hold, fails the firmware's build rather than its boot. The root CMakeLists adds
# this directory once the packages it lists are added.
#
# A device product of the desktop build (cmake/packages.cmake), so it registers the device entries of the packages it
# lists as the desktop builds them, which plan; beside them, the same entries as the device builds them
# (retroplug_device_mode_objects), which the loader's sizing pass sizes the plan with. Not shipped, and built by name:
# the scripts that build a device image build it first, and retroplug-device-mode-test runs it.
set(RP_EXPORT_PLAN_PACKAGES core dsp n8 device-io)
add_executable(retroplug-export-plan EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
set_target_properties(retroplug-export-plan PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
retroplug_product(retroplug-export-plan PACKAGES ${RP_EXPORT_PLAN_PACKAGES} DEVICE)
retroplug_device_mode_objects(retroplug-export-plan-device PACKAGES ${RP_EXPORT_PLAN_PACKAGES})
target_sources(retroplug-export-plan PRIVATE $<TARGET_OBJECTS:retroplug-export-plan-device>)
target_link_libraries(retroplug-export-plan PRIVATE retroplug-graph retroplug-node-sdk)
