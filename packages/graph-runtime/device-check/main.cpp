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
// It reports the outcome three ways: one line on stdio (the UART, GP0 at 115200) for each check, gDeviceCheck,
// gN8Protocol and gDevicePlan for a debugger, and the board's LED, lit and steady for a pass and for a failure a count
// of blinks, repeated after a pause: the failed Step's value for a type, kPlanBlinks plus the PlanStep's for the plan,
// kN8ProtocolBlinks for the protocol. The plan's line reports its node count and the arena it took, as the device
// measured them, beside the exporter's estimate, which the check holds it to, and the digest of what reached its end
// beside the desktop's (DeviceCheck.hpp's kPlanDesktopDigest), which it does not judge: the float tolerance between the
// two is the audio version's. A fourth line gives the deepest the stack went (stackHighWater below).
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
#include "N8Protocol.hpp"
#include "graph/Packages.hpp"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "pico/stdio.h"

// The cases were generated for one list of package entries, and this image registers its product's: they must be the
// same list, or a type would meet a case made for another (CMakeLists.txt passes the product's entries).
static_assert(std::string_view(RP_DEVICE_CHECK_ENTRIES) == rp::device_check::kEntries,
              "DeviceCheckCases.hpp was generated for other package entries than this image registers: regenerate it "
              "with rp-device-check-cases from the same packages.cmake (tools/build-device-check.sh)");

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

// The blinks a plan failure shows when the types passed, plus its PlanStep's value, and a protocol failure when both
// passed: past every Step's value.
constexpr unsigned kPlanBlinks = 10;
constexpr unsigned kN8ProtocolBlinks = 20;

const char* planStepName(rp::device_check::PlanStep step) {
    switch (step) {
        case rp::device_check::PlanStep::Ok: return "ok";
        case rp::device_check::PlanStep::Load: return "load";
        case rp::device_check::PlanStep::Move: return "move";
        case rp::device_check::PlanStep::Process: return "process";
        case rp::device_check::PlanStep::Result: return "result";
        case rp::device_check::PlanStep::Estimate: return "estimate";
        case rp::device_check::PlanStep::Case: return "case";
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
rp::device_check::Result        gDeviceCheck;
rp::device_check::N8ProtocolResult gN8Protocol;
rp::device_check::PlanResult       gDevicePlan;

int main() {
    paintStack();
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    std::size_t                    count = 0;
    const rp_package* const* const packages = rp_register_packages(&count);
    gDeviceCheck = rp::device_check::run(packages, count, rp::device_check::kCases, rp::device_check::kCaseCount,
                                         gArena, sizeof gArena, rp::device_check::kBlockFrames);
    gDeviceCheckStep = gDeviceCheck.step;
    if (gDeviceCheck.step == rp::device_check::Step::Ok)
        std::printf("rp-device-check: pass, %u types, arena %u bytes\n", static_cast<unsigned>(gDeviceCheck.types),
                    static_cast<unsigned>(gDeviceCheck.arenaBytes));
    else
        std::printf("rp-device-check: step %u at type %u (%s): %s\n", static_cast<unsigned>(gDeviceCheck.step),
                    static_cast<unsigned>(gDeviceCheck.type), gDeviceCheck.id ? gDeviceCheck.id : "-",
                    gDeviceCheck.error.message);

    gN8Protocol = rp::device_check::n8Protocol();
    if (gN8Protocol.ok)
        std::printf("rp-device-check: n8 protocol pass, %u bytes\n", static_cast<unsigned>(gN8Protocol.written));
    else
        std::printf("rp-device-check: n8 protocol failed at %s (%u bytes, hash %08x)\n", gN8Protocol.failed,
                    static_cast<unsigned>(gN8Protocol.written), static_cast<unsigned>(gN8Protocol.hash));

    // The plan, in the arena the types used, over the same registration.
    const std::uint32_t start = time_us_32();
    rp::device_check::runPlan(kPlan, packages, count, gArena, sizeof gArena, &gDevicePlan);
    const std::uint32_t took = time_us_32() - start;
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

    // Every check's deepest stack, their lines' printf included, measured over the paint main laid at boot.
    std::printf("rp-device-check: stack %u of %u bytes at most\n", static_cast<unsigned>(stackHighWater()),
                static_cast<unsigned>(__StackTop - __StackBottom));

    unsigned blinks = static_cast<unsigned>(gDeviceCheck.step);
    if (blinks == 0 && p.step != rp::device_check::PlanStep::Ok) blinks = kPlanBlinks + static_cast<unsigned>(p.step);
    if (blinks == 0 && !gN8Protocol.ok) blinks = kN8ProtocolBlinks;
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
