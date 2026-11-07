#pragma once

// The device check (docs/modular-graph-phase-e1-design.md section 7): every node type a product registers, created
// from its default settings as msgpack into memory the caller owns, activated, run for one block through the executor
// and destroyed, with no heap and no exceptions. rp-device-check runs it on the RP2350 (main.cpp, beside this file),
// where the image's link trap is what proves the path allocates and throws nothing; retroplug-device-mode-test runs
// the same code on the desktop over the device-mode builds of the same packages, where it is what proves the path
// works and where the counting allocator stands in for the trap.
//
// It is a host, not a node: it reaches every type through the C ABI alone, and it plans nothing. What a planner would
// have worked out (each port's kind and channel count, each queue's capacity, each param's default) comes with the
// settings, from the desktop's build of the type (the case generator in cases/).
//
// Beside the types it runs a plan (runPlan, docs/modular-graph-phase-e3-design.md section 7): a multi-node plan the
// desktop exported for the RP2350 (plan.json beside this file, plan.cmake), loaded by the plan loader in the same
// arena, run in ragged blocks with its params moving, and unloaded, which puts the loader and the executor's coupled
// groups and feedback copies under the image's trap, and holds the run, on the desktop, to what the test expects.
//
// And a script pack (docs/modular-graph-phase-e4-design.md sections 7 and 8): the devtest pack's TypeScript nodes, on
// the MicroQuickJS host, are types like the rest, each run with its package's memory set up in the arena; a second
// plan (script-plan.json) runs two of them behind the device MIDI input, fed a fixed stimulus by the host, through the
// loader, which sets their pack's memory up beside the nodes; a bench (benchScript) times one of them alone, a block
// and a message at a time; and the deadline case (loopDeadline) times how long a loop over a builtin holds the block
// thread before its deadline faults the node.

#include <cstddef>
#include <cstdint>

#include "graph/PlanLoader.hpp"
#include "graph/rp_node.h"
#include "graph/rp_script.h"

namespace rp::device_check {

// One port of a type's layout for its default settings, resolved as a planner would for a node with no wires: an
// unwired "any" input is mono, a match output takes its port's count.
struct PortCase {
    std::uint32_t kind = RP_PORT_AUDIO;  // rp_port_kind
    bool          output = false;
    std::uint32_t channels = 0;  // audio
    std::uint32_t messages = 0;  // events and bytes: the queue's capacity
    std::uint32_t bytes = 0;
};

// A registered type's case: its default settings as the msgpack a device reads, and the layout they give.
struct TypeCase {
    const char*          id = nullptr;
    const std::uint8_t*  settings = nullptr;
    std::uint32_t        settingsSize = 0;
    const PortCase*      ports = nullptr;
    std::uint32_t        portCount = 0;
    const double*        params = nullptr;  // each param's default, in layout order
    std::uint32_t        paramCount = 0;
};

// Where a run stopped. Ok is the only pass; anything else names the type (by its index in registration order, across
// packages) and what it failed.
enum class Step : std::uint8_t {
    Ok,
    NoCase,       // a registered type with no case: the cases were generated from another registration
    CaseUnused,   // a case no registered type took
    QueryMemory,  // query_memory refused the settings
    Arena,        // the instance, its buffers and its queues do not fit the arena
    Create,       // create refused the settings
    Activate,
    Process,      // the block faulted the node
    Package,      // the type's package memory (a script pack's context) was refused, or does not fit the arena
};

struct Result {
    Step          step = Step::Ok;
    std::uint32_t type = 0;  // the type's index, in registration order
    const char*   id = nullptr;
    std::uint32_t types = 0;          // how many types ran to the end (on a pass, every one registered)
    std::size_t   instanceBytes = 0;  // the largest query_memory size among the types reached, the failed one included
    std::size_t   arenaBytes = 0;     // the most of the arena one type used, instance, buffers and queues together
    rp_error      error{};            // the type's own reason, where it gave one
};

// The one resource the check's host holds: a stereo audio buffer, res:1, which a case names where a type's defaults
// must name a resource and name none (the case generator's completions). Its frames are written by run(), so a type
// reads a signal from it, and a recorder may write it.
inline constexpr std::uint32_t kBufferResource = 1;
inline constexpr std::uint32_t kBufferChannels = 2;
inline constexpr std::uint32_t kBufferFrames = 256;

// What rp-device-check gives run(): one block of kBlockFrames, in an arena of kArenaBytes. The arena is sized for the
// largest default instance, rp.core.delay's (a second of stereo at 48 kHz, 384,000 bytes of delay line of its own),
// with room for its ports; runPlan's plan is loaded in the same arena afterwards, and is exported against it
// (plan.cmake's RP_DEVICE_CHECK_ARENA_BYTES, which the image and the test hold to this). retroplug-device-mode-test
// runs the check in an arena of the same size on the desktop, where pointers and max_align_t are wider, so the
// desktop's sizes should be an upper bound on the device's. For the plan the image checks it (PlanStep::Estimate,
// against the exporter's figure); for the per-type runs it prints the most a type used, and nothing holds it.
inline constexpr std::uint32_t kBlockFrames = 64;
inline constexpr std::size_t   kArenaBytes = 400 * 1024;

// The host every type is created under: no heap (the types it runs declare none), no guards, a resource table of the
// one buffer above, and device links of a host that owns no device (every acquire answers null). The case generator
// creates the desktop's builds of the types under it too. Beside those it answers:
// - rp.host.packages/1 with the one package memory openPackage() set up (run() opens each script type's own in the
//   arena for its run; the case generator opens its own), and refuses every other package;
// - rp.host.clock/1 with the clock offerClock() gave it, if any: a script node's deadline (the image's, the box's
//   figure), as the box's host offers its own;
// - rp.device.midi-in/1 with the check's stimulus (kStimulusCycle): what runPlan stages for each block of a case that
//   asks for it, and nothing at any other time.
const rp_host& host();

// Sets `package` up in `memory` (its memory query's size and alignment, zeroed by the caller) and offers it through
// host() until closePackage(), which tears it down. False, with err's message, when the package refuses; nothing is
// left open then.
bool openPackage(const rp_package* package, void* memory, rp_error* err);
void closePackage();

// The clock host() offers script nodes (rp.host.clock/1), or none (null), for every run from then on.
void offerClock(const rp_host_clock* clock);

// The stimulus the host's MIDI input carries (rp.device.midi-in/1) and the bench feeds its node, a cycle of five blocks
// after the device-pack toolchain's runner (packages/graph-runtime/mqjs-run), each as full as the device MIDI input's
// queue (RP_DEVICE_MIDI_IN_MESSAGES and _BYTES): 64 note messages; 64 SysEx of 64 bytes, the queue's bytes full; two
// blocks that sweep the first byte, 63 messages each whose first bytes run on from the last sweep's (0x00 to 0xFF and
// round again, each in the length a parser gives it, a lone F7 and the real-time bytes alone); then a block of none.
// Each block spreads its messages over its frames in order.
inline constexpr std::uint32_t kStimulusCycle = 5;
// How many messages block `block` of the stimulus holds.
std::uint32_t stimulusCount(std::uint32_t block);
// Message `k` of block `block` into `out` (64 bytes at least): its size, and its frame in a block of `frames` frames.
std::uint32_t stimulusMessage(std::uint32_t block, std::uint32_t k, std::uint32_t frames, std::uint8_t* out,
                              std::uint32_t* frame);

// Runs every type of every package in `packages` (rp_register_packages()'s list) against its case, one block of
// `frames` frames at 48 kHz each, with `arena` reused from one type to the next.
Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames);
// The same, filling `out`: a result is a few hundred bytes, so the image holds it rather than its own frame, which the
// deepest chains (a script node's) run on top of.
void run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
         void* arena, std::size_t arenaBytes, std::uint32_t frames, Result* out);

// ---- the plan ----------------------------------------------------------------------------------------------------

// A param change the plan check pushes at frame 0 of block `block`: node `node`'s param `param`, in its layout's order.
struct PlanMove {
    std::uint32_t block = 0;
    const char*   node = nullptr;
    std::uint32_t param = 0;
    double        value = 0.0;
};

// A counter the checks time with (the M33's cycle counter on the image), or null for no timing. Unsigned 32-bit
// differences, so a wrap between two reads costs nothing.
using Cycles = std::uint32_t (*)();

// What runPlan runs: an exported plan and the arena the exporter's sizing gave it (its header's <symbol>ArenaEstimate;
// 0 for none), how many frames, in what block sizes (taken in turn, each held to the plan's maxFrames and to what is
// left; none given, maxFrames each; a zero refused), the param moves, and the node whose rp.devtest.digest/1 the run
// must feed (null for none). Beside those, all off by default:
// - stimulus: the host's MIDI input carries the stimulus (stimulusMessage) for each block, block 0 the cycle's first;
// - expectDigest: the digest must be `want` exactly, a plan whose digest holds no float arithmetic (messages alone);
// - contextBytes: every package with memory of its own must have made a context of this many bytes (rp.script.heap/1's
//   `bytes`), the figure its build gives the image's declared heap, so a registration that gave another fails;
// - cycles: the load and the first block are timed with it;
// - afterLoad: called once the plan has loaded, before its first block (the image reads its stack's depth there, so
//   the load's and the blocks' are told apart).
struct PlanCase {
    const std::uint8_t*  blob = nullptr;
    std::size_t          blobSize = 0;
    std::size_t          arenaEstimate = 0;
    std::uint32_t        frames = 0;
    const std::uint32_t* splits = nullptr;
    std::size_t          splitCount = 0;
    const PlanMove*      moves = nullptr;
    std::size_t          moveCount = 0;
    const char*          digest = nullptr;
    bool                 stimulus = false;
    bool                 expectDigest = false;
    std::uint64_t        want = 0;
    std::size_t          contextBytes = 0;
    Cycles               cycles = nullptr;
    void (*afterLoad)() = nullptr;
};

// The image's run of its plan (plan.json): a second at 48 kHz, in ragged blocks from 1 frame to the plan's maxFrames
// (256), with the gain, both lockstep drives, the biquad's frequency, the constant and the delay's time moving. Its
// node ids and param positions are plan.json's.
inline constexpr std::uint32_t kPlanFrames = 48000;
inline constexpr std::uint32_t kPlanSplits[] = {1, 31, 256, 3, 200, 77, 128, 64, 255, 16};
inline constexpr PlanMove      kPlanMoves[] = {
    {2, "g", 0, -1.5}, {3, "a", 0, 1.9}, {5, "bq", 0, 700.0}, {6, "b", 0, 0.4},
    {8, "k", 0, 0.2},  {9, "d", 0, 300.0}, {11, "g", 0, 0.3}, {40, "bq", 0, 4000.0},
};
inline constexpr const char* kPlanDigest = "dg";
// What the digest hears when the desktop's builds run the same plan the same way: linux-x64 GCC 13's figure (the
// reference host parity and the kit and render baselines hold theirs to), pinned there by retroplug-device-mode-test,
// which fails with the new value when plan.json, a node it names or the case moves it. Another compiler or architecture
// may differ, as the M33 may: the image prints its own beside it and does not judge it, the float tolerance between
// the device and the desktop being the audio version's.
inline constexpr std::uint64_t kPlanDesktopDigest = 0xb8854b2cc247318full;

// The image's run of its script plan (script-plan.json): rp.device.midi-in fed the stimulus, then
// rp.devtest.script-transpose and rp.devtest.script-nest (deep: it drops the real-time messages, from the host's
// deepest C re-entry, so the digest hears whether the deep path ran), two script nodes of the devtest pack sharing its
// context, then rp.devtest.digest, for 40 blocks of the splits in turn (eight times the stimulus's cycle, each kind of
// block at each split). Its digest hears messages alone (the digest's audio and control inputs unwired: a silent
// channel and a stream that starts at 0 and never moves), integers the script nodes made on whichever engine width ran
// them, so it is the same on every host and the image holds its own run to it: the device's 32-bit bytecode on the M33
// against the desktop's 64-bit run (retroplug-device-mode-test pins it, on every host).
inline constexpr std::uint32_t kScriptPlanSplits[] = {256, 1, 128, 77};
inline constexpr std::uint32_t kScriptPlanFrames = (256 + 1 + 128 + 77) * 10;
inline constexpr const char*   kScriptPlanDigest = "dg";
inline constexpr std::uint64_t kScriptPlanWant = 0xc7248a64d500e6d8ull;

// Where a plan run stopped. Ok is the only pass; each other step names the node at fault in `error.node` ("-" for
// none: a blob refused whole, a case's own fault) and says what happened in `error.message`.
enum class PlanStep : std::uint8_t {
    Ok,
    Load,      // the loader refused the plan, or a node failed part-way through its run-up (unwound): its error
    Move,      // a move names no param stream the plan has (no such node, a param past its params, or one a wire
               // drives), or its stream is full
    Process,   // a block faulted a node (the executor silences it from then on)
    Result,    // the run did not hold: a move never pushed (past the last block), a queue dropped messages, or the
               // case's digest node missing, deaf, or counting other blocks than ran
    Estimate,  // the loaded plan took more arena than the exporter's sizing gave it, which the design takes as an upper
               // bound on the device's (its section 11, question 1)
    Case,      // the case cannot run: a block size of 0
    Heap,      // a script pack's context is not the size the case says its image declares
};

struct PlanResult {
    PlanStep                 step = PlanStep::Ok;
    rp::graph::PlanLoadError error{};  // the loader's for Load; for every other step, the node and what happened
    std::uint32_t            nodes = 0;
    std::size_t              arenaBytes = 0;  // what the loaded plan took of the arena
    double                   sampleRate = 0.0;
    std::uint32_t            maxFrames = 0;
    std::uint32_t            blocks = 0;  // blocks run (on a Process failure, the one that faulted included)
    std::uint32_t            frames = 0;
    std::uint32_t            moves = 0;  // param changes pushed
    std::uint32_t            drops = 0;  // messages the plan's full queues dropped
    // What the case's digest node (rp.devtest.digest/1) had seen at the end, when the plan has the node it names: its
    // FNV-1a 64 of everything that reached it, the blocks it counted, and whether any of it was more than silence.
    bool                     digested = false;
    std::uint64_t            digest = 0;
    std::uint32_t            digestBlocks = 0;
    bool                     heard = false;
    std::uint32_t            messages = 0;  // what the host's MIDI input handed the plan (a stimulus case's)
    std::uint32_t            loadCycles = 0;        // by the case's counter: the load, the packages' setup included
    std::uint32_t            firstBlockCycles = 0;  // and the first block, which makes each script node's instance
    // Each package the plan set memory up for (its first kPackReports), read once the run ends, before the unload:
    // its name, the memory the loader placed (the host's state, the bytecode's copy and the context), and of the
    // context (rp.script.heap/1, collected): its bytes, what MicroQuickJS keeps of them for itself and what that and
    // the live objects take in all.
    struct Pack {
        char        name[24];
        std::size_t memoryBytes;
        std::size_t contextBytes;
        std::size_t fixedBytes;
        std::size_t inUseBytes;
    };
    static constexpr std::uint32_t kPackReports = 2;
    Pack                     packs[kPackReports];
    std::uint32_t            packCount = 0;
};

// Loads `c.blob` into `arena` with the loader, over the node types of `packages` (rp_register_packages()'s list) under
// host(), holds its arena to the case's estimate, runs it as `c` says, judges the run (PlanStep::Result) and unloads
// it. Fills `out`: a result is a few hundred bytes, so the caller holds it rather than this frame, and every one of its
// bytes is zero by default, so the image's static result lands in .bss and costs no initialised RAM. Allocates nothing.
void runPlan(const PlanCase& c, const rp_package* const* packages, std::size_t packageCount, void* arena,
             std::size_t arenaBytes, PlanResult* out);

// ---- the script bench --------------------------------------------------------------------------------------------

// What benchScript times: the type `id` (one of `packages`' types, its case among `cases`) created from its case's
// settings with its package's memory set up in the arena, as run() makes it, its input fed the stimulus's notes and
// SysEx, `rounds` rounds of an empty block, a block of notes and a block of SysEx, each of kBlockFrames frames, after a
// first, empty block that makes its instance and its pools.
inline constexpr const char*   kBenchType = "rp.devtest.script-transpose";
inline constexpr std::uint32_t kBenchRounds = 16;

enum class BenchStep : std::uint8_t {
    Ok,
    Type,     // no registered type has the id, or no case is the type's
    Package,  // its package memory was refused, or does not fit the arena
    Create,   // query_memory or create refused its settings, or the instance does not fit
    Activate,
    Ports,    // its ports, or an input that carries no messages: the bench feeds one
    Process,  // a block faulted the node (its script fault's words in error.message)
    Result,   // the node passed on other than every message it was handed
};

struct BenchResult {
    BenchStep     step = BenchStep::Ok;
    rp_error      error{};
    std::uint32_t blocks = 0;     // blocks run, the first included
    std::uint32_t messages = 0;   // messages the node was handed
    std::uint32_t passed = 0;     // and passed on
    // By the case's counter, each kind's total over the rounds (the first block alone).
    std::uint32_t firstCycles = 0;
    std::uint64_t emptyCycles = 0;
    std::uint64_t noteCycles = 0;
    std::uint64_t sysexCycles = 0;
    std::uint32_t noteMessages = 0;   // the messages in each note block
    std::uint32_t sysexMessages = 0;  // and in each SysEx block
    // The pack's context once the rounds are run (rp.script.heap/1, collected).
    std::size_t   contextBytes = 0;
    std::size_t   inUseBytes = 0;
};

// Runs the bench in `arena` (see above), over the host() every type runs under. Allocates nothing; fills `out`.
void benchScript(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases,
                 std::size_t caseCount, void* arena, std::size_t arenaBytes, std::uint32_t rounds, Cycles cycles,
                 BenchResult* out);

// ---- the deadline over a loop of builtins ---------------------------------------------------------------------

// What loopDeadline runs (docs/modular-graph-phase-e4-design.md section 4, the deadline): rp.devtest.script-loop made
// from each kLoops case's settings in a package of the image's script pack registered again at kLoopHeapBytes, the heap
// the box's bliptoaster pack declares, so its context is the box's pack's size; its blocks run until one faults it,
// each time the deadline's: a loop over one builtin, whose every call is work the interpreter's polls never see into,
// is the longest a node can hold the block thread before its deadline faults it. Each case's last block, the one the
// deadline ended, is timed, and the node logs a line as its loop begins (rp.host.script-log/1, which the check's host
// counts), so a case the deadline faulted before its loop began fails. The cases' data is about what that heap holds
// beside devtest's definitions at either width: an array's sort of 600 numbers, a search in 4000 characters, join of an
// array-like of a billion elements (whose loop nothing but its length bounds), forEach over 2000 numbers and
// JSON.stringify of 400; then, each after 300 turns of plain code, which let the polls grow to their cap before the
// builtin's loop starts (a phase change, MqjsHost.hpp's kPollCapDivisor): forEach and JSON.stringify again, indexOf
// over 1000 references to a 4096-character string for a needle that differs at its end (4 MB compared a call, which the
// comparisons' count of their bytes brings into the polls), forEach over 1200 numbers with a callback that makes
// strings of 1024 characters and more (whose callbacks the polls see between), and an array that applies
// Function.prototype.apply to itself, a call of apply that hands on the same call for ever, each a tail call the
// interpreter runs with no poll of its own (which apply's own poll sees).
inline constexpr const char*   kLoopType = "rp.devtest.script-loop";
inline constexpr std::uint32_t kLoopHeapBytes = 24576;
struct LoopCase {
    const char*   loop;
    std::uint32_t count;
    std::uint32_t warm = 0;
    std::uint32_t length = 4096;  // indexOf's and callback's strings
};
inline constexpr LoopCase kLoops[] = {
    {"sort", 600},          {"search", 4000},       {"join", 1000000000},         {"forEach", 2000},
    {"stringify", 400},     {"forEach", 2000, 300}, {"stringify", 400, 300},      {"indexOf", 1000, 300, 4096},
    {"callback", 1200, 300, 1024},                  {"apply", 1, 300}};
inline constexpr std::uint32_t kLoopCount = sizeof kLoops / sizeof kLoops[0];
// How many blocks a case may take to fault (its data is made a few hundred numbers a block first).
inline constexpr std::uint32_t kLoopBlocks = 32;
// The longest a case's last block may take, by the image's judgement: a third of the box's 3 s watchdog, so a node in
// the worst loop is faulted with the watchdog's restart a long way off.
inline constexpr std::uint32_t kLoopBoundMicros = 1000000;

enum class LoopStep : std::uint8_t {
    Ok,
    Type,     // the package has no such type, or no case is the type's
    Package,  // its package memory was refused, or does not fit the arena
    Create,   // query_memory or create refused a case's settings, or the instance does not fit
    Activate,
    Ports,    // its ports do not fit the arena
    Result,   // a case's blocks did not end in its deadline's fault (the node's fault's words, if any, in the error)
};

struct LoopResult {
    LoopStep      step = LoopStep::Ok;
    rp_error      error{};
    std::uint32_t loop = 0;                  // the case that failed
    std::uint64_t micros[kLoopCount] = {};   // each case's last block, by the case's clock (0 untimed)
    std::uint32_t blocks[kLoopCount] = {};   // and how many blocks it ran, the last included
    bool          entered[kLoopCount] = {};  // whether its loop began (the node logged it) before the deadline
    std::size_t   contextBytes = 0;          // the pack's context (rp.script.heap/1)
};

// A clock in microseconds the loop case times its blocks with (time_us_64 on the image), or null for none.
using Micros = std::uint64_t (*)();

// Runs the cases (see above) in `arena` over `package` (the pack registered at kLoopHeapBytes) and `cases` (the
// image's, whose case for kLoopType gives the type's ports), under host() and the clock offerClock gave it, whose
// deadline is what faults each. Allocates nothing; fills `out`.
void loopDeadline(const rp_package* package, const TypeCase* cases, std::size_t caseCount, void* arena,
                  std::size_t arenaBytes, Micros micros, LoopResult* out);

}  // namespace rp::device_check
