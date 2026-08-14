# Pico firmware - standalone N8 bridge

Firmware for a small RP2040 box that drives a real NES + Everdrive N8 Pro with **no
computer in the loop**. It closes the remaining control gaps the USB toolkit can't
reach on its own (physical joypad input, a hardware MIDI port) and folds in the
existing `n8-bridge` MIDI->N8 path.

Three independent stages, built and verified one at a time:

| Stage | Folder | What it does | Status |
|-------|--------|--------------|--------|
| 1. MIDI IN | [`midi-in/`](midi-in/) | Hardware MIDI TRS jack -> opto -> Pico UART | built + buildable |
| 2. N8 USB host | `n8-host/` (todo) | Pico as USB host (PIO-USB) speaking the Edio protocol to the N8 | planned |
| 3. Controller inject | `controller/` (todo) | Pico driving the NES controller ports (PIO) | planned |

Target split: **two Picos** - one dedicated to the USB host (PIO-heavy), one for
MIDI + controller. They can coordinate over UART/I2C.

## Prerequisites

- `gcc-arm-none-eabi` (ARM bare-metal toolchain)
- The [Pico SDK](https://github.com/raspberrypi/pico-sdk), pointed to by `PICO_SDK_PATH`

```sh
sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
git clone --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk.git /workspaces/pico-sdk
export PICO_SDK_PATH=/workspaces/pico-sdk
```

Each stage is a self-contained SDK project - `cd` into it and see its README.
