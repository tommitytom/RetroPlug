# Pico firmware - standalone N8 bridge

Firmware for a small Pico box that drives a real NES + Everdrive N8 Pro with **no
computer in the loop**. The board in use is a **Pico 2 (RP2350)** - stage 2's likely unblock is
the RP2350's native USB host, which an RP2040 does not have. It closes the remaining control gaps the USB toolkit can't
reach on its own (physical joypad input, a hardware MIDI port) and folds in the
existing `n8-bridge` MIDI->N8 path.

Three independent stages, built and verified one at a time:

| Stage | Folder | What it does | Status |
|-------|--------|--------------|--------|
| 1. MIDI IN | [`midi-in/`](midi-in/) | Hardware MIDI TRS jack -> opto -> Pico UART | built + buildable |
| 2. N8 USB host | [`n8-host/`](n8-host/) | Pico as USB host speaking the Edio protocol to the N8, in C++ on the N8 pack (`packages/n8`), the Edio the desktop shares | **done, HW-verified**: the Pico boots a ROM through the N8 menu and forwards MIDI into its FIFO, no PC ([the months-long FIFO "bug"](pico-n8-fifo-write-bug.md)) |
| 3. Controller inject | `controller/` (todo) | Pico driving the NES controller ports (PIO) | planned |

Since E3 the box is a graph host ([docs/modular-graph-phase-e3-design.md](../docs/modular-graph-phase-e3-design.md)):
[`host/`](host/) is the generic part (the block driver, the UART MIDI staging, the device `rp_host`, the load of the
built-in plan) and [`n8-host/`](n8-host/) the N8 product, whose MIDI bridge is `n8-host/plan.json` (MIDI in, a filter,
the N8 node) exported by `retroplug-export-plan` when the firmware is built and loaded into a static arena at boot.
Build the exporter first (`cmake --build build --target retroplug-export-plan-current -j$(nproc)`); `pnpm device:box` does.

Target split: **two Picos** - one dedicated to the USB host (PIO-heavy), one for
MIDI + controller. They can coordinate over UART/I2C. (Stage 2's own README notes that one Pico can
in fact carry both MIDI-in and the N8 host, since the UART and PIO do not contend; the split is
about headroom, not necessity.)

## Prerequisites

**In the devcontainer these are already baked in** - the ARM toolchain
(`gcc-arm-none-eabi`), the [Pico SDK](https://github.com/raspberrypi/pico-sdk) at
`/opt/pico-sdk` (with `PICO_SDK_PATH` set for every shell), and a system `picotool`
(so builds don't re-fetch it). Nothing to install; just build a stage.

On a bare host (no devcontainer):

```sh
sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib libusb-1.0-0-dev
git clone --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
export PICO_SDK_PATH=/opt/pico-sdk
```

Each stage is a self-contained SDK project - `cd` into it and see its README. Stage 2 is the exception in one way: it
builds the repository's `core`, `device-io` and `n8` packages in device mode over `node-sdk` and `graph-runtime`, so its
configure needs reflect-cpp's headers (the desktop build's checkout, or a clone) and the desktop's
`retroplug-export-plan` and it links under the link trap on the heap, on exceptions and on RTTI; CI builds
it with `pnpm device:box`.
