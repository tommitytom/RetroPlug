#pragma once

#include <cstdint>

// What a system node's inputs can be handed in one block. A wire carries its source's queue and an input declares none
// (spec/02-native-host.md, "Host I/O, the system nodes' inputs and queue capacities"), so the bound is the capacity of
// the outputs that feed a system: a script node's serialIn and midi outputs hold 4096 messages and 72 KiB, its bytes
// output 256 messages and 1 KiB, its buttons output 64 messages of two bytes each, and rp.host.midi-in's output (which
// reaches a system through a routing node) holds less than the midi one. A Game Boy's serialOut output, fixed at 8256
// messages and 8256 bytes, can wire into any system's serialIn or bytes input too. The script numbers are the ones
// packages/retroplug/src/script/nodes/ports.ts declares, and none of them depend on the block size.
//
// A core sizes its ingress queues from them when it is built, so a block's input grows nothing on the block thread
// (docs/modular-graph-phase-c4-design.md section 11).
namespace rp {

struct InputCapacity {
    std::uint32_t messages;
    std::uint32_t bytes;
};

inline constexpr InputCapacity kMidiInput{4096, 73728};
inline constexpr InputCapacity kSerialInInput{4096, 73728};
inline constexpr InputCapacity kBytesInput{256, 1024};
inline constexpr InputCapacity kSerialOutInput{8256, 8256};  // the Game Boy's serialOut (SystemNodes.cpp)
inline constexpr std::uint32_t kButtonsInput = 64;

// Presses reach a core from two doors before the same prepareForBlock clears its queue: a block's buttons input, and
// Engine::applyCommand(PressButton) on GraphHost's command ring (GraphHost::commandCapacity()). A press queue is sized
// for both, so a host that queues a burst between two blocks does not lose the tail.
inline constexpr std::uint32_t kCommandRingCapacity = 256;
inline constexpr std::uint32_t kPressInput = kButtonsInput + kCommandRingCapacity;

// A Mesen core's press queue holds one transition a button: it applies the whole queue before the emulator polls its
// controller, so only a button's last transition before the poll is observable and a transition for a button already
// queued replaces it. Its bound is then the button range, not what the doors deliver, and kPressInput covers every
// value a button byte can hold, so no burst fills it and a release is never refused behind one. The coalescing keys on
// that byte, so a console's map names at most one button per value - and a value it has no button for names none, which
// keeps every entry that does name a button the only one driving it.
static_assert(kPressInput >= 256, "a Mesen press queue must cover every button value");

}  // namespace rp
