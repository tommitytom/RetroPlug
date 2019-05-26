cmake_minimum_required(VERSION 3.0.0)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)

# Create the executable target
add_executable(lsdj-wavetable-import main.cpp wavetable_importer.hpp wavetable_importer.cpp ../common/common.hpp ../common/common.cpp)
source_group(\\ FILES main.cpp wavetable_importer.hpp wavetable_importer.cpp ../common/common.hpp ../common/common.cpp)

target_compile_features(lsdj-wavetable-import PUBLIC cxx_std_14)
target_include_directories(lsdj-wavetable-import PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(lsdj-wavetable-import liblsdj ${Boost_LIBRARIES})

install(TARGETS lsdj-wavetable-import DESTINATION bin)
