# The PortAudio backend (docs/modular-graph-phase-f5b-design.md section 5.3): today's PortAudio code out of the SDL
# app's main.cpp - the host-API and device lookup, the low-latency request, the obtained rate and the wide-output
# warning, the ALSA/PipeWire/JACK log quietening run before Pa_Initialize at the backend's first use - behind the audio
# package's interface. Added by retroplug_package(audio-portaudio); its manifest item is what a product names in
# AUDIO_BACKENDS, so the CLI, the headless host and the plugin link neither PortAudio nor this.
#
# `portaudio` itself is added at ROOT scope (cmake/portaudio.cmake), before the packages: this target must find it.
if(RP_DEVICE_BUILD)
    return()
endif()

retroplug_package_library(retroplug-audio-portaudio ${CMAKE_CURRENT_SOURCE_DIR}/src/PortAudioBackend.cpp)
target_include_directories(retroplug-audio-portaudio
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_SOURCE_DIR}/deps/portaudio/include)  # a subdirectory build does not export it
target_link_libraries(retroplug-audio-portaudio PUBLIC retroplug-audio PRIVATE portaudio)

if(UNIX AND NOT APPLE)
    # The quietening calls snd_lib_error_set_handler directly, which libasound must therefore be linked for by name:
    # until now it arrived through rtmidi's static link, which is another package's business (report item 22).
    find_package(ALSA REQUIRED)
    target_link_libraries(retroplug-audio-portaudio PRIVATE ALSA::ALSA)
    target_link_libraries(retroplug-audio-portaudio PRIVATE ${CMAKE_DL_LIBS})  # the libjack dlopen
endif()
