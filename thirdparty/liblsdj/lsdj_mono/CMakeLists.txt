cmake_minimum_required(VERSION 3.0.0)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)

# Create the executable target
add_executable(lsdj-mono main.cpp ../common/common.hpp ../common/common.cpp)
source_group(\\ FILES main.cpp ../common/common.hpp ../common/common.cpp)

target_compile_features(lsdj-mono PUBLIC cxx_std_14)
target_include_directories(lsdj-mono PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(lsdj-mono liblsdj ${Boost_LIBRARIES})

install(TARGETS lsdj-mono DESTINATION bin)
