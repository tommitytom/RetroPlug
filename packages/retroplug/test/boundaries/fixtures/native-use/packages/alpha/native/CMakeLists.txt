# The test binary opens beta's baked-in ROM by the path its manifest gives, and links no target of beta's, so the path
# is what declares the use. Gamma's path - ${CMAKE_SOURCE_DIR}/packages/gamma/ - is named in this comment and in no
# target property, so it is no use at all.
add_library(alpha-native INTERFACE)
target_compile_definitions(alpha-native INTERFACE RP_ROM_PATH="${CMAKE_SOURCE_DIR}/packages/beta/rom/x.gb")
