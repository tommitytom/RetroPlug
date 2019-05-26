cmake_minimum_required(VERSION 3.0.0)

if (APPLE)
  # Add global definitions and include directories
  add_definitions(-Wall -Werror -Wconversion -Wno-unused-variable)
endif (APPLE)

set(HEADERS chain.h channel.h command.h compression.h error.h groove.h instrument.h instrument_constants.h instrument_kit.h instrument_noise.h instrument_pulse.h instrument_wave.h panning.h phrase.h project.h row.h sav.h song.h synth.h table.h vio.h wave.h word.h)
set(SOURCES chain.c command.c compression.c error.c groove.c instrument.c phrase.c project.c row.c sav.c song.c synth.c table.c vio.c wave.c word.c)

# Create the library target
add_library(liblsdj STATIC ${HEADERS} ${SOURCES})
set_target_properties(liblsdj PROPERTIES OUTPUT_NAME lsdj)
source_group(\\ FILES ${HEADERS} ${SOURCES})

install(TARGETS liblsdj DESTINATION lib)
install(FILES chain.h channel.h command.h error.h groove.h instrument.h sav.h panning.h phrase.h project.h row.h song.h synth.h table.h wave.h word.h vio.h DESTINATION include/lsdj)
