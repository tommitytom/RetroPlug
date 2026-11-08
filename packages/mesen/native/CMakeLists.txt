# The Mesen pack's native half (docs/modular-graph-phase-f2-design.md section 2): the NES, GBA and SMS/GG cores over the
# vendored `mesen` library, their configs, the global init and the NES debug session, the video and audio devices, the
# shared block tail, the emulated N8 FIFO and its reliable link role, the SMS sync role, the consoles' button tables and
# ROM gates, the three build functions their nodes' requests carry (rp.host.systems/2), and the three node types. Added by
# retroplug_package(mesen) (cmake/packages.cmake).
#
# It builds on the system package's contract and on the node SDK, and declares the N8 pack: NesN8FifoRole speaks
# BlipToaster's reliable link (transport/N8ReliableLink.hpp) from the cart's side, and rp.mesen.nes's expansion-volume
# bounds are the pack's (n8/N8Node.hpp), so the emulated chip and the real cart's register are set from one range. The
# edge runs one way: n8 imports neither this package nor the contract, and its TS names no console.
#
# Its include root is src/, and the include paths keep their text: "system/mesen/MesenNesSystem.hpp" and
# "debug/Cc65DbgParser.hpp" are what the sources and the engine's tests spell.

# --- retroplug-mesen: the rp_mesen_package entry and rp.mesen.nes / .gba / .sms
# (src/system/mesen/MesenNode.cpp), over the three systems and their build functions (src/system/mesen/MesenBackend.cpp).
retroplug_package_library(retroplug-mesen
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNesSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenGlobalInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNesDebugSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenVideoDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenGbaSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenSmsSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/roles/NesN8FifoRole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/roles/SmsSyncRole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNode.cpp)
target_include_directories(retroplug-mesen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-mesen
    PUBLIC  retroplug-system    # SystemBase, the capability interfaces, the build request, the host's system services
            retroplug-node-sdk  # graph/node.hpp, the authoring layer SystemNode instantiates
            retroplug-n8        # transport/N8ReliableLink.hpp (NesN8FifoRole), n8/N8Node.hpp's expansion-volume range
            mesen)              # deps/mesen: the emulator, and the headers MesenBlockOps and MesenBoot include

# --- the pack's test binary (cmake/test-binary.cmake), built by name; sources resolve against this directory.

# Guards what the three consoles compute without an Engine: the NES APU's flush window, the per-channel taps and the
# stereo-mod pins re-summing to the mix, the emulated N8 FIFO (its byte scheduling, the Edio status protocol, and the
# reliable-link role the N8 pack's sender talks to), the NES and SMS teardown survival, the SMS/GG boot, visible geometry
# and PSG, the GBA's firmware choice and block triad, the shared drain tail, and Mesen's own shared pieces this fork
# patches for the block thread. Every case builds its core (or its role) directly, so this binary links no shell target;
# the sender's half of the reliable link is the N8 pack's own test binary's. Run by `pnpm test:plugin`.
retroplug_add_test_binary(
    NAME     retroplug-mesen-test
    SOURCES  test/NesApuLatency.test.cpp         # runtime NES APU flush window (live apuLatencyMs knob)
             test/NesN8FifoTiming.test.cpp       # host MIDI-in keeps its intra-block offset into the N8 FIFO
             test/NesEverdriveFifo.test.cpp      # Edio status protocol leaves no stale byte to desync MIDI parsing
             test/NesStems.test.cpp              # NES stereo-mod pins re-sum to the mix (spec/10 step 5)
             test/NesTeardown.test.cpp           # the NES twin of SMS's construct/destruct guard
             test/SmsAudio.test.cpp              # SMS/GG boot, visible geometry, non-silent PSG, teardown survival
             test/GbaFirmware.test.cpp           # each GBA system loads ITS bios, not whichever landed last
             test/GbaAudio.test.cpp              # the GBA block triad terminates and fills a block
             test/MesenBlockOps.test.cpp         # the shared drain tail blanks a short ring
             test/MesenShared.test.cpp           # ClearState's empty state, and the notification dispatch's copy
             test/N8ReliableLink.test.cpp        # the emulated cart FIFO's side of the reliable link
    LIBS     retroplug-mesen                    # the consoles, their roles, and mesen's own headers
             retroplug-testing-allocator        # CountingAllocator, and LinkRomModel.hpp through it: the ROM's link rules
    DEFS     RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")
