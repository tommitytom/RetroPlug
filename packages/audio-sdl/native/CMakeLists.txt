# The SDL2 backend (docs/modular-graph-phase-f5b-design.md section 5.3): SDL_InitSubSystem(SDL_INIT_AUDIO) at the
# backend's first use (the app's setupSdl initialises video and the game controller only), float32 output with a pull
# callback, 2, 4, 6 or 8 channels, the frequency allowed to change and adopted, SDL's audio drivers as drivers with the
# dummy and disk test drivers hidden, and a device list that is real only for the driver currently initialised (SDL runs
# one audio driver at a time). Added by retroplug_package(audio-sdl); its manifest item is what a product names in
# AUDIO_BACKENDS.
if(RP_DEVICE_BUILD)
    return()
endif()

retroplug_package_library(retroplug-audio-sdl ${CMAKE_CURRENT_SOURCE_DIR}/src/SdlAudioBackend.cpp)
target_include_directories(retroplug-audio-sdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# PkgConfig::SDL2 is the root's (CMakeLists.txt), defined on every platform before the packages are added.
target_link_libraries(retroplug-audio-sdl PUBLIC retroplug-audio PRIVATE PkgConfig::SDL2)
