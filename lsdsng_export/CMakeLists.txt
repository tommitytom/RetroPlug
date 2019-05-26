cmake_minimum_required(VERSION 3.0.0)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)

# Create the executable target
add_executable(lsdsng-export main.cpp exporter.hpp exporter.cpp ../common/common.hpp ../common/common.cpp)
source_group(\\ FILES main.cpp exporter.hpp exporter.cpp ../common/common.hpp ../common/common.cpp)

target_compile_features(lsdsng-export PUBLIC cxx_std_14)
target_include_directories(lsdsng-export PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(lsdsng-export liblsdj ${Boost_LIBRARIES})

install(TARGETS lsdsng-export DESTINATION bin)
