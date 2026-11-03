// rp-device-check (docs/modular-graph-phase-e1-design.md section 7): the RP2350 image that runs the device check over
// every type its product registers (CMakeLists.txt: packages.cmake's list, as a device builds it), and the proof, at
// link time, that what it links allocates and throws nothing: the image links with a trap on the heap, on exceptions
// and on RTTI (CMakeLists.txt), so a use of any of them anywhere in its code is a link error naming the object that
// pulled it in.
//
// It reports the outcome three ways: one line on stdio (the UART, GP0 at 115200), gDeviceCheck for a debugger, and the
// board's LED, lit and steady for a pass and for a failure a count of blinks, the failed Step's value, repeated after
// a pause. E3 runs this check's successor on the rig; E1 builds it and runs nothing.

#include <cstddef>
#include <cstdio>
#include <string_view>

#include "DeviceCheck.hpp"
#include "DeviceCheckCases.hpp"
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

namespace {

alignas(16) std::byte gArena[rp::device_check::kArenaBytes];

void led(bool on) { gpio_put(PICO_DEFAULT_LED_PIN, on); }

}  // namespace

// The outcome, where a debugger finds it.
volatile rp::device_check::Step gDeviceCheckStep = rp::device_check::Step::Ok;
rp::device_check::Result        gDeviceCheck;

int main() {
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

    const unsigned blinks = static_cast<unsigned>(gDeviceCheck.step);
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
