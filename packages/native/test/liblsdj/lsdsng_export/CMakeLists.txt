cmake_minimum_required(VERSION 3.0.0)

# Create the executable target
add_executable(lsdsng-export main.cpp exporter.hpp exporter.cpp ../common/common.hpp ../common/common.cpp)
source_group(\\ FILES main.cpp exporter.hpp exporter.cpp ../common/common.hpp ../common/common.cpp)

target_compile_features(lsdsng-export PUBLIC cxx_std_14)
target_include_directories(lsdsng-export PUBLIC ${PROJECT_SOURCE_DIR}/dependency)
target_link_libraries(lsdsng-export liblsdj)

install(TARGETS lsdsng-export DESTINATION bin)
