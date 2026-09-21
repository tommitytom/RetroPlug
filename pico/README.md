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
| 2. N8 USB host | [`n8-host/`](n8-host/) | Pico as USB host speaking the Edio protocol to the N8 | slices 2.1 + 2.2 HW-verified; **2.3 blocked** on a PIO-USB FIFO-write limitation ([details](pico-n8-fifo-write-bug.md)) |
| 3. Controller inject | `controller/` (todo) | Pico driving the NES controller ports (PIO) | planned |

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

Each stage is a self-contained SDK project - `cd` into it and see its README.
