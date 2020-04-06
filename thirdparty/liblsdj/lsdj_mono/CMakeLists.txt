cmake_minimum_required(VERSION 3.0.0)

set(SOURCES
	../common/common.hpp
	../common/common.cpp
	../common/song_processor.hpp
	../common/song_processor.cpp
	main.cpp
	mono_processor.hpp
	mono_processor.cpp)

# Create the executable target
add_executable(lsdj-mono ${SOURCES})
source_group(\\ FILES ${SOURCES})

target_compile_features(lsdj-mono PUBLIC cxx_std_14)
target_include_directories(lsdj-mono PUBLIC ${PROJECT_SOURCE_DIR}/dependency)
target_link_libraries(lsdj-mono liblsdj)

install(TARGETS lsdj-mono DESTINATION bin)
