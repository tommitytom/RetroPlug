# retroplug-cli: headless renderer + scripted-input runner.
#
# Reuses the DSP-side sources directly (Project.cpp + SameBoySystem.cpp);
# no DPF, no LVGL, no JS. Output binary lands in build/bin/.

add_executable(retroplug-cli
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/project/Project.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp
)

target_include_directories(retroplug-cli PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(retroplug-cli PRIVATE
    sameboy
    rpcpp     # transitively pulls reflectcpp
)

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
