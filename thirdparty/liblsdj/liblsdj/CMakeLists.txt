cmake_minimum_required(VERSION 3.0.0)

if (APPLE)
  # Add global definitions and include directories
  add_definitions(-Wall -Werror -Wconversion -Wno-unused-variable)
endif (APPLE)

set(PUBLIC_HEADERS
	include/lsdj/allocator.h
	include/lsdj/chain.h
	include/lsdj/channel.h
	include/lsdj/command.h
	include/lsdj/compression.h
	include/lsdj/error.h
	include/lsdj/instrument.h
	include/lsdj/panning.h
	include/lsdj/phrase.h
	include/lsdj/project.h
	include/lsdj/sav.h
	include/lsdj/song.h
	include/lsdj/synth.h
	include/lsdj/table.h
	include/lsdj/version.h
	include/lsdj/wave.h
	include/lsdj/vio.h
	)

set(SOURCES
	src/allocator.c
	src/bytes.c
	src/bytes.h
	src/compression.c
	src/chain.c
	src/defaults.h
	src/error.c
	src/groove.c
	src/instrument.c
	src/instrument_kit.c
	src/instrument_noise.c
	src/instrument_pulse.c
	src/instrument_wave.c
	src/phrase.c
	src/project.c
	src/sav.c
	src/song_offsets.h
	src/song.c
	src/speech.c
	src/synth.c
	src/table.c
	src/vio.c
	src/wave.c
	)

# Create the library target
add_library(liblsdj STATIC ${PUBLIC_HEADERS} ${SOURCES})
set_target_properties(liblsdj PROPERTIES OUTPUT_NAME lsdj)
source_group(\\ FILES ${PUBLIC_HEADERS} ${SOURCES})
target_include_directories(liblsdj
	INTERFACE "include"
	PRIVATE "include/lsdj"
	)

install(TARGETS liblsdj DESTINATION lib)
install(FILES ${PUBLIC_HEADERS} DESTINATION include/lsdj)

add_subdirectory(test)