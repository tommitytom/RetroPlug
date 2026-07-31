# linksync — RetroPlug as a hardware LSDj sync bridge

`retroplug-cli linksync` turns RetroPlug into the host-side tempo brain for a
[ModRetro Chromatic](https://github.com/peterswimm/oss-chromatic-console-fpga) (or any
Game Boy reachable over its link port). It runs the **same** `lsdj-sync` clock the plugin uses
and streams the resulting LSDj serial bytes to the console as commands the Chromatic firmware
injects onto the Game Boy link — so a DAW / Ableton Link session drives LSDj tempo on real
hardware, the way RetroPlug drives an emulated Game Boy in a DAW.

This is the host half of the RetroPlug → Chromatic port. See the FPGA repo's
`docs/retroplug-port/` and the MCU repo's `docs/retroplug-sync.md` for the device halves.

## How it reuses the plugin's clock

The drift-exact 24-PPQN tick walker `walkTicks` was extracted to
[`src/ppqClock.ts`](../packages/retroplug/src/ppqClock.ts) (dependency-free) and is imported by
both the DSP kernel's `lsdj-sync` role ([`dspKernel.ts`](../packages/retroplug/src/dspKernel.ts) /
[`dspRoles.ts`](../packages/retroplug/src/dspRoles.ts)) and the bridge
([`cli/sessions/linksyncBridge.ts`](../packages/retroplug/cli/sessions/linksyncBridge.ts)). Because
the clock is one source of truth, the hardware byte stream matches an in-plugin render **by
construction** — the plugin's sync tests are the bridge's golden vector.

## Usage

```
retroplug-cli linksync --bpm 120 --duration 4s --out sync.txt
cat sync.txt > /dev/ttyACM0        # stream the commands to the Chromatic
```

Flags: `--bpm`, `--divisor` (1/2/4/8), `--mode` (midiSync | arduinoboy), `--duration`,
`--block-ms`, `--auto-start`, `--sample-rate`, `--out`. It emits `rpsync <mode> <byte…>` lines
(and a `poke` for Start when `--auto-start` arms a SYNC=MIDI cart) — the exact console commands
the Chromatic MCU firmware consumes.

## Live Ableton Link (future)

The PoC generates a command stream for a fixed `--bpm`. A live daemon that follows an Ableton
Link session and streams to `/dev/ttyACM0` in real time needs native serial + Link-SDK adapters
the txiki CLI runtime doesn't have — see `docs/retroplug-port/ableton-link.md` in the FPGA repo.
The clock math and command framing are already the shared, tested core; only the transport
source and the serial writer change.

## Tests

The bridge core is pure and Node-runnable (no build required):

```
node --test packages/retroplug/cli/sessions/linksyncBridge.test.ts \
            packages/retroplug/cli/sessions/linksync.test.ts
```
