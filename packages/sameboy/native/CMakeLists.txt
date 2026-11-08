# The SameBoy pack's native half (docs/modular-graph-phase-f2-design.md section 2): the Game Boy core, its config and
# boot ROMs, the Game Boy button table, its ROM gate, the build function rp.sameboy.gb's request carries
# (rp.host.systems/2), and the node type itself. Added by retroplug_package(sameboy) (cmake/packages.cmake). It builds on
# the system package's contract (SystemBase, the build request and function, the host's system services, the SystemNode
# template) and on the node SDK, which the template is written in; it links no shell target.
#
# Its include root is src/, and the include paths keep their text: "system/sameboy/SameBoySystem.hpp" is what the
# engine's tests and the node's own translation unit spell, as it was before the carve.

# --- retroplug-sameboy: the rp_sameboy_package entry and rp.sameboy.gb (src/system/sameboy/SameBoyNode.cpp), over
# SameBoySystem (the core), SameBoyBackend (the build function) and the boot ROMs sameboy_bootroms generates into the
# build tree (cmake/sameboy_bootroms.cmake, included by cmake/sameboy.cmake). The manifest entry is not `device`: a Game
# Boy builds on a desktop host's control thread, and nothing of this tree is a device build's.
if(WIN32)
    # SameBoySystem.cpp #defines GB_INTERNAL to reach SameBoy's internal struct view and GNU-extension headers, which MSVC
    # cl.exe cannot parse, so on Windows clang-cl (MSVC-ABI) compiles it into retroplug-samebsys and this library links
    # that. Elsewhere the source compiles in place. The one RetroPlug C++ that needs the internal view (verified with
    # rpc-schema-dump -k 0); everything else compiles under cl.exe because GB_INTERNAL is PRIVATE to the sameboy target.
    find_program(RP_CLANG_CL clang-cl
        HINTS "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin"
              "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        REQUIRED)
    set(_sb_src "${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp")
    set(_sb_obj "${CMAKE_BINARY_DIR}/sameboy-clang/SameBoySystem.cpp.obj")
    # A custom command gets no header scan, so it rebuilds on the headers listed here and nothing else. A stale object is
    # not a slow build but a crash: change a class layout or a vtable (SystemBase's virtuals) and the MSVC objects agree on
    # the new one while this one keeps the old, every slot after the change off by one. These are the trees its includes
    # reach: the console's own, the contract it implements (system/, transport/, util/), the node SDK's util/
    # (util/FixedQueue.hpp) and sameboy's Core headers; the boot-ROM headers sameboy_bootroms generates come with
    # DEPENDS below, which orders them before this command but does not list them as inputs.
    file(GLOB _sb_headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/system/native/src/system/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/system/native/src/transport/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/system/native/src/util/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/util/*.hpp"
        "${CMAKE_SOURCE_DIR}/deps/sameboy/Core/*.h")
    add_custom_command(
        OUTPUT "${_sb_obj}"
        COMMAND "${RP_CLANG_CL}" /nologo /c /MT /O2 /Ob2 /DNDEBUG /EHsc /std:c++20 /Zc:__cplusplus
            "/FI${CMAKE_BINARY_DIR}/sameboy-clang/sameboy_version.h"
            # GB_DISABLE_* MUST match the sameboy core or GB_gameboy_t's layout diverges.
            -DGB_DISABLE_TIMEKEEPING -DGB_DISABLE_DEBUGGER
            -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DNOGDI -D_CRT_SECURE_NO_WARNINGS
            "-I${CMAKE_CURRENT_SOURCE_DIR}/src"                    # the console: system/sameboy/
            "-I${CMAKE_SOURCE_DIR}/packages/system/native/src"     # the contract: system/, transport/, util/
            "-I${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src"   # util/FixedQueue.hpp
            "-I${CMAKE_SOURCE_DIR}/deps/sameboy/Core"
            "-I${CMAKE_BINARY_DIR}/generated"
            "-I${CMAKE_BINARY_DIR}/_deps/reflectcpp-src/include"
            "/Fo${_sb_obj}" "${_sb_src}"
        DEPENDS "${_sb_src}" ${_sb_headers} sameboy_bootroms
        COMMENT "clang-cl (sameboy) SameBoySystem.cpp"
        VERBATIM)
    add_library(retroplug-samebsys STATIC "${_sb_obj}")
    set_target_properties(retroplug-samebsys PROPERTIES LINKER_LANGUAGE CXX)
    set(_sb_sources "")
else()
    list(APPEND _sb_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp")
endif()

retroplug_package_library(retroplug-sameboy
    ${_sb_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/SameBoyBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/SameBoyNode.cpp)
target_include_directories(retroplug-sameboy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-sameboy
    PUBLIC  retroplug-system     # SystemBase, the build request and function, the SystemNode template's contract
            retroplug-node-sdk   # graph/node.hpp, the authoring layer SystemNode instantiates
            sameboy              # deps/sameboy: the emulator, and the boot ROM headers sameboy_bootroms generates
    )
# On Windows the core's source is the clang-cl object above; a PUBLIC dependency, so nothing else has to remember it.
if(WIN32)
    target_link_libraries(retroplug-sameboy PUBLIC retroplug-samebsys)
endif()

# --- the pack's test binary (cmake/test-binary.cmake), built by name; sources resolve against this directory.

# Guards the Game Boy's audio path: the four per-channel taps and the mix they re-sum to, the serial port's capture
# timing against the host's MIDI-in, and every SameBoyModel (SGB variants included) booting and rendering. No Engine:
# each case builds the core the build function would (docs/modular-graph-phase-f2-design.md section 8), so this binary
# links no shell target. Run by `pnpm test:plugin`.
retroplug_add_test_binary(
    NAME     retroplug-sameboy-test
    SOURCES  test/SameBoyStems.test.cpp
             test/SameBoySerialTiming.test.cpp   # host MIDI-in keeps its intra-block sample offset over serial
             test/SameBoyModels.test.cpp         # every SameBoyModel boots + renders
    LIBS     retroplug-sameboy
    DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/packages/mgb/rom/mGB.gb")
