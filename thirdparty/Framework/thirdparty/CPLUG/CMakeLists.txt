# This file is for building the example project ONLY. It's not meant to be a module included as a subdirectory.
# If you're interested using CMake to build your plugins, read the code here, then copy/paste and modify it in
# your own projects. It's easy!
cmake_minimum_required(VERSION 3.7)
project(cplug_example VERSION 1.0.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

if (APPLE)
    enable_language(OBJC)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

# settings used across all formats
include_directories(src)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_STANDARD 20) # required for c++ clap build
    add_compile_options(
        /FI ${CMAKE_SOURCE_DIR}/example/config.h
    )
else()
    add_compile_options(
        -Werror=return-type
        -Werror=shadow
        -Wunused-function

        -Wno-deprecated
        -Wno-multichar
        -Wno-nullability-completeness
        -Wno-writable-strings

        -include${CMAKE_SOURCE_DIR}/example/config.h
    )
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-Werror=excess-initializers)
    endif()
endif()

# ██╗   ██╗███████╗████████╗██████╗ 
# ██║   ██║██╔════╝╚══██╔══╝╚════██╗
# ██║   ██║███████╗   ██║    █████╔╝
# ╚██╗ ██╔╝╚════██║   ██║    ╚═══██╗
#  ╚████╔╝ ███████║   ██║   ██████╔╝
#   ╚═══╝  ╚══════╝   ╚═╝   ╚═════╝ 

if (WIN32)
    add_library(cplug_example_vst3 MODULE
        example/example.c
        src/cplug_vst3.c
    )
    # According to the docs you're meant to bundle your Windows VST3 using a bundle like folder structure:
    # https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Format.html#for-the-windows-platform
    # However, placing the unbundled binary directly into your ...\Common Files\VST3 folder will work fine in every host.
    # I'm not sure anyone in the real world actually bundles Windows VST3s like this, but maybe I'm just ignorant.
    set_target_properties(cplug_example_vst3 PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION vst3
        OUTPUT_NAME cplug_example                                             # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.vst3 # out directory name, differs from target name
        SUFFIX .vst3
        PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.vst3
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.vst3/Contents/x86_64-win
    )
elseif (APPLE)
    add_library(cplug_example_vst3 MODULE
        example/example.m
        src/cplug_vst3.c
    )
    target_link_libraries(cplug_example_vst3 PRIVATE "-framework Cocoa")
    set_target_properties(cplug_example_vst3 PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION vst3
        OUTPUT_NAME cplug_example                                             # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.vst3 # out directory name, differs from target name
        MACOSX_BUNDLE_BUNDLE_NAME "CPLUG Example"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER com.cplug.example.vst3
        MACOSX_BUNDLE_COPYRIGHT CPLUG
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/example/vst3.plist.in
    )
    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/cplug_example.vst3/Contents/PkgInfo)
    file(WRITE ${CMAKE_BINARY_DIR}/cplug_example.vst3/Contents/PkgInfo "BNDL????")
    add_custom_command(TARGET cplug_example_vst3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/cplug_example.vst3 to ~/Library/Audio/Plug-Ins/VST3/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/cplug_example.vst3" "~/Library/Audio/Plug-Ins/VST3/cplug_example.vst3"
        )
endif()

#  █████╗ ██╗   ██╗██╗   ██╗██████╗ 
# ██╔══██╗██║   ██║██║   ██║╚════██╗
# ███████║██║   ██║██║   ██║ █████╔╝
# ██╔══██║██║   ██║╚██╗ ██╔╝██╔═══╝ 
# ██║  ██║╚██████╔╝ ╚████╔╝ ███████╗
# ╚═╝  ╚═╝ ╚═════╝   ╚═══╝  ╚══════╝

# Read the comments here carefully. Building Audio Units v2 through CMake is not as easy as VST3 or CLAP

if (APPLE)

add_library(cplug_example_auv2 MODULE
    example/example.m
    src/cplug_auv2.c
)
target_link_libraries(cplug_example_auv2 PRIVATE "-framework AudioToolbox -framework Cocoa") # -framework AudioToolbox not actually required...

# The following properties will end up in our bundles .plist, but must be set here.
# CMake doesn't support passing them as arguments in 'set_target_properties' below
# Some of these properties are used by both the .plist, and inside the AU when the host requests a 'ClassInfo' dict
# I would love to only define these in example/config.h, but then I would be duplicating these properties.
# Instead, I configure all AU settings from a single place
# Full list of AU types & tags here:
# https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html
# The properties:
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.cplug.example.auv2")
set(MACOSX_BUNDLE_DESCRIPTION "CPLUG Example")
set(MACOSX_BUNDLE_TAGS "<string>Synthesizer</string>")
set(MACOSX_BUNDLE_TYPE "aumu")
set(MACOSX_BUNDLE_SUBTYPE "xmpl")
set(MACOSX_BUNDLE_MANUFACTURER "CPLG")
# Convert version string (1.0.0) > integer (65536)
math(EXPR MACOSX_BUNDLE_VERSION_INT "${PROJECT_VERSION_MAJOR} << 16 | ${PROJECT_VERSION_MINOR} << 8 | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT DECIMAL) # cool trick bro

set_target_properties(cplug_example_auv2 PROPERTIES
    BUNDLE True
    BUNDLE_EXTENSION component
    OUTPUT_NAME "cplug_example"                                                # out binary name, differs from target name
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.component # out directory name, differs from target name
    MACOSX_BUNDLE_BUNDLE_NAME "CPLUG Example"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT CPLUG
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/example/auv2.plist.in
    )
file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/cplug_example.component/Contents/PkgInfo)
file(WRITE ${CMAKE_BINARY_DIR}/cplug_example.component/Contents/PkgInfo "BNDL????")

target_compile_definitions(cplug_example_auv2 PRIVATE
    CPLUG_AUV2_VERSION_INT=${MACOSX_BUNDLE_VERSION_INT}
    CPLUG_AUV2_BUNDLE_ID="${MACOSX_BUNDLE_GUI_IDENTIFIER}"
    CPLUG_BUILD_AUV2
)

# For some reason Audio Units don't work unless they're inside their expcted folder.
# If I can find a way to remove this silly dependancy I will
# Until then, better to copy test plugins to the users directory
add_custom_command(TARGET cplug_example_auv2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/cplug_example.component to ~/Library/Audio/Plug-Ins/Components/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/cplug_example.component" "~/Library/Audio/Plug-Ins/Components/cplug_example.component"
    )

endif() # APPLE

#  ██████╗██╗      █████╗ ██████╗
# ██╔════╝██║     ██╔══██╗██╔══██╗
# ██║     ██║     ███████║██████╔╝
# ██║     ██║     ██╔══██║██╔═══╝
# ╚██████╗███████╗██║  ██║██║
#  ╚═════╝╚══════╝╚═╝  ╚═╝╚═╝

if (APPLE)
    add_library(cplug_example_clap MODULE example/example.m src/cplug_clap.c)
    target_link_libraries(cplug_example_clap PRIVATE "-framework Cocoa")
    set_target_properties(cplug_example_clap PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION clap
        OUTPUT_NAME "cplug_example"                                           # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.clap # out directory name, differs from target name
        MACOSX_BUNDLE_GUI_IDENTIFIER com.cplug.example.clap
        MACOSX_BUNDLE_BUNDLE_NAME "CPLUG Example"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT CPLUG
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/example/clap.plist.in
        )
    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/cplug_example.clap/Contents/PkgInfo)
    file(WRITE ${CMAKE_BINARY_DIR}/cplug_example.clap/Contents/PkgInfo "BNDL????")

    add_custom_command(TARGET cplug_example_clap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/cplug_example.clap to ~/Library/Audio/Plug-Ins/CLAP/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/cplug_example.clap" "~/Library/Audio/Plug-Ins/CLAP/cplug_example.clap"
        )
elseif(WIN32)
    add_library(cplug_example_clap MODULE example/example.c src/cplug_clap.c)
    set_target_properties(cplug_example_clap PROPERTIES
        OUTPUT_NAME cplug_example
        SUFFIX .clap
        PDB_NAME cplug_example_clap
        )
endif()

# ███████╗████████╗ █████╗ ███╗   ██╗██████╗  █████╗ ██╗      ██████╗ ███╗   ██╗███████╗
# ██╔════╝╚══██╔══╝██╔══██╗████╗  ██║██╔══██╗██╔══██╗██║     ██╔═══██╗████╗  ██║██╔════╝
# ███████╗   ██║   ███████║██╔██╗ ██║██║  ██║███████║██║     ██║   ██║██╔██╗ ██║█████╗  
# ╚════██║   ██║   ██╔══██║██║╚██╗██║██║  ██║██╔══██║██║     ██║   ██║██║╚██╗██║██╔══╝  
# ███████║   ██║   ██║  ██║██║ ╚████║██████╔╝██║  ██║███████╗╚██████╔╝██║ ╚████║███████╗
# ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝╚══════╝

set(HOTRELOAD_LIB_NAME cplug_example_hotreload)

if (WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_library(${HOTRELOAD_LIB_NAME} MODULE example/example.c)
    add_executable(cplug_example_standalone WIN32 src/cplug_standalone_win.c)

    # Windows paths are complicated
    set(HOTRELOAD_WATCH_DIR_POSIX "${PROJECT_SOURCE_DIR}/example")
    set(HOTRELOAD_LIB_PATH_POSIX "${CMAKE_BINARY_DIR}/${HOTRELOAD_LIB_NAME}.dll")
    string(REPLACE "/" "\\\\" HOTRELOAD_WATCH_DIR "${HOTRELOAD_WATCH_DIR_POSIX}")
    string(REPLACE "/" "\\\\" HOTRELOAD_LIB_PATH "${HOTRELOAD_LIB_PATH_POSIX}")
    
    target_compile_definitions(${HOTRELOAD_LIB_NAME} PRIVATE -DCPLUG_SHARED)

    target_compile_definitions(cplug_example_standalone PRIVATE
        -DHOTRELOAD_WATCH_DIR="${HOTRELOAD_WATCH_DIR}"
        -DHOTRELOAD_LIB_PATH="${HOTRELOAD_LIB_PATH}"
        -DHOTRELOAD_BUILD_COMMAND="cmake --build ${CMAKE_BINARY_DIR} --config Debug --target ${HOTRELOAD_LIB_NAME}"
        -DCPLUG_SHARED
        )
    add_dependencies(cplug_example_standalone ${HOTRELOAD_LIB_NAME})
elseif (APPLE)
    add_library(${HOTRELOAD_LIB_NAME} MODULE example/example.m)
    target_link_libraries(${HOTRELOAD_LIB_NAME} PRIVATE "-framework Cocoa")

    add_executable(cplug_example_app MACOSX_BUNDLE src/cplug_standalone_osx.m)

    set_target_properties(cplug_example_app PROPERTIES
        BUNDLE True
        OUTPUT_NAME "cplug_example"                                          # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cplug_example.app # out directory name, differs from target name
        MACOSX_BUNDLE_GUI_IDENTIFIER com.cplug.${PROJECT_NAME}.app
        MACOSX_BUNDLE_BUNDLE_NAME "CPLUG Example"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT CPLUG
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/example/app.plist.in
    )
    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/cplug_example.app/Contents/PkgInfo)
    file(WRITE ${CMAKE_BINARY_DIR}/cplug_example.app/Contents/PkgInfo "APPL????")
    target_link_libraries(cplug_example_app PRIVATE "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework CoreServices")

    target_compile_definitions(cplug_example_app PRIVATE
        -DHOTRELOAD_WATCH_DIR="${PROJECT_SOURCE_DIR}/example"
        -DHOTRELOAD_LIB_PATH="${CMAKE_BINARY_DIR}/lib${HOTRELOAD_LIB_NAME}.so"
        -DHOTRELOAD_BUILD_COMMAND="cmake --build ${CMAKE_BINARY_DIR} --config Debug --target ${HOTRELOAD_LIB_NAME}"
        -DCPLUG_SHARED
        )
    add_dependencies(cplug_example_app ${HOTRELOAD_LIB_NAME})
endif()

# ████████╗███████╗███████╗████████╗
# ╚══██╔══╝██╔════╝██╔════╝╚══██╔══╝
#    ██║   █████╗  ███████╗   ██║   
#    ██║   ██╔══╝  ╚════██║   ██║   
#    ██║   ███████╗███████║   ██║   
#    ╚═╝   ╚══════╝╚══════╝   ╚═╝   

if (APPLE)
    add_executable(test_compile_objcpp test_compile.mm)
    target_compile_definitions(test_compile_objcpp PRIVATE CPLUG_BUILD_AUV2=1)
    target_link_libraries(test_compile_objcpp PRIVATE "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework AudioToolbox")
else()
    add_executable(test_compile_cpp WIN32 test_compile.cpp)
endif()