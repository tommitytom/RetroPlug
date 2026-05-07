cmake_minimum_required(VERSION 3.0.0)

# Create the executable target
add_executable(lsdj-wavetable-import main.cpp wavetable_importer.hpp wavetable_importer.cpp ../common/common.hpp ../common/common.cpp)
source_group(\\ FILES main.cpp wavetable_importer.hpp wavetable_importer.cpp ../common/common.hpp ../common/common.cpp)

target_compile_features(lsdj-wavetable-import PUBLIC cxx_std_14)
target_include_directories(lsdj-wavetable-import PUBLIC ${PROJECT_SOURCE_DIR}/dependency)
target_link_libraries(lsdj-wavetable-import liblsdj)

install(TARGETS lsdj-wavetable-import DESTINATION bin)
