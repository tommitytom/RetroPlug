# Plan: drive a real Everdrive N8 Pro from RetroPlug (MIDI in -> USB serial)

Status: plan / spike complete
Goal: take MIDI into RetroPlug and forward it over USB to a physical Everdrive N8 Pro running the
BlipToaster NES ROM, so a MIDI controller / DAW plays the real NES. One-way (host -> cart); BlipToaster is
MIDI-in only.

---

## 1. Summary

The hard part (the host <-> N8-Pro USB protocol) is fully solved and validated: the `ecs-linux` branch has a
working C++ `Edio` client the author already used against a real N8 Pro. Almost everything else already
exists in RetroPlug (MIDI-in path, a lock-free audio->control ring, and the file-watcher pattern for a
host-side blocking-I/O thread). This plan ports that reference onto the current build in two deliverables:

1. **CLI bridge** (`retroplug-cli n8-bridge`) - a thin MIDI-port -> serial-port pipe, no emulator, lowest
   latency. Fastest to validate against hardware.
2. **Standalone / plugin bridge** - a host-side serial thread fed by the audio thread, with a timed
   scheduler that preserves note/clock timing, plus the emulated BlipToaster as a live on-screen preview.

---

## 2. Spike result: the N8 Pro USB protocol (known + proven)

Reference (read via `git show ecs-linux:<path>`): `src/mesen/Edio.{h,cpp}` (host protocol),
`src/mesen/EdioProxy.h` (host thread + queue), `thirdparty/serial/` (vendored wjwwood/serial),
`src/core/EverdriveComponents.h`.

The N8 Pro USB is **krikzz's Edio command protocol over a CDC serial port** - NOT raw byte passthrough.

- **Device**: opens as a plain serial port at 9600 baud (baud is irrelevant on the FT245-class device),
  `Timeout::simpleTimeout(300)` at connect then `2000` ms after handshake.
- **Auto-detect** by USB id: **VID:PID = `38df:0017`**
  (Windows hwid `USB\VID_38DF&PID_0017&REV_0200`, Linux `USB VID:PID=38df:0017`). This is krikzz's own id,
  NOT the old-N8 FTDI `0403:6001`.
- **Command frame** (`txCMD`): 4 bytes `'+' , '+'^0xFF , cmd , cmd^0xFF`. All multi-byte args little-endian
  (`tx32`/`tx16`/`tx8`).
- **Connect handshake**: `CMD_STATUS (0x10)` -> `rx16()`; high byte must be `0xA5` (`0xA5xx`), low byte is a
  status code (0 = ok).
- **MIDI delivery** (the one op the bridge needs): `fifoWR(bytes)` = `memWR(ADDR_FIFO, bytes)`:
  ```
  txCMD(CMD_MEM_WR = 0x1A)
  tx32(addr = ADDR_FIFO = 0x1810000)
  tx32(len)
  tx8(0)            // "exec"
  txData(bytes)     // raw MIDI bytes
  ```
  `memWR` does **not** round-trip (fire-and-forget), so a 3-byte MIDI message is ~16 bytes over USB,
  sub-millisecond. Bytes land at the cart FIFO the ROM polls at `$40F0` (data) / `$40F1` (status bit 7);
  BlipToaster reads them with a standard running-status MIDI parser.

Constants worth lifting verbatim from `ecs-linux:src/mesen/Edio.h`:

| name | value | use |
|---|---|---|
| `ADDR_FIFO` | `0x1810000` | MIDI write target (via `CMD_MEM_WR`) |
| `CMD_STATUS` | `0x10` | connect handshake |
| `CMD_MEM_WR` | `0x1A` | write bytes to a device address |
| frame | `'+', '+'^0xFF, cmd, cmd^0xFF` | every command |
| VID:PID | `38df:0017` | port auto-detect |

There is also a dedicated `CMD_FIFO_WR (0x23)` in the enum, but the proven path uses `CMD_MEM_WR` ->
`ADDR_FIFO`. Keep using the proven path.

Oracle for validation: the emulator already models this FIFO in
`packages/native/src/system/mesen/NesEverdriveFifo.hpp`; `RP_FIFO_TRACE=1` logs every byte the ROM reads.

---

## 3. Timing model (preserve note/clock accuracy)

Firing MIDI at the hardware the instant the audio thread produces it yields block-quantized jitter (up to one
audio buffer). Instead, the **serial thread is a timed scheduler**: a small fixed lookahead buys accurate
relative timing (the right trade - a little constant latency over sloppy note/sync timing).

- The producer stamps each message with a target release time:
  `target = blockStartClock + sampleOffsetInBlock / sampleRate + lookahead`.
  The sample offset already exists - `NesN8FifoRole::pumpUntil` computes it for the emulated FIFO
  (see [MIDI-in sample timing](packages/native/src/system/mesen/roles/NesN8FifoRole.hpp)).
- The serial thread holds a time-ordered queue, `sleep_until(target)` (with a short sub-ms spin for the
  tail), then `fifoWR`s. One clock domain (`steady_clock`) end to end.
- `lookahead` ~5-15 ms absorbs the block period + USB/OS jitter; because it is constant, note spacing and
  24-PPQN clock ticks stay tight. Expose it as a setting.

The CLI pure-pipe path barely needs this (the MIDI input's own timing is the source of truth), but it is the
same mechanism, opt-in via a `--lookahead-ms` flag.

---

## 4. Architecture

```
                        ┌─ emulated BlipToaster (on-screen preview)   [standalone/plugin only]
MIDI in ──> routing ──> NesN8FifoRole ──┤
 (RtMidi / DAW)          (audio thread)  └─ tap ──> SpscRing<TimedEdioCmd> ──> serial thread (scheduler)
                                                                                        │ fifoWR
                                                                              libserialport ──> N8 Pro ──> NES
```

- **`Edio`** (ported from `ecs-linux:src/mesen/Edio.{h,cpp}`): the protocol client. Library-agnostic except
  ~10 serial calls; swap `serial::` for libserialport (`sp_open` / `sp_blocking_write` / `sp_blocking_read` /
  `sp_list_ports` / `sp_get_port_usb_vid_pid`). Keep `findN8Port()` matching `38df:0017`.
- **Serial thread** (`EdioProxy`-style, from `ecs-linux:src/mesen/EdioProxy.h`): a `std::jthread` owning the
  `Edio` connection, fed by a lock-free SPSC queue of `{ targetTime, bytes[<=8], size }`. Clean stop (flag +
  sentinel enqueue + join). Model teardown on
  [NativeFileWatcher](packages/native/src/host/rpc/NativeFileWatcher.hpp) (thread-owning member declared
  LAST). This becomes the timed scheduler from section 3.
- **Audio->serial tap**: a new audio->control `SpscRing` (mirror `released_` in
  [EngineInvoker](packages/native/src/host/engine/EngineInvoker.hpp)), or reuse moodycamel as the reference
  did. A DSP sink alongside `NesN8FifoRole` pushes the same routed MIDI (with sample offset) to the ring.
- **CLI pure-pipe**: no emulator, no audio thread - RtMidi callback -> (optional scheduler) -> `edio.fifoWR`.

---

## 5. Library

**libserialport** (chosen). Mature/stable (quiet upstream, but serial APIs barely change); built-in port
enumeration incl. USB VID/PID (nice for the device picker). The spike de-risks this: the *protocol* is
proven, and `Edio`'s serial surface is tiny and isolated, so the lib is a thin swap. **wjwwood/serial** is
the zero-risk fallback (the vendored `ecs-linux` copy already talks to the N8). Vendor as a submodule like
efsw. RtMidi (for the CLI's MIDI-in) is already inside the DPF submodule and easy to vendor.

---

## 6. UX

**CLI** (deliverable 1):
```
retroplug-cli n8-bridge [--midi-in <port>] [--serial <port>] [--lookahead-ms N] [--list]
```
- `--list` enumerates MIDI inputs + serial ports (auto-tagging the `38df:0017` N8).
- No args: auto-pick the single MIDI input + the N8 port; print what it chose; pipe until Ctrl-C; show a live
  byte/message counter + connection status.

**Standalone** (deliverable 2): a "MIDI -> N8" panel - MIDI input picker (already have via RtMidi), N8 serial
port dropdown (auto-suggest `38df:0017`), Connect toggle, status + byte counter, lookahead-latency slider,
and an optional "run emulated BlipToaster as monitor" toggle so the on-screen core mirrors the real cart.

**Plugin** (later): a `System > Send to N8 Pro...` menu -> pick port -> that system's routed MIDI also
streams to the cart; status in the tile/menu.

---

## 7. Task breakdown

### Deliverable 1 - CLI bridge (validate the port against hardware)
1. Vendor `libserialport` + `RtMidi` into the CMake build (submodules, like efsw/catch2).
2. Port `Edio.{h,cpp}` from `ecs-linux` onto libserialport (protocol unchanged; adapt the ~10 serial calls;
   keep `findN8Port` on `38df:0017`; keep the `CMD_STATUS` handshake).
3. New `retroplug-cli n8-bridge` subcommand in [cli/main.cpp](packages/native/cli/main.cpp) (same dispatch
   seam as `render`): open RtMidi in + `Edio`, pipe in the MIDI callback, optional `--lookahead-ms`
   scheduler, `--list`.
4. Validate: `RP_FIFO_TRACE` parity against the emulator + a real N8 Pro. A mock/loopback serial for CI.

### Deliverable 2 - standalone / plugin bridge
5. `EdioProxy`-style host serial thread + timed scheduler (section 3), fed by a new audio->control
   `SpscRing<TimedEdioCmd>`.
6. A DSP sink that taps the routed N8 MIDI (with sample offset) alongside `NesN8FifoRole`.
7. Host facet + TS wiring (enable/listPorts/connect/disconnect/drainStatus) + realBackend + mock.
8. Standalone panel UX (pickers, connect, status, lookahead slider, preview toggle). Plugin menu after.

---

## 8. Risks / notes

- **Hardware-only validation.** The protocol is proven, but the current-build port + libserialport swap need
  a real N8 Pro to confirm end to end (the CLI bridge exists to make that a 5-minute test).
- **Cross-platform serial.** N8 Pro presents a CDC port; macOS/Linux drivers are built in, Windows is
  usually plug-and-play. Port naming differs per OS - enumeration + `38df:0017` matching handles it.
- **CLI needs a new MIDI-in dep.** The CLI has no live MIDI today (only harness `stageMidiIn`); RtMidi is the
  addition.
- **One-way only.** BlipToaster only reads MIDI, so no return path is needed. (The `Edio` client can read
  status, used for the connect handshake / health only.)
- **Don't over-batch.** `memWR` is fire-and-forget; write per message (or per due-time in the scheduler) to
  keep latency low. The 2048-byte cart FIFO buffers bursts.

---

## 9. Reference map

Reuse from `ecs-linux` (old premake/`src` layout):
- `src/mesen/Edio.h` / `Edio.cpp` - the protocol client (port this).
- `src/mesen/EdioProxy.h` - the host thread + lock-free queue pattern (port this for deliverable 2).
- `src/core/EverdriveComponents.h` - the component holder.
- `thirdparty/serial/` - the proven wjwwood/serial fallback.

Target integration points (current `packages/native` build):
- `cli/main.cpp` - subcommand dispatch (deliverable 1).
- `src/system/mesen/roles/NesN8FifoRole.{hpp,cpp}` + `NesEverdriveFifo.hpp` - the emulated N8 path + the
  sample-offset timing + `RP_FIFO_TRACE` oracle.
- `src/transport/SpscRing.hpp` + `src/host/engine/EngineInvoker.hpp` (`released_` ring) - the audio->control
  crossing.
- `src/host/rpc/NativeFileWatcher.hpp` + `HostRpcService` - the opt-in host-thread + facet pattern for
  deliverable 2.
