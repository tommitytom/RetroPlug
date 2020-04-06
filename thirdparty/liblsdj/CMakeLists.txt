cmake_minimum_required(VERSION 3.0.0)

# Create the project
project(lsdj)

add_subdirectory(dependency/Catch2)
add_subdirectory(liblsdj)
add_subdirectory(lsdsng_export)
add_subdirectory(lsdsng_import)
# add_subdirectory(lsdj_clean)
add_subdirectory(lsdj_mono)
add_subdirectory(lsdj_wavetable_import)