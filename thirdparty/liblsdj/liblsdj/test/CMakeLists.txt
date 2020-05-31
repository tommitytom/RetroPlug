cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

set(SOURCES
	file.cpp
	file.hpp
    format.cpp
	main.cpp
	project.cpp
	sav.cpp
	song.cpp
	vio.cpp
	)

add_executable(test ${SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_compile_features(test PUBLIC cxx_std_17)
target_compile_definitions(test PRIVATE RESOURCES_FOLDER="${CMAKE_SOURCE_DIR}/resources/")

target_link_libraries(test
	PRIVATE
	Catch2::Catch2
	liblsdj
	)

# include(CTest)
# include(Catch)
# catch_discover_tests(test)
