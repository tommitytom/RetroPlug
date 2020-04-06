cmake_minimum_required(VERSION 3.0.0)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS program_options)

set(SOURCES
    ../common/common.hpp
    ../common/common.cpp
    ../common/song_processor.hpp
    ../common/song_processor.cpp
    clean_processor.hpp
    clean_processor.cpp
    main.cpp)

# Create the executable target
add_executable(lsdj-clean ${SOURCES})
source_group(\\ FILES ${SOURCES})

target_compile_features(lsdj-clean PUBLIC cxx_std_17)
target_include_directories(lsdj-clean PUBLIC ${Boost_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/dependency)
target_link_libraries(lsdj-clean liblsdj ${Boost_LIBRARIES})

install(TARGETS lsdj-clean DESTINATION bin)
