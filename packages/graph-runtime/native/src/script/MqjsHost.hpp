#pragma once

// The MicroQuickJS script host (docs/modular-graph-phase-e4-design.md section 4): a device script pack's nodes behind
// the node ABI (graph/rp_node.h), on the engine the box runs, built two ways from this one source, as graph/node.hpp
// is. The device's build (RP_DEVICE: no heap, no exceptions, no RTTI) is what a device runs a plan's script nodes with:
// each type has query_memory, create and destroy, and nothing that describes or re-plans. The desktop's adds what
// planning needs (the descriptor, layout_for, prepare_settings and its swap, rp.sdk.settings-json/1 for exportPlan), so
// a device pack runs on this engine on the desktop as well, and the desktop runs the source and the bundle the box
// runs, at its own width (decision 2). A desktop-only pack stays on the QuickJS script host
// (host/script/ScriptHost.hpp).
//
// A pack. Its bytecode (rp_mqjs_pack.h; the device-pack toolchain compiles it, once per width) defines its nodes with
// defineNode and hands them to registerScriptNodes (packages/retroplug/src/script/register.ts), which appends them to
// the global __rp_scriptNodes. It comes as two bundles: the device's runtime of the pack, which every node context on
// every host runs (the source's `bytecode`), and the whole pack (`control_bytecode`, the desktop's alone), which the
// desktop's control context runs. package() makes one rp_package for a pack, one rp_node_type per definition, in the
// order the toolchain recorded them (the pack source's type ids), each type's ctx naming its definition. Registration
// runs no JavaScript on the device: the ids are the build's. The desktop's runs the whole pack once in a context of its
// own (the control context, below) to read each definition's descriptor, and refuses a pack whose definitions are not
// the ids the build recorded; every settings check and layout_for runs there too.
//
// Its context. A pack's nodes share one MicroQuickJS context, which lives in the pack's package memory
// (rp_package_memory: graph/PlanLoader.hpp places it in a device's arena, graph/PackageMemory.hpp on the desktop), one
// per host: the block is the pack's state, then the context's own copy of the runtime's bytecode, then the context's
// buffer, the whole context (MicroQuickJS's JSContext, its class prototypes, its heap and its stack). The manifest's
// heapBytes is that context as the box runs it, at 32 bits: the box's figure, which its build gives exactly, and which
// only the box measures (its boot line, the device check). MicroQuickJS's heap is made of words, so the same objects
// take about twice the bytes at 64 bits (kWidthFactor), less where a payload's bytes are much of the heap; a 64-bit
// device build (the exporter's trial load, the device-mode tests) gives the context what the toolchain's check gives
// it, kDeviceHalves halves of heapBytes (kCheckHalves: 3/2, a heuristic, below), so neither is laxer than the other.
// The desktop's hosts give it desktopContextBytes(): at least kDesktopLeastContextBytes, since their queues are larger
// than the box's and a re-plan there holds two instances of a node for a while (below). So a pack's memory is its
// context plus a fixed overhead, the state and the bytecode (packageBytes() for a device build of this width,
// boxPackageBytes() for the box's). A desktop pack whose heap ran out is set up again, a fresh context, once its last
// node is destroyed, so a re-plan without its nodes and then with them brings it back. The copy is the context's alone
// because running bytecode writes into it (rp_mqjs_pack.h says how), so two contexts never share one. Setting the
// package up makes the context, copies the bytecode, relocates the copy against the standard library, loads it and
// runs it, and checks its definitions against the build's ids; tearing it down frees the context, with every value in
// it.
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
// send(), forward(), control(), emit(), frames and transport. Every object a block hands out is made at the node's
// first block and refilled after: each input port's pool is made to its queue's capacity before the node's JS first
// runs on it (a message object per place the queue holds, each with a Uint8Array of its own for each payload length up
// to kPlacedLengths over three bytes of a buffer of the port's, and every array messages() hands out), each control
// stream's to its capacity, so what a node's ports take of the heap is fixed by the plan it is in, and its first block,
// which the exporter's trial load runs, measures it. What a block's contents take is not: a payload longer than
// kPlacedLengths gets a Uint8Array made for its block, and a node may make things a message; the toolchain's heap
// check, which fills every input to the device MIDI input's capacity, SysEx included, on the node's default settings,
// is what holds a pack to a full block. (The box loads one plan and keeps it. A desktop host that re-plans makes a
// replacing instance's pools at its first block, while the instance it replaces waits for its plan to be reclaimed, so
// for that while the pack's heap holds both: hence the desktop's larger context.) messages(port) is one of a set of
// arrays the port keeps, by the power of two that holds the count (its length set within half its capacity, so the
// engine neither grows nor shrinks it, on a rooted slot, so it is right if JS spliced the array meanwhile), over the
// pool, whose frame and flags are getters on the block's queue and whose data is the place's view of the payload's
// length, refilled. control(id) is the port's stream object, whose changes are kept as the messages are. A message's
// frame and flags, a stream's start and changes and a change's frame and value are read through the port its object
// names, checked against the running node's layout and block (integers the host keeps natively), so an object a pack
// kept past its block, of a node whose slot another now holds, never reads out of bounds: it throws a TypeError when
// the new node has no port of that ordinal, side and kind, and reads that port's when it has one. A message's data is
// its own value in the JS heap, what it last held, and is not checked. send copies the data into the output queue at
// the call (a Uint8Array's bytes as they lie, any other array-like's element by element), so a node may refill and
// send one array again; a frame earlier than the port's last is raised to it, and a negative one wraps through
// ToUint32, as the desktop's host does. forward(port, message) is send(port, message.frame, message.data,
// message.flags), a message the block handed out read natively as its getters read it. A warmed block reads its own
// arrays and a message's bytes in place (rp_mqjs.h's rp_mqjs_array_get and rp_mqjs_bytes), finds a port named by a
// constant of the pack's bytecode by the value alone, and passes a port whose pool its queue fits with no JS at all:
// what a message costs is the interpreter's and the getters'. A node whose definition says idle: "skip" is not entered
// on a block that brings it no message and no change and starts each of its control inputs and params where its last
// call left it, whichever plan the block is of (defineNode.ts's host contract says when it still is). The host
// makes a block's arrays and views with the engine's own constructors (rp_mqjs.h), never through a call into
// JavaScript, so messages() works at any depth of a node's C re-entries, and a pack's own globals (a Uint8Array it
// reassigned) never reach the host's refill. A node's context holds it to kMaxReentries nested calls into JavaScript on
// the C stack (rp_mqjs.h's RP_MQJS_MAX_REENTRIES says why): a native's callback past them throws an InternalError.
//
// What a warmed block allocates: nothing native, ever; in the JS heap, nothing at 64 bits for the whole API but a
// payload longer than kPlacedLengths (a SysEx), whose Uint8Array is made for its block. At 32 bits, the box's width,
// MicroQuickJS boxes every number that is not a 31-bit integer (it has short floats at 64 bits alone), so there each
// read of a non-integer number allocates its box: a control stream's start and a change's value, the transport's
// tempo, ppq and sampleRate when they are not whole, and any such number the node computes. A node that keeps to
// bytes, frames, flags and integers (a MIDI policy) allocates nothing at either width.
//
// Faults (design section 4). A throw from process (or from create, layout or a hook, or from a getter the host reads of
// what they return: a result's then, an instance's process and hooks), a process that returns a promise or a thenable,
// a create that returns no process, and an entry that runs past its deadline each fault the node. An overrun faults it
// whatever the entry then returns: the engine's interrupt cannot be caught, but a builtin or a getter the host reads
// may drop it on the way out, so the host faults an entry that ran past its deadline as it ends. A faulted node's
// process answers RP_FAULT from then on, so the executor silences it and counts it once, and the node keeps the first
// fault's words (graph/rp_script.h's rp.script.fault/1). A node does not run again after a throw, unlike the desktop's
// QuickJS host: the box has no one to retry for. A heap that runs out (an allocation the engine finds no room for once
// it has collected, which it reports to the host: rp_mqjs.h's rp_mqjs_api.ran_out) in an entry that then ends in an
// exception kills the pack's context: every node of the pack faults at its next call, with the same words, and the
// context runs nothing more. What the entry throws does not decide it: a node's own throw of null, or of an error
// saying "out of memory", faults that node alone, as does the engine's out-of-memory error for an array or a buffer
// past its size limits, with room left. MicroQuickJS throws out of memory as an ordinary InternalError, which a node
// may catch: one that does and returns runs on in a heap with no room. Each entry into a context (a block, a hook, a
// settings check) is held to a deadline through the engine's interrupt handler, polled as kFirstPollDivisor says (the
// interpreter's calls and branches, and inside the builtins whose one call could run long), over everything the entry
// runs: by the host's clock (rp.host.clock/1, the box's, at kHostClockDeadlineMicros unless it
// says otherwise, a block's entry kHostClockMessageMicros longer for each message it hands the node), or, on the
// desktop when its host offers none, by the calling thread's CPU time at kDesktopDeadlineMicros, so a stall that is not
// the node's costs it nothing. On the device, a host that offers no clock runs its script nodes with no deadline.

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"
#include "graph/rp_script.h"
#include "script/rp_mqjs_pack.h"

#ifndef RP_DEVICE
#include <string>
#endif


namespace rp::mqjs {

// How many bytes the same objects take at this width for each one at 32 bits, the box's: 2 at 64 bits, 1 at 32.
// MicroQuickJS's heap is made of words (a value, a pointer, an object's header and its slots), so the objects take
// about twice the bytes at 64 bits. Measured on i386 against x86-64 (the same host sources and engine at each width):
// MicroQuickJS's own part of a context (the JSContext and the class prototypes) 1.88 times, devtest's definitions and
// instance 2.0, its port pools 2.05. What does not double, a byte's payload (a string, a typed array's buffer, so a
// SysEx), brings a whole run lower: the toolchain's run of bliptoaster, full blocks of notes and of 64-byte SysEx,
// needs 31680 bytes at 64 bits and 19296 at 32, 1.64 times. A float goes the other way: MicroQuickJS boxes one at 32
// bits alone, so a heap of floats can take more at 32 bits than at 64. So no 64-bit figure says exactly what fits at
// 32: the box's own figure (its boot line after the first block, the device check) is the authority, and a pack
// declares heapBytes with room over it.
#if INTPTR_MAX >= INT64_MAX
inline constexpr std::uint32_t kWidthFactor = 2;
#else
inline constexpr std::uint32_t kWidthFactor = 1;
#endif
// The toolchain's check (exactPackage(), the device-pack toolchain's runner) at 64 bits: a context of 3/2 heapBytes,
// under every ratio measured above. It is a heuristic, not a guarantee: a pack whose 64/32 ratio falls under 1.5 can
// pass it and still run out on the box, as one whose heap is mostly payload bytes (a node that keeps SysEx: bliptoaster
// under full SysEx blocks is at 1.64 already) or mostly floats (tempo, CC scaling: under 1). The box's figure decides.
inline constexpr std::uint32_t kCheckHalves = 3;
// The halves of heapBytes a device build of this width gives a pack's context: the box's figure exactly at 32 bits; at
// 64 bits, the toolchain check's, so the exporter's trial load (and every device-mode test) holds a plan's script nodes
// to the same heuristic, never to a laxer one.
#if INTPTR_MAX >= INT64_MAX
inline constexpr std::uint32_t kDeviceHalves = kCheckHalves;
#else
inline constexpr std::uint32_t kDeviceHalves = 2;
#endif
// What a pack's state takes of its memory at 32 bits, the box's width: a 32-bit build's state is held to it (a
// static_assert) and reserves exactly it, so a 64-bit build sizes the box's package memory (boxPackageBytes) as the box
// itself does. sizeof(PackState) is 416 bytes on the RP2350 (stateBytes() says it on the build that asks; the device
// check prints it).
inline constexpr std::size_t kBoxStateBytes = 448;
// What this build's state takes, unrounded: sizeof the host's state, which kBoxStateBytes reserves at 32 bits.
std::size_t stateBytes();

// How many instances one pack's context holds at once, all of its types together.
inline constexpr std::uint32_t kMaxInstances = 64;
// How many nested calls into JavaScript one context lets a node have on the C stack, the host's own call into it the
// first: script/rp_mqjs.h's RP_MQJS_MAX_REENTRIES, which says why it is this.
inline constexpr std::uint32_t kMaxReentries = 4;
// How many ports and params one node may have: a message's object names its port in 6 bits.
inline constexpr std::uint32_t kMaxPorts = 64;
// The payload lengths a message place keeps a Uint8Array for, from 1: every MIDI message but a SysEx.
inline constexpr std::uint32_t kPlacedLengths = 3;
// An entry's deadline by a host's clock that names none (the box's): 10 ms. A MIDI-rate node's process takes
// microseconds, the box's block is about a millisecond and its watchdog 3 s, so a node this far over is stuck. The
// box's other nodes lose this long before it is silenced, and kHostClockMessageMicros more for each message the block
// hands it (26 ms at a full queue of the device MIDI input's 64), and the overrun past it: about one call of the
// builtin the node was in when the deadline passed, which the pack's heap bounds (kFirstPollDivisor).
inline constexpr std::uint32_t kHostClockDeadlineMicros = 10000;
// What a block's entry gets on top of it by a host's clock, for each message the block hands the node on its events and
// bytes inputs: 250 us. A node's work grows with its messages, and one node's cost on the RP2350 at 150 MHz, run from
// flash, was measured at about 65 us a note and 48 us a 64-byte SysEx (rp.devtest.script-transpose), so a full queue
// of 64 takes about 4 ms of the fixed deadline alone: with this, an author's budget is about 250 us a message at any
// queue's fill, and a node is held to the fixed figure only where it is handed nothing. The desktop's CPU-time default
// has none.
inline constexpr std::uint32_t kHostClockMessageMicros = 250;
// How an entry polls its deadline. MicroQuickJS calls the interrupt handler once every so many of the calls and
// branches its interpreter runs, 10000 by its own count, and a builtin's work between two of them is unpolled, so at
// the engine's interval a loop over a builtin ran 10000 of its calls between polls, each bounded only by the pack's
// heap: in bliptoaster's 24576 bytes a `for (;;) a.sort()` on the desktop ran 0.7 s past a deadline of 10 ms, seconds
// on the box, past its 3 s watchdog. So the handler sets each next poll's distance (script/rp_mqjs.h's
// rp_mqjs_set_poll_interval): an entry first polls after its deadline in microseconds over kFirstPollDivisor calls and
// branches, at least kLeastPoll (8 on the box's 10 ms, 244 at the desktop's 500 ms); each poll before the deadline sets
// the next twice as far, to kMostPoll, the engine's own, but no further than the rate since the last poll says fits in
// half of what is left, so the polls close in on the deadline; and a poll past it sets the next at once, so an
// interrupt a builtin dropped is thrown again at the interpreter's next call or branch. The builtins whose one call can
// run long poll in their loops off the same count (script/rp_mqjs_engine.c: the sort a comparison, the string searches
// a position, join on what is no array an element), so a loop over any builtin overruns by about one call of a builtin
// whose work is linear in what the pack's heap holds. A node that finishes its entry in microseconds reads the clock a
// few times at most (the first poll and its doublings): the desktop's [script-bench], a transpose's block of 16
// messages, went from about 259 to 275 ns a message (two reads of the thread's CPU time a block), and of 256 from 244
// to 248; on the box, a read of its microsecond clock is a few dozen cycles.
inline constexpr std::uint32_t kFirstPollDivisor = 2048;
inline constexpr std::uint32_t kLeastPoll = 8;
inline constexpr std::uint32_t kMostPoll = 10000;
// An entry's deadline by the desktop's own clock, the thread's CPU time: 500 ms. Windows counts that time in scheduler
// ticks of about 15.6 ms, so a deadline must be many ticks long to be measured at all; a Debug or sanitizer build's
// first entry (the settings decoded, create and layout) takes tens of milliseconds; and a desktop host's audio thread
// can lose half a second to a node that hangs where the box could not.
inline constexpr std::uint32_t kDesktopDeadlineMicros = RP_SCRIPT_DESKTOP_DEADLINE_MICROS;
// What a desktop host gives a pack's context: its width's figure (heapBytes times kWidthFactor) times
// kDesktopHeapFactor, and at least kDesktopLeastContextBytes (desktopContextBytes()). A desktop plan's queues are the
// planner's (256 messages unless a port declares another), not the box's 64, and a node's pools follow the queues it
// reads, so a node behind a desktop source needs more than the box gives it (bliptoaster behind 256 places: about 74 KB
// at 64 bits); and a re-plan holds the instance it replaces beside the new one until its plan is reclaimed, both with
// their pools. The floor is the control context's size; the memory is zeroed by the allocator, so what the engine never
// touches is never paged in. The device's build, the exporter's trial load and the toolchain's runner give
// kDeviceHalves or kCheckHalves halves of heapBytes.
inline constexpr std::uint32_t kDesktopHeapFactor = 2;
inline constexpr std::size_t   kDesktopLeastContextBytes = std::size_t{1} << 20;
// The least context the host makes, at this build's width: below it MicroQuickJS's JS_NewContext sets the standard
// library up past its buffer and through the allocation that failed, rather than throwing (its assertion, 1 KiB, is no
// working floor). Measured over RetroPlug's standard library: at 64 bits a context of 4096 bytes sets itself up and one
// of 4088 crashes (ScriptPack.test.cpp holds the figure); at 32 bits, on i386 (the box's word width), 2552 and 2544.
// On ARM's EABI a uint64_t aligns to 8 in a struct, where i386 aligns it to 4, and MicroQuickJS's JSContext holds one,
// so the M33's floor may be a few bytes higher than the 32-bit figure here: the device check settles it on the box.
inline constexpr std::uint32_t kBoxLeastContextBytes = 2560;
#if INTPTR_MAX >= INT64_MAX
inline constexpr std::uint32_t kLeastContextBytes = 4096;
#else
inline constexpr std::uint32_t kLeastContextBytes = kBoxLeastContextBytes;
#endif

// The build's rp_package for a pack, made the first time it is asked for and the same after: its context
// kDeviceHalves halves of heapBytes on the device's build, desktopContextBytes() on the desktop's. Its package memory
// answers query_target for the box's width (boxPackageBytes). Null when the pack's name is not one a manifest's id
// holds, the build holds no more packs or types (RP_MQJS_MAX_PACKS, RP_MQJS_MAX_TYPES), or (on the desktop) the whole
// pack does not define the types it was built with; packageRefusal() then says why, and the desktop's build says it on
// stderr too. The source must outlive every use of the package.
const rp_package* package(const rp_mqjs_pack_source& source);
#ifndef RP_DEVICE
// The desktop build's rp_package for a pack whose context is kCheckHalves halves of heapBytes: the toolchain's runner,
// which holds a pack to its manifest. A registration of its own, beside package()'s.
const rp_package* exactPackage(const rp_mqjs_pack_source& source);
// The context a desktop host gives a pack (kDesktopHeapFactor).
std::size_t desktopContextBytes(const rp_mqjs_pack_source& source);
// The clock and deadline every desktop control context made from now on holds its calls to (a pack's load at its
// registration, a settings check, layout_for): by default the thread's CPU time at 2 s. The device-pack toolchain's
// runner gives it its --deadline-us, as it gives its nodes, so a build whose every run is slow (DEBUG_GC under a
// sanitizer, where loading scripttest's whole pack takes about 2 s of CPU time) registers a pack where an ordinary
// build does. Set it before the registrations it is for.
void setControlClock(std::uint64_t (*clock)(void* ctx), std::uint64_t deadlineMicros);
#endif
// Why the last package() or exactPackage() answered null (the registration's own thread), or empty.
const char* packageRefusal();

// A pack's package memory as the device's build of this width gives it: its context (kDeviceHalves halves of the
// manifest's heapBytes), the host's state and the context's copy of the runtime's bytecode.
std::size_t packageBytes(const rp_mqjs_pack_source& source);
// The same as the box's 32-bit build gives it, from any build: kBoxStateBytes, the 32-bit bytecode
// (box_bytecode_size) and heapBytes. What an export holds a box's arena to.
std::size_t boxPackageBytes(const rp_mqjs_pack_source& source);


#ifndef RP_DEVICE
// The desktop's tools (the device-pack toolchain's runner, retroplug-mqjs-run): `bytecode` (mqjs -o's output, which
// this relocates where it lies, and which the run writes into) run in a context of `contextBytes` over RetroPlug's
// standard library, its console.log written to `log`. Empty when it ran to its end; else what stopped it (its
// exception, or why it could not start).
std::string runScript(std::uint8_t* bytecode, std::size_t size, std::size_t contextBytes,
                      void (*log)(void* ctx, const char* text, std::size_t size), void* logCtx);
#endif

}  // namespace rp::mqjs
