// rp-device-check (docs/modular-graph-phase-e1-design.md section 7): the RP2350 image that runs the device check over
// every type its product registers (CMakeLists.txt: packages.cmake's list, as a device builds it), and the proof, at
// link time, that what it links allocates and throws nothing: the image links with a trap on the heap, on exceptions
// and on RTTI (CMakeLists.txt), so a use of any of them anywhere in its code is a link error naming the object that
// pulled it in.
//
// Beside the types it runs the N8 pack's protocol over a scripted cart (N8Protocol.hpp), which links the pack's Edio,
// Menu and Ops into the image under the same trap, and a plan (DeviceCheck.hpp's runPlan): plan.json, exported at build
// time by the desktop's retroplug-export-plan against this image's arena and embedded (DeviceCheckPlan.hpp), loaded
// by the plan loader in that arena, run for a second in ragged blocks with its params moving, and unloaded, which
// links the loader and the executor's coupled groups and feedback copies under the trap too.
//
// And a script pack (docs/modular-graph-phase-e4-design.md section 8): the devtest pack's TypeScript nodes on the
// device's MicroQuickJS host, its 32-bit bytecode kept in flash and copied into the arena with the context its
// registration gives it (RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES, packages.cmake). Its types are checked with the rest; the
// bench times rp.devtest.script-transpose alone, in the M33's cycles; and, last, its script plan (script-plan.json:
// the host's MIDI input, fed DeviceCheck.hpp's stimulus, through two of its nodes into a digest) is loaded, run for 40
// ragged blocks and unloaded, its digest held to the desktop's exactly (it hears messages alone), its pack's context
// to the declared figure, and its stack measured on its own, its load's and its blocks' apart, the nest node taking the
// engine to the host's deepest C re-entry every block. Before that plan, the deadline case (DeviceCheck.hpp's
// loopDeadline) runs rp.devtest.script-loop's loops over a builtin in the pack registered again at the box's pack's
// heap, times how long each holds the block thread before its deadline faults it, and judges the worst against
// kLoopBoundMicros, a third of the box's watchdog. Then the policy case (DeviceCheck.hpp's policyHeap) runs the box's
// MIDI policy, the bliptoaster pack registered at the box's heap less kPolicyMarginBytes, over the blocks that fill its
// heap most, so a pass says the box's pack holds them with that much to spare at the box's width.
//
// It reports the outcome three ways: one line on stdio (the UART, GP0 at 115200) for each check, gDeviceCheck,
// gN8Protocol, gDevicePlan, gScriptPlan, gScriptBench, gLoops and gPolicy for a debugger, and the board's LED, lit and
// steady for a pass and for a failure a count of blinks, repeated after a pause: the failed Step's value for a type,
// kPlanBlinks plus the PlanStep's for the plan, kN8ProtocolBlinks for the protocol, kScriptPlanBlinks plus the
// PlanStep's for the script plan (plus kScriptStackBlinks for a stack that left less than kStackReserve), and
// kBenchBlinks plus the BenchStep's for the bench, kLoopBlinks plus the LoopStep's for the deadline case
// (kLoopBoundBlinks over its bound), and kPolicyBlinks plus the PolicyStep's for the policy case. The plan's line
// reports its node count and the arena it took, as the device measured them, beside the exporter's estimate, which the
// check holds it to, and the digest of what reached its end beside the desktop's (DeviceCheck.hpp's
// kPlanDesktopDigest), which it does not judge: the float tolerance between the two is the audio version's. The script
// plan's lines report the same, the load's and the first block's time, the pack's memory and its context's use, and the
// stack its load and its blocks took, each judged against the stack with kStackReserve kept; the bench's line its
// cycles. A last line gives the deepest the stack went in all (stackHighWater below).
//
// An absent plan line with a dark LED is a hang or a stack overrun, which the stack guard (CMakeLists.txt) turns into a
// HardFault that halts at isr_hardfault's breakpoint: halted with the probe, the overrun reads CFSR (0xE000ED28) with
// STKOF set (bit 20, 0x00100000) and HFSR (0xE000ED2C) with FORCED (bit 30), the PC at isr_hardfault.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "DeviceCheck.hpp"
#include "DeviceCheckCases.hpp"
#include "DeviceCheckPlan.hpp"
#include "DeviceCheckScriptPlan.hpp"
#include "N8Protocol.hpp"
#include "graph/Packages.hpp"
#include "graph/noinline.hpp"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/m33.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "pico/stdio.h"
#include "script/MqjsHost.hpp"

// The cases were generated for one list of package entries, and this image registers its product's: they must be the
// same list, or a type would meet a case made for another (CMakeLists.txt passes the product's entries).
static_assert(std::string_view(RP_DEVICE_CHECK_ENTRIES) == rp::device_check::kEntries,
              "DeviceCheckCases.hpp was generated for other package entries than this image registers: regenerate it "
              "with rp-device-check-cases from the same packages.cmake (tools/build-device-check.sh)");

// The image's script pack's 32-bit source (its pack32.c), which the deadline case registers again at another heap, and
// the bliptoaster pack's, which the policy case registers at kPolicyHeapBytes.
extern "C" const rp_mqjs_pack_source rp_script_devtest;
extern "C" const rp_mqjs_pack_source rp_script_bliptoaster;

// The stack's bounds (the SDK's linker script: the 4 KB of SCRATCH_Y, PICO_STACK_SIZE).
extern "C" char __StackBottom[];
extern "C" char __StackTop[];

// The plan was exported against this image's arena (plan.cmake), which its loader holds it to at run time.
static_assert(RP_DEVICE_CHECK_ARENA_BYTES == rp::device_check::kArenaBytes,
              "plan.cmake's RP_DEVICE_CHECK_ARENA_BYTES is not DeviceCheck.hpp's kArenaBytes");

namespace {

// Aligned as the loader asks.
alignas(rp::graph::kArenaAlign) std::byte gArena[rp::device_check::kArenaBytes];

void led(bool on) { gpio_put(PICO_DEFAULT_LED_PIN, on); }

// What the plan check runs (DeviceCheck.hpp): the embedded plan, for a second, in its ragged blocks with its moves.
constexpr rp::device_check::PlanCase kPlan{kDeviceCheckPlan,
                                           sizeof kDeviceCheckPlan,
                                           kDeviceCheckPlanArenaEstimate,
                                           rp::device_check::kPlanFrames,
                                           rp::device_check::kPlanSplits,
                                           std::size(rp::device_check::kPlanSplits),
                                           rp::device_check::kPlanMoves,
                                           std::size(rp::device_check::kPlanMoves),
                                           rp::device_check::kPlanDigest};

// The M33's cycle counter (DWT CYCCNT), which the script checks time with, started by startCycles. It is optional in an
// M33 (DWT_CTRL.NOCYCCNT, bit 25, says it is absent): the microsecond clock stands in for it then, in cycles at
// clk_sys, at a microsecond's resolution.
bool          gCycleCounter = false;
std::uint32_t gCyclesPerMicro = 1;

std::uint32_t cycles() { return gCycleCounter ? m33_hw->dwt_cyccnt : time_us_32() * gCyclesPerMicro; }

void startCycles() {
    gCyclesPerMicro = clock_get_hz(clk_sys) / 1000000u;
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
    gCycleCounter = (m33_hw->dwt_ctrl & (1u << 25)) == 0;
    if (gCycleCounter) {
        m33_hw->dwt_cyccnt = 0;
        m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
    }
}

// Microseconds from cycles at clk_sys.
unsigned micros(std::uint64_t cycleCount) { return static_cast<unsigned>(cycleCount / gCyclesPerMicro); }

// The clock the script nodes' deadline reads, as the box's host offers its own (pico/host's DeviceHost): its
// microseconds, at the script host's default deadline for a host's clock (10 ms).
std::uint64_t hostMicros(void*) { return time_us_64(); }
const rp_host_clock kClock{nullptr, &hostMicros, 0};
// The same microseconds, which the deadline case times its blocks by (the cycle counter wraps every 28 s at 150 MHz,
// and a loop the deadline failed to end could run longer).
std::uint64_t loopMicros() { return time_us_64(); }

// Reads the script plan's load's stack and paints it again for its blocks (below).
void afterScriptPlanLoad();

// What the script plan check runs (DeviceCheck.hpp): the embedded script plan, in its ragged blocks, the host's MIDI
// input carrying the stimulus, its digest held to the desktop's and its pack's context to the image's figure (at 32
// bits a context is the declared heap exactly), timed by the cycle counter.
constexpr rp::device_check::PlanCase kScriptPlan{.blob = kDeviceCheckScriptPlan,
                                                 .blobSize = sizeof kDeviceCheckScriptPlan,
                                                 .arenaEstimate = kDeviceCheckScriptPlanArenaEstimate,
                                                 .frames = rp::device_check::kScriptPlanFrames,
                                                 .splits = rp::device_check::kScriptPlanSplits,
                                                 .splitCount = std::size(rp::device_check::kScriptPlanSplits),
                                                 .digest = rp::device_check::kScriptPlanDigest,
                                                 .stimulus = true,
                                                 .expectDigest = true,
                                                 .want = rp::device_check::kScriptPlanWant,
                                                 .contextBytes = RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES,
                                                 .cycles = &cycles,
                                                 .afterLoad = &afterScriptPlanLoad};

// The blinks a plan failure shows when the types passed, plus its PlanStep's value, a protocol failure when both
// passed, and a script plan's and the bench's failures (plus their steps) when everything before passed: past every
// Step's value, and each past the one before.
constexpr unsigned kPlanBlinks = 10;
constexpr unsigned kN8ProtocolBlinks = 20;
constexpr unsigned kScriptPlanBlinks = 30;
constexpr unsigned kBenchBlinks = 40;
constexpr unsigned kLoopBlinks = 50;
constexpr unsigned kPolicyBlinks = 60;
// A deadline case whose worst loop took longer than kLoopBoundMicros: kLoopBlinks plus this, past every LoopStep's.
constexpr unsigned kLoopBoundBlinks = 9;
// A script plan whose load or blocks left less than kStackReserve: kScriptPlanBlinks plus this, past every PlanStep's.
constexpr unsigned kScriptStackBlinks = 9;

// What the script plan's load and blocks must each leave of the stack: room for an interrupt on top of the deepest
// chain, as the box takes its UART's and USB's (about a hundred bytes of exception frame and a few hundred of handler).
// The check enables none, so its own runs never need it.
constexpr std::size_t kStackReserve = 512;

const char* planStepName(rp::device_check::PlanStep step) {
    switch (step) {
        case rp::device_check::PlanStep::Ok: return "ok";
        case rp::device_check::PlanStep::Load: return "load";
        case rp::device_check::PlanStep::Move: return "move";
        case rp::device_check::PlanStep::Process: return "process";
        case rp::device_check::PlanStep::Result: return "result";
        case rp::device_check::PlanStep::Estimate: return "estimate";
        case rp::device_check::PlanStep::Case: return "case";
        case rp::device_check::PlanStep::Heap: return "heap";
    }
    return "?";
}

const char* benchStepName(rp::device_check::BenchStep step) {
    switch (step) {
        case rp::device_check::BenchStep::Ok: return "ok";
        case rp::device_check::BenchStep::Type: return "type";
        case rp::device_check::BenchStep::Package: return "package";
        case rp::device_check::BenchStep::Create: return "create";
        case rp::device_check::BenchStep::Activate: return "activate";
        case rp::device_check::BenchStep::Ports: return "ports";
        case rp::device_check::BenchStep::Process: return "process";
        case rp::device_check::BenchStep::Result: return "result";
    }
    return "?";
}

// The stack's depth, measured on the device (the E3 design's question 2) rather than summed from -fstack-usage by hand:
// at boot, before anything else runs, every word of the stack below main's entry (from the linker's __StackBottom, the
// stack guard's limit, to a little under the stack pointer) is written with kStackPaint; at the end the lowest word no
// longer painted is the deepest any check went, an interrupt's frame included. A word a check wrote with the paint's
// value would read as unused, which a 32-bit pattern makes as good as never.
constexpr std::uint32_t kStackPaint = 0x5AC4D00Du;

[[gnu::noinline]] void paintStack() {
    std::uintptr_t sp;
    asm volatile("mov %0, sp" : "=r"(sp));
    const std::uintptr_t end = sp - 64;  // this frame's callees, should any be made, keep their room
    for (std::uintptr_t w = reinterpret_cast<std::uintptr_t>(__StackBottom); w < end; w += 4)
        *reinterpret_cast<volatile std::uint32_t*>(w) = kStackPaint;
}

std::size_t stackHighWater() {
    const std::uintptr_t top = reinterpret_cast<std::uintptr_t>(__StackTop);
    std::uintptr_t       w = reinterpret_cast<std::uintptr_t>(__StackBottom);
    while (w < top && *reinterpret_cast<const volatile std::uint32_t*>(w) == kStackPaint) w += 4;
    return top - w;
}

}  // namespace

// The outcome, where a debugger finds it.
volatile rp::device_check::Step gDeviceCheckStep = rp::device_check::Step::Ok;
rp::device_check::Result           gDeviceCheck;
rp::device_check::N8ProtocolResult gN8Protocol;
rp::device_check::PlanResult       gDevicePlan;
rp::device_check::PlanResult       gScriptPlan;
rp::device_check::BenchResult      gScriptBench;
rp::device_check::LoopResult       gLoops;
rp::device_check::PolicyResult     gPolicy;

namespace {

// The registration every check runs over (rp_register_packages()).
const rp_package* const* gPackages = nullptr;
std::size_t              gPackageCount = 0;

// Each check runs in a function of its own and prints in another, none inlined into main, so the frame under each run
// is main's and its own, both small: a script node's deepest chain (the script plan's nest node, at the engine's last
// C re-entry) runs on top of them, as the box's runs on top of its loop. The results are the globals above.

RP_NOINLINE void runTypes() {
    rp::device_check::run(gPackages, gPackageCount, rp::device_check::kCases, rp::device_check::kCaseCount, gArena,
                          sizeof gArena, rp::device_check::kBlockFrames, &gDeviceCheck);
    gDeviceCheckStep = gDeviceCheck.step;
}

RP_NOINLINE void printTypes() {
    if (gDeviceCheck.step == rp::device_check::Step::Ok)
        std::printf("rp-device-check: pass, %u types, arena %u bytes\n", static_cast<unsigned>(gDeviceCheck.types),
                    static_cast<unsigned>(gDeviceCheck.arenaBytes));
    else
        std::printf("rp-device-check: step %u at type %u (%s): %s\n", static_cast<unsigned>(gDeviceCheck.step),
                    static_cast<unsigned>(gDeviceCheck.type), gDeviceCheck.id ? gDeviceCheck.id : "-",
                    gDeviceCheck.error.message);
}

RP_NOINLINE void runN8Protocol() {
    gN8Protocol = rp::device_check::n8Protocol();
    if (gN8Protocol.ok)
        std::printf("rp-device-check: n8 protocol pass, %u bytes\n", static_cast<unsigned>(gN8Protocol.written));
    else
        std::printf("rp-device-check: n8 protocol failed at %s (%u bytes, hash %08x)\n", gN8Protocol.failed,
                    static_cast<unsigned>(gN8Protocol.written), static_cast<unsigned>(gN8Protocol.hash));
}

// The plan, in the arena the types used, over the same registration: its microseconds.
RP_NOINLINE std::uint32_t runPlanCheck() {
    const std::uint32_t start = time_us_32();
    rp::device_check::runPlan(kPlan, gPackages, gPackageCount, gArena, sizeof gArena, &gDevicePlan);
    return time_us_32() - start;
}

RP_NOINLINE void printPlan(std::uint32_t took) {
    const rp::device_check::PlanResult& p = gDevicePlan;
    if (p.step == rp::device_check::PlanStep::Ok)
        std::printf("rp-device-check: plan pass, %u nodes, %u arena bytes (the export's estimate %u, the arena %u), "
                    "%u Hz, %u frames a block: loaded, ran %u frames in %u blocks with %u param moves and %u drops, "
                    "unloaded, in %u us; digest %08x%08x over %u blocks (the desktop's %08x%08x)\n",
                    static_cast<unsigned>(p.nodes), static_cast<unsigned>(p.arenaBytes),
                    static_cast<unsigned>(kDeviceCheckPlanArenaEstimate), static_cast<unsigned>(sizeof gArena),
                    static_cast<unsigned>(p.sampleRate), static_cast<unsigned>(p.maxFrames),
                    static_cast<unsigned>(p.frames), static_cast<unsigned>(p.blocks), static_cast<unsigned>(p.moves),
                    static_cast<unsigned>(p.drops), static_cast<unsigned>(took), static_cast<unsigned>(p.digest >> 32),
                    static_cast<unsigned>(p.digest), static_cast<unsigned>(p.digestBlocks),
                    static_cast<unsigned>(rp::device_check::kPlanDesktopDigest >> 32),
                    static_cast<unsigned>(rp::device_check::kPlanDesktopDigest));
    else if (p.arenaBytes == 0)  // refused at load, whose own message carries what it measured
        std::printf("rp-device-check: plan failed, step %u (%s) at node %s after %u blocks: %s%s%s\n",
                    static_cast<unsigned>(p.step), planStepName(p.step), p.error.node,
                    static_cast<unsigned>(p.blocks), p.error.code, p.error.code[0] ? ": " : "", p.error.message);
    else
        std::printf("rp-device-check: plan failed, step %u (%s) at node %s after %u blocks: %s (%u arena bytes, the "
                    "export's estimate %u)\n",
                    static_cast<unsigned>(p.step), planStepName(p.step), p.error.node,
                    static_cast<unsigned>(p.blocks), p.error.message, static_cast<unsigned>(p.arenaBytes),
                    static_cast<unsigned>(kDeviceCheckPlanArenaEstimate));
}

// The bench, with the XIP cache's counters read around it: the image runs from flash (the box runs from RAM), so its
// cycles hold the cache's misses, which these say the share of.
RP_NOINLINE void runBench() {
    xip_ctrl_hw->ctr_acc = 0;
    xip_ctrl_hw->ctr_hit = 0;
    rp::device_check::benchScript(gPackages, gPackageCount, rp::device_check::kCases, rp::device_check::kCaseCount,
                                  gArena, sizeof gArena, rp::device_check::kBenchRounds, &cycles, &gScriptBench);
    const unsigned xipReads = xip_ctrl_hw->ctr_acc, xipHits = xip_ctrl_hw->ctr_hit;
    const rp::device_check::BenchResult& b = gScriptBench;
    if (b.step != rp::device_check::BenchStep::Ok) {
        std::printf("rp-device-check: script bench failed, step %u (%s) after %u blocks: %s\n",
                    static_cast<unsigned>(b.step), benchStepName(b.step), static_cast<unsigned>(b.blocks),
                    b.error.message);
        return;
    }
    // Each kind's cycles a block, averaged over the rounds; a message's, its kind's block over the empty one's.
    const std::uint64_t rounds = rp::device_check::kBenchRounds;
    const std::uint64_t empty = b.emptyCycles / rounds, notes = b.noteCycles / rounds;
    const std::uint64_t sysex = b.sysexCycles / rounds;
    const std::uint64_t perNote = notes > empty && b.noteMessages ? (notes - empty) / b.noteMessages : 0;
    const std::uint64_t perSysex = sysex > empty && b.sysexMessages ? (sysex - empty) / b.sysexMessages : 0;
    std::printf("rp-device-check: script bench pass, %s at %u MHz, %u rounds: the first block %u cycles (%u us), an "
                "empty block %u (%u us), %u per note (%u us) over %u to a block, %u per 64-byte SysEx (%u us) over %u "
                "to a block, %u messages passed; heap %u of %u bytes in use; XIP cache %u hits of %u reads\n",
                rp::device_check::kBenchType, static_cast<unsigned>(gCyclesPerMicro), static_cast<unsigned>(rounds),
                static_cast<unsigned>(b.firstCycles), micros(b.firstCycles), static_cast<unsigned>(empty),
                micros(empty), static_cast<unsigned>(perNote), micros(perNote), static_cast<unsigned>(b.noteMessages),
                static_cast<unsigned>(perSysex), micros(perSysex), static_cast<unsigned>(b.sysexMessages),
                static_cast<unsigned>(b.passed), static_cast<unsigned>(b.inUseBytes),
                static_cast<unsigned>(b.contextBytes), xipHits, xipReads);
}

const char* loopStepName(rp::device_check::LoopStep step) {
    switch (step) {
        case rp::device_check::LoopStep::Ok: return "ok";
        case rp::device_check::LoopStep::Type: return "type";
        case rp::device_check::LoopStep::Package: return "package";
        case rp::device_check::LoopStep::Create: return "create";
        case rp::device_check::LoopStep::Activate: return "activate";
        case rp::device_check::LoopStep::Ports: return "ports";
        case rp::device_check::LoopStep::Result: return "result";
    }
    return "?";
}

// The deadline case: the image's script pack registered again with the box's pack's heap (a registration of its own,
// keyed on this copy of its source), its loops run in the arena.
rp_mqjs_pack_source gLoopSource;

RP_NOINLINE void runLoops() {
    gLoopSource = rp_script_devtest;
    gLoopSource.heap_bytes = rp::device_check::kLoopHeapBytes;
    rp::device_check::loopDeadline(rp::mqjs::package(gLoopSource), rp::device_check::kCases,
                                   rp::device_check::kCaseCount, gArena, sizeof gArena, &loopMicros, &gLoops);
}

// The deadline case's worst loop: the one whose last block took longest.
std::uint32_t worstLoop() {
    std::uint32_t worst = 0;
    for (std::uint32_t k = 1; k < rp::device_check::kLoopCount; ++k)
        if (gLoops.micros[k] > gLoops.micros[worst]) worst = k;
    return worst;
}

// Its line, the worst judged against kLoopBoundMicros: "pass" when every loop ended in its deadline's fault within it.
RP_NOINLINE void printLoops() {
    const rp::device_check::LoopResult& l = gLoops;
    if (l.step != rp::device_check::LoopStep::Ok) {
        std::printf("rp-device-check: script deadline failed, step %u (%s) at %s of %u: %s\n",
                    static_cast<unsigned>(l.step), loopStepName(l.step), rp::device_check::kLoops[l.loop].loop,
                    static_cast<unsigned>(rp::device_check::kLoops[l.loop].count), l.error.message);
        return;
    }
    const std::uint32_t worst = worstLoop();
    const bool          fits = l.micros[worst] <= rp::device_check::kLoopBoundMicros;
    std::uint32_t entered = 0;
    for (std::uint32_t k = 0; k < rp::device_check::kLoopCount; ++k) entered += l.entered[k] ? 1 : 0;
    std::printf("rp-device-check: script deadline %s, %s in a context of %u bytes (the box's pack's heap), %u of %u "
                "loops entered and each faulted by its deadline of %u us:",
                fits ? "pass" : "failed", rp::device_check::kLoopType, static_cast<unsigned>(l.contextBytes),
                static_cast<unsigned>(entered), static_cast<unsigned>(rp::device_check::kLoopCount),
                static_cast<unsigned>(rp::mqjs::kHostClockDeadlineMicros));
    for (std::uint32_t k = 0; k < rp::device_check::kLoopCount; ++k)
        std::printf("%s %s of %u%s in %u us (%u blocks)", k ? "," : "", rp::device_check::kLoops[k].loop,
                    static_cast<unsigned>(rp::device_check::kLoops[k].count),
                    rp::device_check::kLoops[k].warm ? " after plain code" : "", static_cast<unsigned>(l.micros[k]),
                    static_cast<unsigned>(l.blocks[k]));
    std::printf("; the worst, %s, %u us of its block, %s the bound of %u us (a third of the box's 3 s watchdog)\n",
                rp::device_check::kLoops[worst].loop, static_cast<unsigned>(l.micros[worst]),
                fits ? "within" : "over", static_cast<unsigned>(rp::device_check::kLoopBoundMicros));
}

const char* policyStepName(rp::device_check::PolicyStep step) {
    switch (step) {
        case rp::device_check::PolicyStep::Ok: return "ok";
        case rp::device_check::PolicyStep::Type: return "type";
        case rp::device_check::PolicyStep::Package: return "package";
        case rp::device_check::PolicyStep::Create: return "create";
        case rp::device_check::PolicyStep::Activate: return "activate";
        case rp::device_check::PolicyStep::Ports: return "ports";
        case rp::device_check::PolicyStep::Process: return "process";
        case rp::device_check::PolicyStep::Result: return "result";
    }
    return "?";
}

// The policy case: the bliptoaster pack registered at kPolicyHeapBytes (a registration of its own, keyed on this copy
// of its source), its policy run in the arena over the blocks that fill its heap most.
rp_mqjs_pack_source gPolicySource;

RP_NOINLINE void runPolicy() {
    gPolicySource = rp_script_bliptoaster;
    gPolicySource.heap_bytes = rp::device_check::kPolicyHeapBytes;
    rp::device_check::policyHeap(rp::mqjs::package(gPolicySource), gArena, sizeof gArena, &gPolicy);
}

RP_NOINLINE void printPolicy() {
    const rp::device_check::PolicyResult& p = gPolicy;
    if (p.step != rp::device_check::PolicyStep::Ok) {
        std::printf("rp-device-check: script policy heap failed, step %u (%s) after %u blocks, in a context of %u "
                    "bytes (the box's %u less %u): %s\n",
                    static_cast<unsigned>(p.step), policyStepName(p.step), static_cast<unsigned>(p.blocks),
                    static_cast<unsigned>(rp::device_check::kPolicyHeapBytes),
                    static_cast<unsigned>(rp::device_check::kPolicyBoxHeapBytes),
                    static_cast<unsigned>(rp::device_check::kPolicyMarginBytes), p.error.message);
        return;
    }
    std::printf("rp-device-check: script policy heap pass, %s in a context of %u bytes (the box's %u less %u): %u "
                "blocks of notes, SysEx after notes at each place, 63 SysEx of 64 bytes then one of 4096, and full "
                "queues of SysEx, %u messages, %u passed; heap %u bytes in use after the first block and %u after "
                "the last\n",
                rp::device_check::kPolicyType, static_cast<unsigned>(p.contextBytes),
                static_cast<unsigned>(rp::device_check::kPolicyBoxHeapBytes),
                static_cast<unsigned>(rp::device_check::kPolicyMarginBytes), static_cast<unsigned>(p.blocks),
                static_cast<unsigned>(p.messages), static_cast<unsigned>(p.passed),
                static_cast<unsigned>(p.firstInUseBytes), static_cast<unsigned>(p.inUseBytes));
}

// The script plan's stack, in two phases: the load (the pack's setup among it), read as the plan has loaded, which
// paints the stack again below its own frame; then the blocks and the unload, read after.
std::size_t gLoadStack = 0;

void afterScriptPlanLoad() {
    gLoadStack = stackHighWater();
    paintStack();
}

RP_NOINLINE void runScriptPlan() {
    rp::device_check::runPlan(kScriptPlan, gPackages, gPackageCount, gArena, sizeof gArena, &gScriptPlan);
}

// The stack judged against its bound with kStackReserve kept: the guard faults a push past the stack's bottom, so a
// line at all says a run fitted, and what a box's interrupts take on top of a block (or of its boot's load) must come
// from what is left. "pass" for each phase that leaves at least the reserve.
const char* fits(std::size_t stack, std::size_t stackSize) {
    return stack + kStackReserve <= stackSize ? "pass" : "failed";
}

RP_NOINLINE void printScriptPlan(std::size_t stack, std::size_t stackSize) {
    const rp::device_check::PlanResult& sp = gScriptPlan;
    if (sp.step == rp::device_check::PlanStep::Ok)
        std::printf("rp-device-check: script plan pass, %u nodes, %u arena bytes (the export's estimate %u), %u Hz, "
                    "%u frames a block: loaded in %u us, the first block %u us, ran %u frames in %u blocks with %u "
                    "messages and %u drops, unloaded; digest %08x%08x over %u blocks, the desktop's\n",
                    static_cast<unsigned>(sp.nodes), static_cast<unsigned>(sp.arenaBytes),
                    static_cast<unsigned>(kDeviceCheckScriptPlanArenaEstimate), static_cast<unsigned>(sp.sampleRate),
                    static_cast<unsigned>(sp.maxFrames), micros(sp.loadCycles), micros(sp.firstBlockCycles),
                    static_cast<unsigned>(sp.frames), static_cast<unsigned>(sp.blocks),
                    static_cast<unsigned>(sp.messages), static_cast<unsigned>(sp.drops),
                    static_cast<unsigned>(sp.digest >> 32), static_cast<unsigned>(sp.digest),
                    static_cast<unsigned>(sp.digestBlocks));
    else
        std::printf("rp-device-check: script plan failed, step %u (%s) at node %s after %u blocks: %s%s%s (%u arena "
                    "bytes, the export's estimate %u; digest %08x%08x, the desktop's %08x%08x)\n",
                    static_cast<unsigned>(sp.step), planStepName(sp.step), sp.error.node,
                    static_cast<unsigned>(sp.blocks), sp.error.code, sp.error.code[0] ? ": " : "", sp.error.message,
                    static_cast<unsigned>(sp.arenaBytes), static_cast<unsigned>(kDeviceCheckScriptPlanArenaEstimate),
                    static_cast<unsigned>(sp.digest >> 32), static_cast<unsigned>(sp.digest),
                    static_cast<unsigned>(rp::device_check::kScriptPlanWant >> 32),
                    static_cast<unsigned>(rp::device_check::kScriptPlanWant));
    for (std::uint32_t i = 0; i < sp.packCount; ++i)
        std::printf("rp-device-check: script pack %s, %u bytes (the host's state %u of %u reserved, the bytecode's "
                    "copy and the context): heap %u of %u bytes in use after the run, MicroQuickJS's own %u\n",
                    sp.packs[i].name, static_cast<unsigned>(sp.packs[i].memoryBytes),
                    static_cast<unsigned>(rp::mqjs::stateBytes()), static_cast<unsigned>(rp::mqjs::kBoxStateBytes),
                    static_cast<unsigned>(sp.packs[i].inUseBytes), static_cast<unsigned>(sp.packs[i].contextBytes),
                    static_cast<unsigned>(sp.packs[i].fixedBytes));
    std::printf("rp-device-check: script plan stack: the load %s, %u of %u bytes at most, %u left; the blocks %s, %u "
                "bytes at most, %u left, with %u kept for interrupts\n",
                fits(gLoadStack, stackSize), static_cast<unsigned>(gLoadStack), static_cast<unsigned>(stackSize),
                static_cast<unsigned>(stackSize - gLoadStack), fits(stack, stackSize), static_cast<unsigned>(stack),
                static_cast<unsigned>(stackSize - stack), static_cast<unsigned>(kStackReserve));
}

}  // namespace

int main() {
    paintStack();
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    startCycles();
    // Every script node's deadline, the per-type runs' included, by the device's clock (the box's 10 ms).
    rp::device_check::offerClock(&kClock);
    gPackages = rp_register_packages(&gPackageCount);

    runTypes();
    printTypes();
    runN8Protocol();
    printPlan(runPlanCheck());
    runBench();
    runLoops();
    printLoops();
    runPolicy();
    printPolicy();

    // The script plan last, on a stack painted afresh, so its line says the depths its run alone took: the load (the
    // pack's setup), read and painted again as it has loaded (afterScriptPlanLoad), then every block with the nest
    // node at the host's deepest C re-entry, and the unload. A run that overran the stack would stop the image there
    // (the guard's HardFault), after every other line.
    const std::size_t stackSize = static_cast<std::size_t>(__StackTop - __StackBottom);
    const std::size_t before = stackHighWater();
    paintStack();
    runScriptPlan();
    const std::size_t scriptStack = stackHighWater();
    printScriptPlan(scriptStack, stackSize);

    // Every check's deepest stack, their lines' printf included, measured over the paint main laid at boot and the
    // ones laid again for the script plan's load and its blocks.
    std::size_t deepest = stackHighWater();
    if (before > deepest) deepest = before;
    if (gLoadStack > deepest) deepest = gLoadStack;
    std::printf("rp-device-check: stack %u of %u bytes at most\n", static_cast<unsigned>(deepest),
                static_cast<unsigned>(stackSize));

    const rp::device_check::PlanResult&  p = gDevicePlan;
    const rp::device_check::PlanResult&  sp = gScriptPlan;
    const rp::device_check::BenchResult& b = gScriptBench;
    const rp::device_check::LoopResult&  l = gLoops;
    const rp::device_check::PolicyResult& pol = gPolicy;
    unsigned blinks = static_cast<unsigned>(gDeviceCheck.step);
    if (blinks == 0 && p.step != rp::device_check::PlanStep::Ok) blinks = kPlanBlinks + static_cast<unsigned>(p.step);
    if (blinks == 0 && !gN8Protocol.ok) blinks = kN8ProtocolBlinks;
    if (blinks == 0 && sp.step != rp::device_check::PlanStep::Ok)
        blinks = kScriptPlanBlinks + static_cast<unsigned>(sp.step);
    if (blinks == 0 && b.step != rp::device_check::BenchStep::Ok) blinks = kBenchBlinks + static_cast<unsigned>(b.step);
    if (blinks == 0 && l.step != rp::device_check::LoopStep::Ok) blinks = kLoopBlinks + static_cast<unsigned>(l.step);
    if (blinks == 0 && l.micros[worstLoop()] > rp::device_check::kLoopBoundMicros)
        blinks = kLoopBlinks + kLoopBoundBlinks;
    if (blinks == 0 && pol.step != rp::device_check::PolicyStep::Ok)
        blinks = kPolicyBlinks + static_cast<unsigned>(pol.step);
    if (blinks == 0 && (gLoadStack + kStackReserve > stackSize || scriptStack + kStackReserve > stackSize))
        blinks = kScriptPlanBlinks + kScriptStackBlinks;
    for (;;) {
        if (blinks == 0) {
            led(true);
            busy_wait_ms(1000);
            continue;
        }
        for (unsigned i = 0; i < blinks; ++i) {
            led(true);
            busy_wait_ms(200);
            led(false);
            busy_wait_ms(200);
        }
        busy_wait_ms(1000);
    }
}
