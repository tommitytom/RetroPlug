# Phase E2: the N8 pack and one Edio

**Date:** 2026-11-03 · **Branch:** `feature/modularize` · **Status:** designed (on 64719dece)

Phase E is the Pico runtime ([modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md) section 3). E1 made a
C++ node's source build for the RP2350 as it builds for the desktop and drew the package boundary. E2 puts the first
feature behind it: everything RetroPlug knows about the Everdrive N8 Pro becomes one workspace package, `packages/n8`,
that a product lists and nothing else names, with the Edio protocol written once, in C++ that builds for both targets,
and wrapped for TypeScript over an RPC facet. The Pico box then runs on that same code and must still do everything it
did before (the baseline, `.superpowers/e-pico/baseline.md`, is a gitignored work file: section 8 repeats its
numbers). A SNES flash-cart pack, or anyone else's device, follows the template this one sets.

## 1. Before E2

- **The Edio protocol exists three times.** `packages/retroplug/src/n8/edio.ts` (362 lines) is the source of truth the
  CLI uses; `packages/native/src/host/n8/Edio.{hpp,cpp}` (173 / 301) is a 1:1 C++ port that throws `std::runtime_error`
  and returns `std::string` and `std::vector`; `pico/n8-host/edio.{c,h}` (269 / 61) is a C subset with caller buffers
  and return codes. The first two are held to one byte-level golden (`packages/retroplug/test/n8/edio-golden.json`, 15
  cases); the third to nothing but the rig. The menu commands (`*t`, `*n`, `*s`, `*v`, `*r`) exist three times too
  (`n8Menu.ts`, `N8Menu.{hpp,cpp}`, folded into `edio.c`), and so does booting a ROM (`n8Load.loadRom`, N8Host's
  `rom.upload` job, the box's `boot_rom`), with different timeouts: an install waits 10 s on the desktop and 12 s on
  the Pico, an SD block's ack 2 s and 5 s.
- **Two paths reach the cart.** The CLI's sessions (`n8-load`, `n8-play`, `n8-bridge`, `n8-sync`) run the TS Edio over
  `SerialRpcService`, a six-method raw serial facet mounted only in `retroplug-cli` under `RETROPLUG_N8_BRIDGE`
  (`cmake/deps.cmake`). The SDL standalone and the plugin run the C++ Edio in-process: `N8Link` (a serial thread fed by
  a lock-free ring from the audio thread, a reliable framing, paused jobs), `N8Host` (`n8.cfg`, the expansion volume,
  the menu probe, the cart jobs) and `N8CartLink` (the engine's `CartLink`), with the UI's "Settings > N8" reaching
  N8Host through four QuickJS hooks (`__rp_getN8Config`, `__rp_setN8Port`, `__rp_connectN8`, `__rp_setN8Lookahead`,
  `N8Hooks.cpp`).
- **Products name the N8 classes.** `sdl/main.cpp` and `plugin/PluginDSP.cpp` each construct an `N8Host` with a
  `WjwwoodSerialPort` factory and a port lister, an `N8CartLink`, call `bindN8Hooks`, `restore()` and
  `setDeviceLinks`, and order their destructors by hand so a node's handle outlives its plan; both product targets
  compile the six N8 sources (`Edio`, `WjwwoodSerialPort`, `N8Link`, `N8Host`, `N8Hooks`, `N8Menu`).
- **`rp.n8.nes` is not a pack.** `packages/native/src/host/graph/N8Nodes.cpp` sits in `retroplug-backend` under
  `packages/native`'s manifest (`rp_n8_package`, `device: false`) and includes engine headers: `HostServices.hpp`
  (`rp.host.devices/1`, a function-pointer struct that hands out C++ `CartLink*` and `SurfaceLink*`),
  `DeviceLinks.hpp` (`CartLink`'s virtuals take `std::string*` and a `JobRequest`; `CartNotes` is a `std::bitset`),
  `CapabilityJobs.hpp` (`JobRequest` of strings and a vector, `Job` with the engine's `SystemId` and `SystemBase*`,
  `DeviceJobSlot` on a `std::mutex`) and `SystemNodes.hpp` (the expansion-volume range). E1's section 10 records this
  as the gap E2 closes.
- **The Pico box is C with one heap allocation.** `pico/n8-host` (`main.c` 766, `box.c` 193, `edio.c` 269, plus
  `../midi-in/midi.c`) is a Pico SDK 2.1.1 project with TinyUSB 0.18.0's CDC host over Pico-PIO-USB (`5a37a66`, an
  external checkout at `/workspaces/Pico-PIO-USB`) or, with `RP_NATIVE_USB=ON`, the chip's own controller. Its own code
  never allocates, but `pio_usb_host_init` makes its 1 ms SOF timer's pool with `alarm_pool_create(2, 1)`, which
  mallocs. Its CMake project already enables C++.

## 2. Decisions taken

The user's, in order (2026-11-03; the Phase E list is in `.superpowers/e-pico/decisions.md`):

1. **C++, not C.** A node or a protocol written once in C++ builds for the desktop and the RP2350; nothing is rewritten
   in C for the device.
2. **N8 is a standalone pack** (nodes, Edio, link, lifecycle), the template a SNES flash-cart pack would follow.
3. **Edio exists once, in C++ in the pack**, with no exceptions, caller buffers and an abstract transport; TypeScript
   wraps it over an RPC facet; the byte decoders may stay TS; `edio.c` retires.
4. **The Pico baseline must pass at E2** (and at E3 and E4).
5. **Pack scope (E2 brainstorm): the native half whole.** A portable target and a desktop target, a device-provider
   entry so no product names an N8 class, the device ABI in `node-sdk` as portable C. The TS half holds the facet
   wrapper, the decoders and the load helpers; the N8 menus, the CLI sessions and `buildGraph`'s placement of
   `rp.n8.nes` stay in `packages/retroplug` and import the pack, until menus move onto extensions (main design
   section 11).
6. **Operations in C++.** The protocol and the multi-step operations (boot a ROM, read and write a file, list a
   directory) are portable C++ functions with a progress callback that can cancel, shared by the box, N8Host's jobs and
   the CLI's facet (synchronous, as the CLI's serial reads are today); TS keeps the decoders and the presentation.
7. **The whole box image under the link trap.** `skip_alarm_pool` and a static SOF timer of our own on hardware alarm
   2 (the alarm, IRQ and cadence the pool had), `pico_malloc` out, the firmware linked under E1's 87-symbol trap; the
   baseline re-proves the timing.
8. **The firmware stays in place, minimally changed.** `pico/n8-host` becomes C++ and calls the pack; the box policy
   stays in the firmware; `midi.c` stays C. E3 splits the main loop when it has a graph to host.

## 3. The pack

```
packages/n8/
  package.json            @retroplug/n8: the manifest (two native entries) and the TS exports
  native/
    CMakeLists.txt
    src/n8/               the portable target, retroplug-n8 (device: true, entry rp_n8_package)
      Transport.hpp         the abstract byte transport (section 4)
      Edio.{hpp,cpp}        the protocol
      Menu.{hpp,cpp}        the on-device menu's commands
      Ops.{hpp,cpp}         boot a ROM, read and write a file, list a directory
      CartLink.hpp          the link interface between the node and whoever drives the cart (section 5)
      N8Node.cpp            rp.n8.nes (today's N8Nodes.cpp)
    src/n8/desktop/       the desktop target, retroplug-n8-desktop (device: false, provider rp_n8_provider)
      SerialTransport.{hpp,cpp}   deps/serial (today's WjwwoodSerialPort)
      N8Link, N8Host, N8CartLink  moved from packages/native/src/host/n8
      N8Rpc.{hpp,cpp}             the facet (section 6)
      N8Provider.cpp              the provider (section 6)
    test/                 retroplug-n8-test, retroplug-serial-test, FakeN8.hpp, retroplug-n8-hwtest
  ts/
    index.ts              createN8Client, the decoders, the load helpers (section 7)
    test/
```

- **The portable target depends on `node-sdk` alone**, as `packages/core`'s does, and its manifest entry says
  `device: true`, so the desktop build compiles its `-device` object library under E1's rules (no heap, exception or
  RTTI symbol; `DeviceTypeCheck` on `rp.n8.nes`'s settings; `validate` and `adjust_layout` guarded) and
  `rp-device-check` links it. The desktop target depends on the portable one, `node-sdk`, `deps/serial`, rpcpp and
  reflect-cpp, never on `packages/native`.
- **What leaves `packages/native`:** all of `src/host/n8/`, `src/host/graph/N8Nodes.{hpp,cpp}`, the `rp_n8_package`
  entry of its manifest, the six N8 sources from the SDL and plugin targets, `SerialRpcService`, and the serial half of
  the CLI's `RETROPLUG_N8_BRIDGE` (its MIDI facet stays: it is not the N8's). The N8 tests' targets move with the code
  and keep their names and `pnpm test:n8`.
- **What leaves `packages/retroplug/src/n8`:** `edio.ts`, `n8Menu.ts`, `transport.ts` and `fakeSerial.ts` are deleted;
  the decoders and the risa sync translator move to `packages/n8/ts`.
- **What stays in `packages/retroplug`, importing the pack:** `menuDefs.ts`'s N8 rows, `n8Devices.ts`, `n8SdOps.ts`,
  `useN8SdWatch.ts`, the CLI sessions and `buildGraph.ts`'s `cart-<id>` placement.
- **Products list the pack** (`RP_DESKTOP_PACKAGES` gains `n8`; the `[packages]` golden records `rp.n8.nes` under it)
  and name nothing in it.

## 4. The one Edio

**The transport** (`Transport.hpp`):

```cpp
namespace rp::n8 {
class Transport {
public:
    virtual std::size_t write(const std::uint8_t* data, std::size_t size) = 0;               // bytes taken
    virtual std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) = 0;  // 0 on timeout
    virtual void        flushInput() = 0;
    virtual bool        linkUp() const { return true; }  // false fails a wait at once (the Pico's unmount)

protected:
    ~Transport() = default;  // not virtual: a virtual one makes the deleting destructor reach operator delete
};
}
```

The desktop's `SerialTransport` wraps `deps/serial` as `WjwwoodSerialPort` does today (its per-read timeout reset
included). The Pico's `UsbCdcTransport` (section 8) pumps TinyUSB and its idle hook inside its own waits; Edio never
knows about either.

**`Edio`** is a plain class over a `Transport&`: it allocates nothing, throws nothing, uses no RTTI, and is not
thread-safe (on the desktop `N8Link` and `N8Host` already serialise every use; on the Pico there is one thread).

- **Status, not exceptions.** Every call returns a `Status`: `Ok`, `Timeout`, `LinkDown`, `BadReply`, `Device` (with
  the N8's status byte), `Cancelled`, `TooLarge`. Values come back through out-parameters and caller buffers.
  `describe(Status)` gives the text, and where today's TS and C++ messages differ the TS one is kept, since it is what
  the CLI prints (section 7).
- **One write per frame.** A command's header, its arguments and as much payload as fits a 256-byte scratch in the
  object go out in one `write`; a larger payload follows in further writes. That is the Pico's PIO-USB rule (one packet
  per endpoint per 1 ms frame) and it changes nothing on the desktop but the number of calls. The golden compares byte
  streams, so it holds the new Edio unchanged; it moves to `packages/n8/native/test/` and stays the protocol's oracle.
- **Primitives** (today's C++ coverage, now on the Pico too): `connect` (flush, `CMD_STATUS`, the `A5` check, a short
  handshake timeout) and `status`; `sysInfo` (64 bytes) and `vdc` (8) into caller arrays; `freeSpace`, `dirMake`
  (tolerating "exists"), `fileDelete`; `memRD`, `memWR` (fire and forget), `fifoWR`, `fifoTxString`; `fileOpen`,
  `fileRead` (512-byte response-gated blocks), `fileWrite` (1 KB ack-gated blocks, then a status), `fileClose`;
  `listDir` as a visitor (one callback per entry, the name in a 256-byte buffer in the object, so no capacity limit);
  the raw `rx8`, `rx16`, `rx32` and `readData` a menu reply needs. The addresses and sizes (`ADDR_SRM`, `ADDR_SSR`,
  `ADDR_FIFO`, `ADDR_CFG`, `ADDR_EXP_VOL`, `CHR_RAM_OFFSET`, ...) are constants in `Edio.hpp`.
- **Menu commands** (`Menu.hpp`): `test` (`*t`, expecting `k`), `install` (`*n` and the path, a status byte, then the
  map index; 0x44 is the dirty-heap refusal), `start` (`*s`), `vramDump` (2048 + 16 bytes into caller buffers),
  `reset` (`*r`).
- **Operations** (`Ops.hpp`): `writeFile(path, data, progress)`, `readFile(path, buf, cap, &size, progress)` (sized by
  `listDir`, as today), and `bootRom(path, rom, srm?, {upload: always | never}, progress)`: `*t`, the upload when asked,
  the `.srm` when given, install, start. Its status names the step that failed, so a caller keeps its own policy
  around it: the box skips the upload when the saved CRC matches and, on an install failure, re-uploads and retries
  once, as `boot_rom` does today.
- **Progress and cancel are one callback**, `bool step(void* ctx, std::uint32_t done, std::uint32_t total)`, false to
  cancel. N8Host's jobs map it onto `DeviceJobSlot::progress` and `abandoned()`; the box feeds its watchdog from it.
- **Timeouts:** one table of defaults, overridable per call: a read 2 s, the handshake 300 ms, an SD block 5 s, an
  install 12 s. The two that differ today take the Pico's longer values, proven on the rig; no happy path waits longer.

What stays out of Edio and in the Pico's transport: the best-effort drain after a write (`edio_write` answers true
when the TX FIFO has not drained in 500 ms) and the input flush's 20 ms of silence.

## 5. The device ABI

The node and the link it drives move into the same pack, so the cart interface between them is the pack's own
contract: the engine needs to know nothing of carts. `node-sdk` gains two portable C headers.

**`rp_device.h`: links by name.**

```c
#define RP_HOST_DEVICES_INTERFACE "rp.host.devices/2"
typedef struct rp_host_devices {
    void* ctx;
    // [control] The link a host installed under `name` with the interface `interface_id`, or null.
    void* (*acquire)(void* ctx, const char* name, const char* interface_id);
    void  (*release)(void* ctx, void* link);
} rp_host_devices;
```

- The engine's `DeviceRegistry` becomes a small fixed table of `{name, interface id, link, acquire, release}` entries
  that a provider installs and clears (section 6), with today's rules kept: acquire and release on the control thread,
  a release reaching the link only while it is still the installed one, a node keeping its own balance across a
  reactivate. `Engine::setDeviceLinks(CartLink*, SurfaceLink*)` goes.
- **`rp::n8::CartLink`** (`packages/n8/native/src/n8/CartLink.hpp`, interface id `rp.n8.cart-link/1`, device name
  `n8`) is today's `CartLink` made portable: `push`, `connected`, `dropped`, `setExpansionVolume`, the job hooks, and
  `releaseNotes` over a fixed 2048-bit array in place of `std::bitset`; a protected non-virtual destructor. When the
  acquire answers null the node uses a disconnected stub of the pack's own (today's `NoCart`, moved).
- **The Launchpad's `SurfaceLink`** moves the same way, beside its nodes in `packages/native`, as
  `rp.launchpad.surface-link/1` under `launchpad`, until it has a pack of its own.

**`rp_jobs.h`: capability jobs.**

- The interface ids (`rp.system.memory/1`, `.sram/1`, `.rom/1`) move here from `CapabilityJobs.hpp`.
- `rp_job_request`: C strings for the node id, capability, op, region and name, the offset and length, and a byte span
  (today's `JobRequest` of `std::string` and `std::vector`).
- A device node's `rp_device_jobs { void* ctx; bool (*submit)(void* ctx, const rp_job_request*, rp_job* job, char*
  error, size_t error_cap); }` under each capability id it answers.
- The engine's report interface over its opaque `rp_job`, `rp.host.jobs/1`: `progress(job, done, total)`, `succeed(job,
  data, size)` (the engine copies), `fail(job, reason)`. The record's internals (its atomics, result, `SystemId`,
  `SystemBase*`) stay in the engine, and the control plane's `submitJob` / `drainJobs` RPC is unchanged.
- `DeviceJobSlot` becomes a desktop-only helper header in `node-sdk` (it keeps its mutex) reporting through that
  interface, so N8Host's one-job-at-a-time, busy and abandon rules stay as they are.

**The expansion volume** (0 to 200, default 100) becomes the pack's own constants; an engine-graph test holds them
equal to `rp.mesen.nes`'s, since one TS knob drives both nodes.

With this `rp.n8.nes` includes only `node-sdk` and its own pack, and the commit that does it changes no behaviour.

## 6. Providers and the facet

**A provider** is how a product starts a pack's device service without naming it. `node-sdk`'s `rp_provider.h`
(desktop-only C):

```c
typedef struct rp_device_host {
    uint32_t    abi_version;
    void*       ctx;
    const char* config_dir;  // where a provider keeps its persisted state (n8.cfg)
    bool (*install)(void* ctx, const char* name, const char* interface_id, void* link,
                    void (*acquire)(void* link), void (*release)(void* link));
    void (*clear)(void* ctx, void* link);
} rp_device_host;

typedef struct rp_provider {
    const char* id;
    void* (*create)(const rp_device_host* host);  // [control] after the engine, before the first graph
    void  (*restore)(void* self);                  // [control] bring the persisted state back (reconnect)
    void* (*rpc_service)(void* self);              // what the pack's facet registration mounts
    void  (*destroy)(void* self);                  // [control] after the host's plans are reclaimed
} rp_provider;
```

- A desktop manifest entry names it: `{ "target": "retroplug-n8-desktop", "provider": "rp_n8_provider", "rpc": {
  "header": "n8/desktop/N8Rpc.hpp", "register": "rp::n8::registerRpc" } }`. `cmake/packages.cmake` generates, per
  product, beside `rp_register_packages()`: `rp_start_providers(host)`, `rp_restore_providers()`,
  `rp_stop_providers()` (in manifest order, stopped in reverse), and a header-only
  `rp_register_package_rpc<Server>(server)` that includes each listed pack's header and calls its `register` with the
  provider's service. The configure-time product check refuses a provider or an `rpc` on a `device: true` entry.
- A product starts its providers after its engine and before its control plane can publish a graph, restores them
  where it restores its own state today, mounts their facets where it mounts its own, and in its destructor reclaims
  its plans, then stops them: the ordering `sdl/main.cpp` and `PluginDSP.cpp` hand-roll today, in one place.
- **The N8 provider** owns the transport factory and the port lister, the `N8Host` (its `n8.cfg`, format and path
  unchanged) and the `N8CartLink`, installed as `n8` / `rp.n8.cart-link/1`. The SDL standalone, the plugin and the CLI
  name none of them.
- **The Launchpad stays product-named** until its pack: only the SDL standalone owns a surface (the plugin's MIDI seam
  caps a message at 4 bytes), and its host switches to the registry's `install` and `clear`.

**The facet** (`N8Rpc.hpp`), mounted in every product that lists the pack:

- **Sessions**, which the CLI uses: `n8ListPorts`, `n8Open(port) -> handle`, the Edio primitives, the menu commands
  and the operations on a handle, `n8Close`. Each answers `{status, deviceStatus, message, bytes}`. A session on a port
  the streaming link holds fails to open, as two processes do today.
- **The streaming link**, which the UI uses: `n8LinkConfig`, `n8SetPort`, `n8Connect`, `n8SetLookahead`, replacing the
  four `__rp_*N8` hooks, so the pack depends on rpcpp and not on QuickJS. `hasN8()` becomes "the product mounted the
  facet"; the headless UI harness lists no N8 pack, and the submenu stays hidden there.
- Calls are synchronous, as every facet is; a long operation blocks its caller, as the CLI's serial reads block it
  today.

## 7. The TS half

- `packages/n8/package.json` names the package `@retroplug/n8`, with `exports` at `ts/index.ts`; `packages/retroplug`
  depends on it as `"workspace:*"`, and esbuild and `tsc` resolve it through the workspace link. It is the first TS
  half of a package; `core` and `dsp` will follow its shape.
- `createN8Client(call)` takes the app's RPC call, so the pack never imports `realBackend.ts`. Its `openN8(port)`
  answers today's `N8` surface: `memRD` and `readFile` return a `Uint8Array`, a failure throws `Error` or
  `N8TimeoutError` with today's message, and `listDir`, `writeFile`, `load`, `dumpSram`, `writeSramDirect`,
  `writeMemDirect` and `mapConfig` keep their names. `load` calls the facet's `bootRom`; the two direct writes become a
  `memWR` and a read-back `memRD`. The CLI sessions change their import and how they open a port, nothing else.
- The decoders (`sniffer`, `mapConfig`, `sysInfo`, `saveState`, `chrImage`, `menuImage`) and `risaSyncTranslator` move
  unchanged. `n8Devices.ts` calls the facet's link methods.
- **The CLI's flags and messages do not change**: BlipToaster and the hardware lab read them.
- **Tests:** the decoder and translator tests move to `packages/n8/ts/test`, and `run-tests.mjs` sweeps
  `packages/*/ts/test` as well (the skip baseline regenerated for the moved paths only). `edio.test.ts` and
  `menu-load.test.ts` are deleted: the golden is the C++ Edio's, and their cases (the menu replies, a boot, the SRAM
  paths, free space, mkdir, delete) become C++ cases over `FakeN8`. New: a pure-TS test of the wrapper over a fake
  `call` (each status to its error, message and `N8TimeoutError`) and a `test:native` case running a CLI-style session
  through the real facet to a fake cart (section 12, question 1).

## 8. The Pico

**The firmware** (`pico/n8-host`, in place): `main.c` and `box.c` become `main.cpp` and `box.cpp`; `edio.c` and
`edio.h` are deleted.

- `UsbCdcTransport` implements `rp::n8::Transport` over TinyUSB's CDC host: its waits pump `tuh_task` and the idle
  hook (`box_feed`: the watchdog and the LED), `linkUp()` reads the mount state, the drain after a write is best
  effort, the input flush waits for 20 ms of silence. One `Edio` and one transport live in static storage, and a mount
  rebinds the transport's CDC index.
- Every N8 call goes through the pack: the probe (`status`, `sysInfo`, never a cart-memory read), `boot_rom` (`bootRom`
  inside the box's CRC skip and retry), `box.cfg` (`readFile`, `writeFile`), the expansion-volume check (`memRD`,
  `memWR` of `ADDR_CFG`), the sniff (`memRD` of `ADDR_SSR + 0x80`), the MIDI batches (`fifoWR`), the health check
  (`status`).
- Unchanged: the box policy and its flags, the LED modes, the watchdog and the recovery with backoff, the ROM table in
  flash at `XIP_BASE + 0x200000`, the UART1 IRQ ring and the 192-byte batches, the SysEx `7D 50` pick, `midi.c` (C,
  behind `extern "C"` guards), the TinyUSB callbacks (`extern "C"`), and `loop.sh`, `stress.sh` and `faults.sh`.
- **The SOF timer:** the PIO-USB config sets `skip_alarm_pool`; the firmware claims hardware alarm 2 and its callback
  re-arms itself 1000 us later on an absolute schedule and calls `pio_usb_host_frame()`: the alarm, the IRQ and the
  cadence `alarm_pool_create(2, 1)` gave, with nothing allocated.

**The build.** The firmware becomes `retroplug_product(n8-host DEVICE PACKAGES n8)`, as `rp-device-check` is: the
pack's portable sources compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti`, E1's runtime with `pico_malloc`
removed, `check-trap.cmake`'s 87-symbol trap and its probe. Its generated registration includes `rp.n8.nes`, which no
plan uses until E3 but which proves the node links into the real image. The reflect-cpp pin and finder move out of
`packages/graph-runtime/device-check/CMakeLists.txt` into `cmake/reflectcpp-pin.cmake`, used by both projects. The
`RP_NATIVE_USB=ON` variant must build and link under the trap too; the rig is PIO-USB, so only that one runs. The
image's sizes are recorded before and after.

**CI.** `pnpm device:box` (`tools/build-box.sh`) builds the firmware, as a step beside `device:check` in the linux
job: the CI's pico-sdk 2.1.1 clone gains `lib/tinyusb` (`git submodule update --init lib/tinyusb`, 0.18.0 by the SDK's
pointer), and Pico-PIO-USB is cloned at `5a37a66`. `PICO_PIO_USB_PATH` keeps its `/workspaces/Pico-PIO-USB` default
for the rig; nothing else in the repository needs Pico-PIO-USB, so it is not vendored.

**The gate** is the baseline (2026-11-03, `pico/n8-host` from `feature/pico` 3865956d), repeated on the new image by
the controller and never by an implementer, under the nes-hardware-lab rules:

1. `./loop.sh --until 'booted by the Pico'`: boots through the N8 menu with no PC (baseline: 8.4 s after reset).
2. `./stress.sh --rom /workspaces/bliptoaster/build/bliptoaster.nes`: every case passes with zero overrun, framing or
   ring loss and zero failed writes (baseline: rx=153607 msgs=50775 fwd=50277 fail=0, latency 0.71 ms average, 9.9 ms
   maximum).
3. Audio control through the whole chain on the L6's channel 3: A4 and C5 on pitch and tracking the note (baseline:
   440.30 Hz, +1.2 c; 522.16 Hz, -3.6 c).
4. `./faults.sh --rounds 8`: at least 7 of 8 recovered, the only failure allowed the known Zigbee late-cut quirk.

The 9.9 ms maximum latency is attributed to a case this time; it fails the gate only if it grows.

## 9. Tests

- **Held unchanged:** parity (strict), the kit goldens, the render baseline, every descriptor golden and the
  `buildGraph` goldens (which place `rp.n8.nes` by its type id, unchanged).
- **Moved and retyped, case counts kept:** `retroplug-n8-test` (`Edio`, `N8Host`, `N8CartLink`, `N8CartJobs`),
  `retroplug-serial-test`, and the engine-graph `[n8-node]`, `[devices]`, `[capability]` and `[n8-adapters]` cases,
  the last now driving the real provider behind a real engine to `FakeN8`.
- **New:** the Edio's cases for what it did not have (status codes for every failure each primitive can meet, one
  write per frame, `TooLarge`, a cancel at each step of `bootRom`, `listDir`'s visitor over a long name, `linkUp`
  failing a wait); the C++ cases that replace `menu-load.test.ts`; the facet's sessions and link methods; the
  generated provider and RPC registration (a product with the pack mounts it, one without does not; the start, restore
  and stop order); the configure check's refusals; the TS wrapper test and the `test:native` session.
- **The device build:** `retroplug-n8-device` compiles in the default build; `rp-device-check` links the pack and its
  cases; `device:box` links the firmware under the trap.
- **Sanitizers:** TSan and ASan over `retroplug-n8-test` and the engine-graph tags above.

## 10. Landing

Five commits, each leaving every suite green:

1. **The device ABI:** `rp_device.h`, `rp_jobs.h` and the desktop `DeviceJobSlot` in `node-sdk`; the engine's named
   registry and `rp.host.devices/2`; the cart and surface interfaces beside their nodes. No behaviour change.
2. **The pack:** `packages/n8` by `git mv`, the portable and desktop targets split, the provider mechanism and the
   generated provider and RPC registration, the products naming no N8 class, the `[packages]` golden gaining `rp.n8`.
   The old C++ Edio moves as it is, into the desktop target.
3. **The one Edio:** the Edio, the menu commands and the operations in the portable target; N8Host and N8Link on
   them; the facet; the TS swap; `edio.ts` and its kin, `SerialRpcService` and the four hooks deleted;
   `rp-device-check` linking the pack.
4. **The Pico:** the C++ firmware, `UsbCdcTransport`, the static SOF timer, the trap on the whole image,
   `cmake/reflectcpp-pin.cmake`, `pnpm device:box` and its CI step.
5. **Docs:** this design's "As built", spec/, AGENTS.md, the Pico READMEs, the nes-hardware-lab skill's box notes, and
   E1's section 10 marked done.

**Acceptance.** Every suite with and without the fixtures, parity strict, the kit and render goldens under their
require flags; the sanitizers; `device:check` and `device:box` and the `env -i` CI dry run of both; a fresh tree;
`sdl:smoke`; the 15 Reaper checks; `validate`; BlipToaster and NESVJ on scratch copies; `test:cli-ts`. Then the
hardware, by the controller:

- **The desktop leg:** `n8-load --info`, a boot of BlipToaster, `--sniff --pal`, `--ls`, `--put-file`, `--get-file`
  and `--rm` on scratch files; `n8-play`'s A4 control on channel 3; `retroplug-n8-hwtest info`, `sniff`, `fifowr`, and
  its `submitCartJob` ops as C5's acceptance ran them (a link pause staying continuous, an SRAM dump, restore and dump
  round-tripping through the card's file at the menu, a ROM uploaded through the job that boots). No cart-memory read
  of a running mapper-30 game.
- **The Pico leg:** section 8's gate.
- The rig is left at a fresh menu with every scratch file deleted.

**How it is built** (as E1, Anthropic models only): a verified prototype on the local branch `proto/e2-n8`, one stage
per commit, stages 1 to 4 by Opus and stage 5 by Sonnet, every stage reviewed by Opus and given a fix pass, the Pico
gate run by the controller on the prototype's image before the plan is written; the plan generated from the
prototype, replayed to identical trees and reviewed by Opus; Sonnet implementers, Opus code reviews of tasks 1, 3 and
4, Sonnet acceptance, the controller's hardware legs, an Opus final review. Work files in the gitignored
`.superpowers/e2pro`, `e2plan` and `e2exec`.

## 11. Left beyond E2

- **E3:** `exportPlan`, the arena executor and the box as a graph host, its default plan today's bridge; with it the
  box's half of the device ABI (a `rp::n8::CartLink` over the firmware's Edio, no thread), and whether `rp.n8.nes` can
  plan on the Pico at all, since the planner's `Target::Pico` refuses `threads` and the node declares it for the
  desktop's link. The main loop splits into the generic host and the pack's part then.
- **E4:** MicroQuickJS on the Pico.
- **The rest of the package split:** the Launchpad as a pack (its provider, its facet in place of
  `bindLaunchpadHooks`), the N8 menus, CLI sessions and graph placement contributed by the pack once menus move onto
  extensions, the TS halves of `core` and `dsp`.
- **A web transport** for the Edio (Web Serial is asynchronous; the synchronous `read` needs a worker with
  `Atomics.wait` or asyncify).

## 12. Prototype questions

1. How a `test:native` session reaches a fake cart through the real facet: a test-only port name the provider's
   factory answers with `FakeN8`, or a pty with a fake-cart thread behind it.
2. Whether `rp_register_package_rpc<Server>` fits each product's server: the CLI's `BackendRpcServer`, SDL's, the
   plugin's `PluginRpcServer`, and the Node addon's (`NodeCodec`) if it mounts the pack.
3. The Pico's re-entrancy: a remount fired inside an Edio wait, mid-frame, with the object's scratch half filled; the
   transport's `linkUp()` and the firmware's `n8_gen` check must leave the next call clean.
4. Whether the static SOF timer matches the pool's timing: the stress latencies and the faults rounds say.
5. Whether `rp.n8.nes` (it declares `threads` and `unique`) compiles and links in device mode, and what its device
   build needs from `rp::n8::CartLink`'s stub.
6. Whether a 1 MB upload through `bootRom` keeps the watchdog fed by the transport's idle hook alone.
7. What the generated provider code costs a product with no providers (the render host, the tests): nothing, or an
   empty table.
