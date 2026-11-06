#pragma once

// The MicroQuickJS script host (docs/modular-graph-phase-e4-design.md section 4): a device script pack's nodes behind
// the node ABI (graph/rp_node.h), on the engine the box runs, built two ways from this one source, as graph/node.hpp
// is. The device's build (RP_DEVICE: no heap, no exceptions, no RTTI) is what a device runs a plan's script nodes with:
// each type has query_memory, create and destroy, and nothing that describes or re-plans. The desktop's adds what
// planning needs (the descriptor, layout_for, prepare_settings and its swap, rp.sdk.settings-json/1 for exportPlan), so
// a device pack runs on this engine on the desktop as well, and the desktop runs the source and the bytecode the box
// runs (decision 2). A desktop-only pack stays on the QuickJS script host (host/script/ScriptHost.hpp).
//
// A pack. Its bytecode (rp_mqjs_pack.h; the device-pack toolchain compiles it, once per width) defines its nodes with
// defineNode and hands them to registerScriptNodes (packages/retroplug/src/script/register.ts), which appends them to
// the global __rp_scriptNodes. package() makes one rp_package for it, one rp_node_type per definition, in the order the
// toolchain recorded them (the pack source's type ids), each type's ctx naming its definition. Registration runs no
// JavaScript on the device: the ids are the build's. The desktop's runs the bytecode once in a context of its own (the
// control context, below) to read each definition's descriptor, and refuses a pack whose definitions are not the ids
// the build recorded.
//
// Its context. A pack's nodes share one MicroQuickJS context, which lives in the pack's package memory
// (rp_package_memory: graph/PlanLoader.hpp places it in a device's arena, graph/PackageMemory.hpp on the desktop), one
// per host: the block is the pack's state, then the context's own copy of the bytecode, then the context's buffer, the
// manifest's heapBytes, which is the whole context (MicroQuickJS's JSContext, its class prototypes, its heap and its
// stack), as the toolchain holds the pack to it. So a pack's memory is heapBytes plus a fixed overhead, the state and
// the bytecode (packageBytes()). The device's build, the exporter's trial load and the toolchain's runner
// (exactPackage()) give the context heapBytes exactly; the desktop's hosts give it kDesktopHeapFactor times that, since
// a re-plan there holds two instances of a node for a while (below). A desktop pack whose heap ran out is set up again,
// a fresh context, once its last node is destroyed, so a re-plan without its nodes and then with them brings it back.
// The copy is the context's alone because running bytecode writes into it (rp_mqjs_pack.h says how), so two contexts
// never share one. Setting the package up makes the context, copies the bytecode, relocates the copy against the
// standard library, loads it and runs it, and checks its definitions against the build's ids; tearing it down frees the
// context, with every value in it.
//
// Its nodes. A node's create (control thread) makes only the native shim, holding its settings (the device's: the
// msgpack the blob carries, in the shim's own memory; the desktop's: its settings as the definition parsed them) and
// the pack's memory, which it asks its host for (rp.host.packages/1). The device's build runs no definition's
// parseSettings: the exporter wrote each node's settings as the desktop parsed them, every default filled in, and the
// device hands create those, decoded. Its JS instance is made at its first call on the
// thread that runs blocks (the ports_changed a plan's adoption sends, or its first process): the settings decoded into
// a JS object, the definition's create({settings, sampleRate, maxFrames}) and layout(settings) called, and the result
// kept in the pack's instance table, at most kMaxInstances a pack. So a pack's context is entered only by the thread
// that runs blocks, while the control thread plans: a destroy (control thread) only queues the instance's slot, which
// the block thread frees at its next entry into the pack. activate's and a settings swap's hooks wait for the node's
// next call there in the same way.
//
// The block API is defineNode.ts's Block, as RetroPlug's standard library's classes (script/rp_mqjs.h): messages(),
// send(), control(), emit(), frames and transport. Every object a block hands out is made at the node's first block and
// refilled after: each input port's pool is made to its queue's capacity before the node's JS first runs on it (a
// message object per place the queue holds, each with a Uint8Array of its own for each payload length up to
// kPlacedLengths over three bytes of a buffer of the port's, and every array messages() hands out), each control
// stream's to its capacity, so what a node's ports take of the heap is fixed by the plan it is in, and its first block,
// which the exporter's trial load runs, measures it. What a block's contents take is not: a payload longer than
// kPlacedLengths gets a Uint8Array made for its block, and a node may make things a message; the toolchain's heap
// check, which fills every input to its queue's capacity, SysEx included, on the node's default settings, is what holds
// a pack to a full block. (The box loads one plan and keeps it. A desktop host that re-plans makes a replacing
// instance's pools at its first block, while the instance it replaces waits for its plan to be reclaimed, so for that
// while the pack's heap holds both: hence kDesktopHeapFactor.) messages(port) is one of a set of arrays the port keeps,
// by the power of two that holds the count (its length set within half its capacity, so the engine neither grows nor
// shrinks it, on a rooted slot, so it is right if JS spliced the array meanwhile), over the pool, whose frame and flags
// are getters on the block's queue and whose data is the place's view of the payload's length, refilled. control(id) is
// the port's stream object, whose changes are kept as the messages are. A message's frame and flags, a stream's start
// and changes and a change's frame and value are read through the port its object names, checked against the running
// node's layout and block (integers the host keeps natively), so an object a pack kept past its block, of a node whose
// slot another now holds, never reads out of bounds: it throws a TypeError when the new node has no port of that
// ordinal, side and kind, and reads that port's when it has one. A message's data is its own value in the JS heap, what
// it last held, and is not checked. send copies the data into the output queue at the call, so a node may refill and
// send one array again; a frame earlier than the port's last is raised to it, and a negative one wraps through
// ToUint32, as the desktop's host does.
//
// What a warmed block allocates: nothing native, ever; in the JS heap, nothing at 64 bits for the whole API but a
// payload longer than kPlacedLengths (a SysEx), whose Uint8Array is made for its block. At 32 bits, the box's width,
// MicroQuickJS boxes every number that is not a 31-bit integer (it has short floats at 64 bits alone), so there each
// read of a non-integer number allocates its box: a control stream's start and a change's value, the transport's
// tempo, ppq and sampleRate when they are not whole, and any such number the node computes. A node that keeps to
// bytes, frames, flags and integers (a MIDI policy) allocates nothing at either width.
//
// Faults (design section 4). A throw from process (or from create, layout or a hook), a process that returns a promise
// or a thenable, a create that returns no process, and an entry that runs past its deadline each fault the node: its
// process answers RP_FAULT from then on, so the executor silences it and counts it once, and the node keeps the first
// fault's words (graph/rp_script.h's rp.script.fault/1). A node does not run again after a throw, unlike the desktop's
// QuickJS host: the box has no one to retry for. A heap that runs out (an out-of-memory error that reaches the host)
// kills the pack's context: every node of the pack faults at its next call, with the same words, and the context runs
// nothing more. MicroQuickJS throws out of memory as an ordinary InternalError, which a node may catch: one that does
// runs on in a heap with no room, and the host does not see it run out. Each entry into a context (a block, a hook, a
// settings check) is held to a deadline through the engine's interrupt handler, polled every 10000 instructions, over
// everything the entry runs: by the host's clock (rp.host.clock/1, the box's, at kHostClockDeadlineMicros unless it
// says otherwise), or, on the desktop when its host offers none, by the calling thread's CPU time at
// kDesktopDeadlineMicros, so a stall that is not the node's costs it nothing. On the device, a host that offers no
// clock runs its script nodes with no deadline.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"
#include "graph/rp_script.h"
#include "script/rp_mqjs_pack.h"

#ifndef RP_DEVICE
#include <string>
#endif


namespace rp::mqjs {

// How many instances one pack's context holds at once, all of its types together.
inline constexpr std::uint32_t kMaxInstances = 64;
// How many ports and params one node may have: a message's object names its port in 6 bits.
inline constexpr std::uint32_t kMaxPorts = 64;
// The payload lengths a message place keeps a Uint8Array for, from 1: every MIDI message but a SysEx.
inline constexpr std::uint32_t kPlacedLengths = 3;
// An entry's deadline by a host's clock that names none (the box's): 10 ms. A MIDI-rate node's process takes
// microseconds, the box's block is about a millisecond and its watchdog 3 s, so a node this far over is stuck, and the
// box's other nodes lose at most this long before it is silenced.
inline constexpr std::uint32_t kHostClockDeadlineMicros = 10000;
// An entry's deadline by the desktop's own clock, the thread's CPU time: 500 ms. Windows counts that time in scheduler
// ticks of about 15.6 ms, so a deadline must be many ticks long to be measured at all; a Debug or sanitizer build's
// first entry (the settings decoded, create and layout) takes tens of milliseconds; and a desktop host's audio thread
// can lose half a second to a node that hangs where the box could not.
inline constexpr std::uint32_t kDesktopDeadlineMicros = RP_SCRIPT_DESKTOP_DEADLINE_MICROS;
// How many times its manifest's heapBytes a desktop host gives a pack's context: a re-plan holds the instance it
// replaces beside the new one until its plan is reclaimed, both with their pools. The device's build, the exporter's
// trial load and the toolchain's runner give heapBytes exactly, which is the box's figure and its check.
inline constexpr std::uint32_t kDesktopHeapFactor = 2;
// The least context the host makes, at this build's width: below it MicroQuickJS's JS_NewContext sets the standard
// library up past its buffer and through the allocation that failed, rather than throwing (its assertion, 1 KiB, is no
// working floor). Measured over RetroPlug's standard library: at 64 bits a context of 4096 bytes sets itself up and one
// of 4088 crashes (ScriptPack.test.cpp holds the figure); at 32 bits, on i386 (the box's word width), 2552 and 2544.
// On ARM's EABI a uint64_t aligns to 8 in a struct, where i386 aligns it to 4, and MicroQuickJS's JSContext holds one,
// so the M33's floor may be a few bytes higher than the 32-bit figure here: the device check settles it on the box.
#if INTPTR_MAX >= INT64_MAX
inline constexpr std::uint32_t kLeastContextBytes = 4096;
#else
inline constexpr std::uint32_t kLeastContextBytes = 2560;
#endif

// The build's rp_package for a pack, made the first time it is asked for and the same after: its context heapBytes on
// the device's build, kDesktopHeapFactor times that on the desktop's. Null when the pack's name
// is not one a manifest's id holds, the build holds no more packs or types (RP_MQJS_MAX_PACKS, RP_MQJS_MAX_TYPES), or
// (on the desktop) the bytecode does not define the types it was built with; packageRefusal() then says why, and the
// desktop's build says it on stderr too. The source must outlive every use of the package.
const rp_package* package(const rp_mqjs_pack_source& source);
#ifndef RP_DEVICE
// The desktop build's rp_package for a pack at heapBytes exactly, as the box runs it: the toolchain's runner, which
// holds a pack to its manifest. A registration of its own, beside package()'s.
const rp_package* exactPackage(const rp_mqjs_pack_source& source);
#endif
// Why the last package() or exactPackage() answered null (the registration's own thread), or empty.
const char* packageRefusal();

// A pack's package memory as the box gives it: its context (the manifest's heapBytes), the host's state and the
// context's copy of the bytecode, at this width.
std::size_t packageBytes(const rp_mqjs_pack_source& source);


#ifndef RP_DEVICE
// The desktop's tools (the device-pack toolchain's runner, retroplug-mqjs-run): `bytecode` (mqjs -o's output, which
// this relocates where it lies, and which the run writes into) run in a context of `contextBytes` over RetroPlug's
// standard library, its console.log written to `log`. Empty when it ran to its end; else what stopped it (its
// exception, or why it could not start).
std::string runScript(std::uint8_t* bytecode, std::size_t size, std::size_t contextBytes,
                      void (*log)(void* ctx, const char* text, std::size_t size), void* logCtx);
#endif

}  // namespace rp::mqjs
