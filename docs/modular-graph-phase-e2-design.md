# Phase E2: the N8 pack and one Edio

**Date:** 2026-11-03 · **Branch:** `feature/modularize` · **Status:** built (E2, five commits); section 13 records the
build, answers section 12 and carries what was left, and sections 1 to 11 are corrected in place

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
   2 (the alarm, IRQ and cadence the pool had), a wrap that drops the SDK's pool function, `pico_malloc` out, the
   firmware linked under E1's 87-symbol trap; the baseline re-proves the timing.
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
      Ops.{hpp,cpp}        boot a ROM, read and write a whole file
      CartLink.hpp          the link interface between the node and whoever drives the cart (section 5)
      N8Node.{hpp,cpp}      rp.n8.nes (today's N8Nodes.cpp)
    src/n8/desktop/       the desktop target, retroplug-n8-desktop (device: false, provider rp_n8_provider)
      PortTransport.hpp           the desktop's owned transport interface (section 4)
      SerialTransport.{hpp,cpp}   deps/serial (today's WjwwoodSerialPort)
      N8Link, N8Host, N8CartLink  moved from packages/native/src/host/n8
      N8Rpc.{hpp,cpp}             the facet (section 6)
      N8Provider.{hpp,cpp}        the provider (section 6)
    src/transport/        N8ReliableLink.hpp, the host half of BlipToaster's reliable link
    test/                 retroplug-n8-test, retroplug-serial-test, retroplug-n8-hwtest; FakeN8.hpp, FakeJobs.hpp,
                          QjsRpc.hpp, the golden
  ts/
    index.ts              createN8Client: the link half and the sessions (section 7)
    constants.ts, load.ts the Edio constants and the load helpers
    sniffer.ts, mapConfig.ts, sysInfo.ts, saveState.ts, menuImage.ts   the decoders
    test/
```

- **The portable target depends on `node-sdk` alone**, as `packages/core`'s does (`FixedQueue`, which `N8ReliableLink`
  needs, moved to `node-sdk` for it), and its manifest entry says `device: true`, so the desktop build compiles its
  `-device` object library under E1's rules (no heap, exception or RTTI symbol; `DeviceTypeCheck` on `rp.n8.nes`'s
  settings; `validate` and `adjust_layout` guarded) and `rp-device-check` links it. The desktop target depends on the
  portable one, `node-sdk`, `deps/serial`, rpcpp and reflect-cpp, never on `packages/native`.
- **What leaves `packages/native`:** all of `src/host/n8/`, `src/host/graph/N8Nodes.{hpp,cpp}`, the `rp_n8_package`
  entry of its manifest, the six N8 sources from the SDL and plugin targets, `SerialRpcService`, and the serial half of
  the CLI's `RETROPLUG_N8_BRIDGE` (its MIDI facet stays: it is not the N8's). The N8 tests' targets move with the code
  and keep their names and `pnpm test:n8`.
- **What leaves `packages/retroplug/src/n8`:** `edio.ts`, `n8Menu.ts`, `transport.ts` and `fakeSerial.ts` are deleted;
  the decoders (`chrImage` and the risa sync translator excepted: section 7) and `n8Load.ts`'s helpers move to
  `packages/n8/ts`.
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

The desktop's `SerialTransport` wraps `deps/serial` as `WjwwoodSerialPort` did (its per-read timeout reset and its
port listing included). It is a `PortTransport` (`n8/desktop/PortTransport.hpp`), the desktop's owned interface: the
transport interface plus a virtual destructor and the text of the call that failed. A transport throws on neither
target: the desktop's catches deps/serial's exceptions, answers a short count or 0 and keeps the port's own words, which
`N8Link`, `N8Host` and the facet report in place of Edio's. The Pico's `UsbCdcTransport` (section 8) pumps TinyUSB and
its idle hook inside its own waits, and `read(..., 0)` returns after one pump with what it found. Edio never knows about
either.

**`Edio`** is a plain class over a `Transport&`: it allocates nothing, throws nothing, uses no RTTI, and is neither
thread-safe (on the desktop `N8Link` and `N8Host` already serialise every use; on the Pico there is one thread) nor
re-entrant: a `Progress`, a `listDir` visitor or a transport's idle hook must not call the same Edio, and a MIDI
forward from inside a wait waits for the call. Its constructor is not `constexpr` on purpose: a constant-initialised
static Edio (the box's) would put the whole object, its zeroed buffers included, in `.data`, copied from flash at every
boot, for no RAM saved.

- **Status, not exceptions.** Every call returns a `Status` struct, `{code, where, step, device, reply}`. The `Code`
  is `Ok`, `Timeout`, `LinkDown`, `BadReply`, `Device` (`device` is the N8's status byte), `NotFound` (an operation's
  file is not on the card, which no other code fits), `Cancelled` or `TooLarge`; `Where` names the wire position that
  failed (a command's bytes, a reply, the status reply, a trailing status, a directory record, a file block, a write's
  ack, the menu's test answer, its install status, its map index, ...), `Step` the step of an operation (`Test`,
  `Upload`, `Save`, `Install`, `Start`, `List`, `Open`, `Data`, `Close`), and `reply` what a `BadReply` answered.
  Values come back through out-parameters and caller buffers. `describe(status, out, capacity, path)` gives the text,
  with the call's path for the few texts that name one (a mkdir's, an install's, a file not found; `Status` carries no
  pointer), formatted by its own small writer because newlib's printf family can reach the heap. Where the TS and C++
  messages differed the TS one is kept, since it is what the CLI prints (section 7), its hex lowercase; four texts
  have no TS twin (a write timeout, link down, cancelled, too large). A port that fails (a deps/serial exception) is
  reported by the facet as `linkDown` with the port's words rather than as a timeout: Edio itself says `Timeout`,
  because a desktop transport cannot tell its link's state, and latching `linkUp` false there would turn a refused
  config write into a dead stream.
- **One write per frame.** A command's header, its arguments and as much payload as fits a 256-byte scratch in the
  object go out in one `write`; a larger payload follows in further writes (8 KB at a time). That is the Pico's PIO-USB
  rule (one packet per endpoint per 1 ms frame) and it changes nothing on the desktop but the number of calls. **A short
  write fails the command** (`Timeout` at `Where::Write`, `LinkDown` when the link says so): both old clients ignored
  the write's count, the root of the 2026-10-22 mid-command bug. On the desktop the streaming link stops with `serial
  write failed (N8 unplugged?): ...` where it used to carry on writing into a cart parser left mid-command, and the
  expansion-volume write is retried by `OnControl` a second later like any failed config write. The golden compares byte
  streams, so it holds the new Edio unchanged; it moves to `packages/n8/native/test/` and stays the protocol's oracle,
  with 13 cases recorded from the old TS clients (a listing, a file read, the menu's commands, ROM boots) added to its
  original 15 (section 9).
- **Primitives** (today's C++ coverage, now on the Pico too): `connect` (flush, `CMD_STATUS`, the `A5` check, a short
  handshake timeout) and `status`; `sysInfo` (64 bytes) and `vdc` (8) into caller arrays; `freeSpace`, `dirMake`
  (tolerating "exists"), `fileDelete`; `memRD`, `memWR` (fire and forget), `fifoWR`, `fifoTxString`; `fileOpen`,
  `fileRead` (512-byte response-gated blocks), `fileWrite` (1 KB ack-gated blocks, then a status), `fileClose`;
  `listDir` as a visitor (one callback per entry, the name in a 256-byte buffer in the object, so no capacity limit; a
  name over 255 bytes is `TooLarge`, and the buffer is valid for the call only); the raw `rx8`, `rx16`, `rx32` and
  `readData` a menu reply needs. The addresses and sizes (`ADDR_SRM`, `ADDR_SSR`, `ADDR_FIFO`, `ADDR_CFG`,
  `ADDR_EXP_VOL`, `CHR_RAM_OFFSET`, ...) are constants in `Edio.hpp`. The facet decodes a listing's names as a
  `TextDecoder` does (`wellFormed` in `N8Rpc.cpp`: the WHATWG UTF-8 decoder, valid sequences kept, each maximal
  ill-formed subpart one U+FFFD, a leading byte order mark dropped), since a name reaches JavaScript as a string.
- **Menu commands** (`Menu.hpp`): `test` (`*t`, expecting `k`; it does not flush first, so a caller that wants the old
  Pico's wire calls `flushInput()` before it), `install` (`*n` and the path, a status byte, then the map index; 0x44 is
  the dirty-heap refusal), `start` (`*s`), `vramDump` (2048 + 16 bytes into caller buffers), `reset` (`*r`). A missing
  map index after a 0 status is a failure (`Timeout`, `Where::MapIndex`, which tells it from a failed status), where
  `edio.c` ignored it; the box treats `Install` at `MapIndex` as success and sends `*s` itself.
- **Operations** (`Ops.hpp`): `writeFile(path, data, progress)` and `readFile`, which comes in two forms. The `FileSink`
  form, `readFile(path, sink, &size, progress)`, lists the file's directory to find its size and then asks the sink,
  `{buffer(ctx, size), ctx}`, for a buffer that size (null is `TooLarge` at `Step::List`, nothing opened): how a caller
  that can allocate, the facet, reads a file it does not know the size of with one listing. The buffer form,
  `readFile(path, buf, cap, &size, progress)`, is the same over a fixed buffer, for a caller with no heap. And
  `bootRom(path, rom, srm, Upload::Always | Never, &mapIndex, progress, options)`: `*t`, the upload when asked, the
  battery when given, install, start. Its status names the step that failed, so a caller keeps its own policy around
  it. The battery goes to `EDN8/gamedata/<the ROM's file name>/bram.srm`, written by `writeBattery`, a never-inlined
  helper, so the path buffer is on the stack only while a battery is written (never in the box, which links the helper
  and never calls it). The bound counts FatFs characters, not bytes: a FatFs name is at most 255 UTF-16 units and so at
  most 765 bytes of UTF-8, which makes the buffer `14 + 3 x 255 + 9 + 1` bytes; a longer name is longer than any FatFs
  name and is `TooLarge` at `Step::Save` before anything is sent. `BootOptions` is for a caller with a boot policy of
  its own, and its defaults are the plain boot: `handshake` false leaves out bootRom's `*t` (and that step's stop
  point), and `before(ctx, step)` is called before each step bootRom runs, between whole commands with the Edio idle, so
  unlike a `Progress` it may use the Edio; false stops the boot there, `Cancelled` at that step with nothing of it sent.
  The box keeps its CRC skip, its saved choice and its retry around it with these (section 8).
- **Progress and cancel are one callback**, `bool step(void* ctx, std::uint32_t done, std::uint32_t total)`, false to
  cancel, taken at stop points between whole commands (before a file is opened, before each block after the first of a
  file read, before an install); a call made inside a command, as each acknowledged block of a file write is, only
  reports, since the cart takes a command's whole payload once it has asked for it. An operation leaves the card as it
  found it on a failure (a file a failed call left open stays open, as the TS and C++ clients did, so the CLI's
  failure-path wire is unchanged); only a cancel, which comes between commands, closes what it opened. N8Host's jobs use
  the primitives rather than `bootRom` and `writeFile`, to keep each job's wire, chunking and delete-on-abort exactly,
  and map the callback onto `DeviceJobSlot::progress` and `abandoned()`; the box passes none, and its watchdog is fed by
  the transport's idle hook alone (section 13.5, question 6).
- **Timeouts:** one table of defaults on every Edio, `Timeouts {readMs 2000, handshakeMs 300, sdBlockMs 5000,
  installMs 12000}`, changeable by a caller and overridable per call (a timeout of 0 takes the table's); each read the
  transport makes waits that long, so a long reply that keeps arriving never times out. "SD block" covers a file write
  block's ack and a file read block's status and data; a command's trailing status keeps the 2 s reply timeout, as the
  Pico does. There are no sticky timeouts: the old install left 10 s set until `N8Link` reset it, where now each wait
  takes its class's value. The two that differed take the Pico's longer values (install 10 s to 12 s, file blocks 2 s to
  5 s on the desktop), proven on the rig; no happy path waits longer. The one wait that is shorter than before is the
  map index: it is read at the reply's 2 s, where both old clients read it under the install's sticky 10 s and the Pico
  read it at 2 s, since the menu sends it straight after the status.

What stays out of Edio and in the Pico's transport: the best-effort drain after a write (`edio_write` answered true
when the TX FIFO had not drained in 500 ms; `UsbCdcTransport::write` answers 0 when the link drops during the drain)
and the input flush's 20 ms of silence.

## 5. The device ABI

The node and the link it drives move into the same pack, so the cart interface between them is the pack's own
contract: the engine needs to know nothing of carts. `node-sdk` gains three portable C headers,
`packages/node-sdk/native/src/graph/rp_device.h`, `rp_jobs.h` and `rp_stage.h`, and the desktop-only `rp_provider.h`
(section 6).

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

- The engine's `DeviceRegistry` (`host/devices/DeviceRegistry.hpp`) is a fixed table of 8 entries, `{name[32],
  interfaceId[64], link, acquire, release}`, copied in (a name up to 31 bytes, an id up to 63: `RP_DEVICE_NAME_MAX` and
  `RP_DEVICE_INTERFACE_ID_MAX` in `rp_device.h`, which the registry's `kMaxName` and `kMaxInterfaceId` are), so an
  acquire is an allocation-free scan. A provider installs and clears through `Engine::installDevice(name, id, link,
  acquire, release)` and `clearDevice(link)`, the shape of `rp_device_host`'s `install` and `clear`;
  `Engine::installLink` and `clearLink` do it for a C++ link implementing an interface, installed as an `Interface*`,
  the pointer a node casts back, with the link's own `acquire()` and `release()` as the hooks when it has them
  (`rp::host::linkHooks`, which installs none for a link without the member, so a product whose link must be counted
  passes its hooks by name: the N8 provider's `acquireCart` / `releaseCart` call `N8CartLink::acquire` and `release`,
  and `[n8-adapters]` holds the expansion-volume register's reset to AUTO after `clearGraph`). An install refuses a null
  link, an empty or overlong name or id, a duplicate name and id or link, and a full table (all `[[nodiscard]]`; a host
  clears before it installs again). Today's rules are kept: acquire and release on the control thread, a release
  reaching the link only while it is still the installed one (identity-checked, so a release after a clear reaches
  nothing; a guard against an out-of-order release, not a licence to free), a node keeping its own balance across a
  reactivate. **A host frees a link only after every plan that could hold it is destroyed**: a node uses its link in its
  destroy, so the order is plans, then clear, then free, and a clear only stops new acquires. `clearDevice` and
  `clearLink` answer whether the pointer was installed (false for one the registry does not hold, as when `clearLink`
  computes another interface's pointer for a link inheriting two). **A link pointer identifies one installation**: a
  device offering two interfaces, or two versions of one, installs a distinct object for each.
  `Engine::setDeviceLinks(CartLink*, SurfaceLink*)` is gone.
- **`rp::n8::CartLink`** (`packages/n8/native/src/n8/CartLink.hpp`, interface id `rp.n8.cart-link/1`, device name
  `n8`) is today's `CartLink` made portable: `push`, `connected`, `dropped`, `setExpansionVolume`,
  `abandonJobs(owner)`, the job hook `submitJob(const rp_job_request&, rp_job*, const rp_host_jobs*, owner, char*
  error, size_t cap)`, and `releaseNotes` over `CartNotes`, a fixed 64-word class (2048 bits: `set`, `reset`, `test`,
  `any`, `none`, `size`, `==`) in place of `std::bitset`; a protected non-virtual destructor, and no virtual
  `acquire`/`release` (they are the installer's hooks). When the acquire answers null the node uses a disconnected
  stub of the pack's own (today's `NoCart`, moved), which counts in a 32-bit atomic: a 64-bit one is
  `__atomic_fetch_add_8` and `__atomic_load_8` library calls on the Cortex-M33, which the box's image would have to
  carry. Whether a link runs a thread is the link's business, so the node declares no `threads` (section 13.6).
- **The Launchpad's `SurfaceLink`** moves the same way, beside its nodes in `packages/native`
  (`launchpad/SurfaceLink.hpp`), as `rp.launchpad.surface-link/1` under `launchpad`, until it has a pack of its own; its
  stub counts in 32 bits too, so a later pack inherits it as it is.

**`rp_jobs.h`: capability jobs.**

- The interface ids (`rp.system.memory/1`, `.sram/1`, `.rom/1`) move here from `CapabilityJobs.hpp`.
- `rp_job_request`: C strings for the node id, capability, op, region and name, the offset and length, and a byte span
  (today's `JobRequest` of `std::string` and `std::vector`). A C string cannot carry an embedded NUL, so `"a\0b"` would
  reach the cart as `"a"` and dump another game's battery: the engine refuses a device job whose strings hold a NUL
  ("the job's name holds a NUL byte, which a device's request cannot carry"), and the rule is written on the type. It is
  the one behaviour change of the commit. The strings are UTF-8, and `bytes` is null when `size` is 0 (the engine's
  view passes null for an empty span rather than an empty vector's `data()`). The three capability ids version
  `rp_job_request` and `rp_device_jobs`; `rp.host.jobs/1` versions the report table alone.
- A device node's `rp_device_jobs { void* ctx; bool (*submit)(void* ctx, const rp_job_request*, rp_job* job, char*
  error, size_t error_cap); }` under each capability id it answers.
- **`rp_job` is opaque**: `struct rp_job` is incomplete in every host. The engine's record is `rp::host::Job`
  (`CapabilityJobs.hpp`), which the engine casts to and from the handle with `handle(Job*)` and `record(rp_job*)`; the
  cast is sound because a node hands its link the report table and the job of the same host. The record's internals
  (its atomics, result, `SystemId`, `SystemBase*`) stay in the engine, and the control plane's `submitJob` / `drainJobs`
  RPC is unchanged. A pack gives its tests a record of its own (`packages/n8/native/test/FakeJobs.hpp`), which proves a
  non-engine record works.
- The engine's report interface, `rp.host.jobs/1` (`rp_host_jobs`), has **four entries and no `ctx`**: `progress(job,
  done, total)`, `succeed(job, data, size)` (the engine copies), `fail(job, reason)` (copies the reason) and
  `abandon(job)`. A device that reports from a thread of its own serialises those reports with `abandon` (the desktop's
  `DeviceJobSlot` does, under one lock), so nothing reaches the record after `abandon` returns; `DeviceJobSlot::claim`
  refuses a null table or record. `abandon` fails the job as removed, whose completion reads "node X left the plan
  before the job completed"; `fail` is the device's own failure and would change that message. There is no `ctx` because
  the record carries everything a report needs, so one static table serves every engine. A node reaches the table
  through `rp_host::get_interface` at bind and passes it to its link with each job, so a host that offers none is
  visible: the node refuses its jobs ("this host takes no device jobs"). Reasons are UTF-8 and bounded: a submit's
  refusal goes through a 256-byte buffer (`Engine::kDeviceJobErrorBytes`) whose last byte the engine NUL-terminates and
  whose trailing incomplete sequence it drops (`deviceReason`); a `fail()` reason is kept to `kFailReasonBytes` (1024,
  since N8Host's texts embed SD paths) and cut back to a whole code point.
- **`rp_stage.h`** holds `rp.stage/1` (`rp_host_stage`), the staged-message interface, moved out of `HostServices.hpp`:
  the one interface of the device ABI that a node presents rather than takes, and `rp.n8.nes` presents it and must
  include no engine header.
- `DeviceJobSlot` becomes a desktop-only helper header in `node-sdk` (`graph/desktop/DeviceJobSlot.hpp`; it keeps its
  mutex and is an `#error` under `RP_DEVICE`) reporting through the `rp_host_jobs*` it is given at `claim`, so
  N8Host's one-job-at-a-time, busy and abandon rules stay as they are.

**The expansion volume** (0 to 200, default 100) becomes the pack's own constants, `rp::n8::kExpansionVolume*` in
`N8Node.hpp`; a `STATIC_REQUIRE` in `N8Node.test.cpp` holds them equal to `rp.mesen.nes`'s (`SystemNodes.hpp`), since
one TS knob drives both nodes.

With this `rp.n8.nes` includes only `node-sdk` and its own pack, and the commit that does it changes no behaviour but
the NUL refusal.

## 6. Providers and the facet

**A provider** is how a product starts a pack's device service without naming it. `node-sdk`'s `rp_provider.h`
(desktop-only C, an `#error` under `RP_DEVICE`):

```c
#define RP_PROVIDER_ABI_VERSION 1u
typedef struct rp_device_host {
    uint32_t    abi_version;
    void*       ctx;
    const char* config_dir;  // where a provider keeps its persisted state (n8.cfg)
    bool (*install)(void* ctx, const char* name, const char* interface_id, void* link,
                    void (*acquire)(void* link), void (*release)(void* link));
    void (*clear)(void* ctx, void* link);
} rp_device_host;

typedef struct rp_provider {
    uint32_t    abi_version;                       // first, so a mismatch is read before anything laid out after it
    const char* id;                                // the provider's name in a log line
    void* (*create)(const rp_device_host* host);   // [control] after the engine, before the first graph
    void  (*restore)(void* self);                  // [control] bring the persisted state back (reconnect)
    void* (*rpc_service)(void* self);              // what the pack's facet registration mounts
    void  (*destroy)(void* self);                  // [control] after the host's plans are reclaimed
} rp_provider;
typedef const rp_provider* (*rp_provider_fn)(void);  // what a manifest entry names, as an entry returns an rp_package*
```

- No call of the table throws: the host's `install` and `clear` are `noexcept` forwards, and the N8 provider's `create`
  catches everything, logs and answers null, its `restore` catches and logs, and `restore`, `rpc_service` and `destroy`
  are `noexcept`. A create that answers null is left out: its service is null, so its facet is not mounted.
- A desktop manifest entry names it: `{ "target": "retroplug-n8-desktop", "device": false, "provider": "rp_n8_provider",
  "rpc": { "header": "n8/desktop/N8Rpc.hpp", "register": "rp::n8::registerRpc" } }`. An `rpc` needs the provider on its
  own entry, because that provider supplies the service. The configure-time product check refuses a provider or an
  `rpc` on a `device: true` entry, an `rpc` without a provider, a non-identifier or duplicate provider, and a provider
  entry that does not say `device` (four scratch manifests under `packages/native/test/packages/refused/` hold the
  refusals).
- **The generated API is per instance.** A DAW holds several plugin instances, each with its own engine, so the
  providers' state is not a static. `cmake/packages.cmake` writes, per desktop product, a header-only
  `rp_product_providers.hpp` in an include directory of the product's own (a device product gets none), which the
  product's sources include by that bare name, with `rp::Providers rp_start_providers(const rp_device_host*)`,
  `rp_restore_providers(rp::Providers&)`, `rp_stop_providers(rp::Providers&)` and
  `rp_register_package_rpc<Server>(Server&, const rp::Providers&)`. `rp::Providers` (`graph/desktop/Providers.hpp`) is
  the providers one product instance started: it creates in manifest order, restores in order, stops in reverse, is
  idempotent, stops in its destructor, is move-only, refuses an ABI version mismatch (naming the provider by its table
  index, not its id), stops what it had started and rethrows on any throw, and answers a provider's `service(i)`. A
  product whose packages name no provider gets inline stubs: an empty `rp::Providers` (no allocation) and a
  registration that does nothing.
- **Listing a pack is not starting it.** The header is inline and only a product that includes it names
  `rp_n8_provider`; a static archive's member is linked only where something names it. The render host, the headless UI
  harness, the Node addon and most test binaries list `n8` (every golden row keeps `rp.n8.nes`) and include nothing, so
  none of the provider's code, threads or ports reaches them; a product's link line still carries the pack's targets
  (deps/serial, setupapi, IOKit), but no object. The headless UI harness mounts no N8 facet, so the submenu
  stays hidden there.
- A product starts its providers after its engine and before its control plane can publish a graph, restores them
  where it restores its own state, mounts their facets where it mounts its own, and in its destructor reclaims its
  plans (`clearGraph`) and then stops them: the ordering `sdl/main.cpp` and `PluginDSP.cpp` hand-rolled, in one place
  (the CLI's is a scoped `Teardown` declared before its audio driver, so any audio thread has stopped first). The
  engine's side is `host/devices/ProviderHost.hpp`, which builds the `rp_device_host` forwarding to
  `Engine::installDevice` and `clearDevice`.
- **The N8 provider** owns the transport factory and the port lister, the `N8Host` (its `n8.cfg`, format and path
  unchanged) and the `N8CartLink`, installed as `n8` / `rp.n8.cart-link/1`; `rp::n8::createProvider(host, factory,
  lister)` is the seam a test host uses. The SDL standalone, the plugin and the CLI name none of them. Its restore
  logs `[retroplug] N8 restore: ...` (the SDL standalone printed `[retroplug-sdl] ...`).
- **The CLI starts the provider and mounts the facet but never restores it.** `n8.cfg` is the UI's persisted link (the
  standalone's and the plugin's), and restoring it in the CLI would claim the port the CLI's own sessions open. So the
  CLI's engine has the `N8CartLink` installed, disconnected, where it had none before: an `rp.n8.nes` node in a CLI
  session binds it rather than its stub, and its jobs are refused with N8Host's "not connected". The Node addon lists
  the pack but starts no provider and mounts no facet, as it mounted no N8 facet before. The test host
  (`retroplug-host`, which `test:native` and the authoring tools run) starts the provider over `FakeN8` (section 13.5,
  question 1).
- **The Launchpad stays product-named** until its pack: only the SDL standalone owns a surface (the plugin's MIDI seam
  caps a message at 4 bytes), and its host switches to the registry's `install` and `clear`.

**The facet** (`N8Rpc.hpp`, declared there and implemented in `N8Rpc.cpp`; only `registerRpc` is a template, so the
logic is compiled once in the pack and a product's server adds methods to itself), mounted in every product that starts
the pack's provider:

- **Sessions**, which the CLI uses: `n8ListPorts`, `n8Open(port) -> handle`, the Edio primitives, the menu commands and
  the operations on a handle (`n8Handshake`, `n8Status`, `n8SysInfo`, `n8Vdc`, `n8FreeSpace`, `n8DirMake`,
  `n8FileDelete`, `n8MemRD`, `n8MemWR`, `n8FifoWR`, `n8FifoTxString`, `n8ListDir`, `n8FileOpen`, `n8FileRead`,
  `n8FileWrite`, `n8FileClose`, `n8ReadData`, `n8FlushInput`, `n8SetReadTimeout`, `n8MenuTest`, `n8MenuInstall`,
  `n8MenuStart`, `n8MenuVramDump`, `n8MenuReset`, `n8ReadFile`, `n8WriteFile`, `n8BootRom`), `n8Close`. Each answers
  `{status, deviceStatus, message, bytes, value, entries, step}`: the four of the design plus a number (a handle, a
  status byte, free bytes, a map index), a listing's entries and an operation's failed step. `status` is `Code`'s name
  (`"ok"`, `"timeout"`, `"linkDown"`, ...). **A session and the streaming link never hold one port, both ways and by
  device**: `n8Open` on the link's port (the link connected, or a job pending on that name) fails with "cannot open
  serial port: <port>", and `N8Host::setPortGuard`, which the facet sets and clears in its destructor, makes the link
  refuse to connect to a port a session holds ("the port <p> is open in an N8 session", the same state a failed
  connect leaves); both compare names first and then `std::filesystem::canonical`, so a `/dev/serial/by-id` symlink and
  its tty are one port. One facet per host (`setPortGuard` holds one).
- **The streaming link**, which the UI uses: `n8LinkConfig`, `n8SetPort`, `n8Connect`, `n8SetLookahead`, replacing the
  four `__rp_*N8` hooks, so the pack depends on rpcpp and not on QuickJS. `hasN8()` becomes "the product mounted the
  facet": the UI probes once and reads it true when `n8LinkConfig` answers. This half is the UI's alone, since its
  setters persist the `n8.cfg` the three products share; the CLI mounts both halves because every product registers its
  packs one way, and uses only the sessions.
- **The codec:** `rfl::Bytestring`, the facet's bytes, reaches JavaScript as a `Uint8Array` through the products'
  QuickJS codec; the JSON codec cannot carry it at all (`rfl::Generic` has no bytes), so `registerRpc` instantiates
  for `TypedRpcServer<Empty, QuickJSCodec>` (the CLI's and SDL's `BackendRpcServer` and the plugin's `PluginRpcServer`
  are all that alias) and the pack's tests mount it on the same (`test/QjsRpc.hpp`). The Node addon's `NodeCodec` is
  not instantiated, since the addon mounts no facet.
- Calls are synchronous, as every facet is; a long operation blocks its caller, as the CLI's serial reads block it
  today.

## 7. The TS half

- `packages/n8/package.json` names the package `@retroplug/n8`, with `exports` at `ts/index.ts`; `packages/retroplug`
  depends on it as `"workspace:*"`, and esbuild and `tsc` resolve it through the workspace link. It is the first TS
  half of a package; `core` and `dsp` will follow its shape.
- `createN8Client(call)` takes the app's RPC call, so the pack never imports `realBackend.ts`. It answers the link half
  (`n8LinkConfig` and its three setters), `listPorts()` (which answers `[]` for a host with no facet by catching only
  method-not-found, and rethrows anything else) and `openN8(port)`, which answers today's `N8` surface: `{port, edio,
  menu, connect, load, dumpSram, writeSramDirect, writeMemDirect, listDir, readFile, writeFile, mapConfig, close}`.
  `memRD` and `readFile` return a `Uint8Array`; a failure throws `Error`, or `N8TimeoutError` for a `timeout` status
  only (a `linkDown` is a plain `Error`, as the old client threw from the RPC), with `describe()`'s message. `load`
  calls the facet's `bootRom`; the two direct writes become a `memWR` and a read-back `memRD`. The CLI sessions
  (`n8-load`, `n8-play`, `n8-bridge`, `n8-sync`) change their import and how they open a port
  (`createN8Client(makeCall()).openN8(...)`), nothing else.
- The decoders (`sniffer`, `mapConfig`, `sysInfo`, `saveState`, `menuImage`), the Edio constants (`constants.ts`) and
  the load helpers (`load.ts`, `n8Load.ts`'s) move to the pack unchanged but for `mapConfig`'s import. **`chrImage`
  and `risaSyncTranslator` stay in `packages/retroplug`**, moved within it (`src/risa/rom/chrImage.ts`,
  `src/risaSyncTranslator.ts`, their tests to `test/risa/`): the first imports the NES tile codec in `src/risa/rom`,
  the second risa's sync protocol in `src/risaSync.ts`, and moving them would make the pack depend on `retroplug`, a
  cycle. `n8Devices.ts` calls the facet's link methods through `createN8Client(makeCall())`; `setN8LinkClient()` is
  the seam the mock tier installs a fake through.
- **The CLI's flags and messages do not change**: BlipToaster and the hardware lab read them. The four sessions'
  `--help` and the top-level index were diffed before and after: identical.
- **Tests:** the decoder tests move to `packages/n8/ts/test` (the translator's stays with it in `test/risa/`), and
  `run-tests.mjs` sweeps `packages/*/ts/test` as well, with slugs `<package dir>/<path>` (`n8/sniffer` is its old slug)
  and a refusal of a duplicate. The pack's tests import `retroplug`'s harness by relative path and are type-checked by
  `retroplug`'s tsconfig (its `include` gains `../n8/ts`), since the pack has no `typescript` devDependency and adding
  one is a lockfile change; the skip baseline regenerated with an empty diff. `edio.test.ts` (25 cases) and
  `menu-load.test.ts` (17) are deleted: the golden is the C++ Edio's, and their cases (the menu replies, a boot, the
  SRAM paths, free space, mkdir, delete) became C++ cases over `FakeN8`. New: `session-client.test.ts`, a pure-TS test
  of the wrapper over a fake `call` (each status to its error, message and `N8TimeoutError`, the direct writes, the
  `listPorts` rules), `link-client.test.ts`, and `test-native/n8-session.test.ts`, which runs a CLI-style session
  (`n8-load --info/--df/--mkdir/--put-file/--ls/--get-file/--rm`, a boot with `--srm`, the running cart's config block,
  `N8TimeoutError`, the link-held port refused) through the real facet to a fake cart (section 13.5, question 1).

## 8. The Pico

**The firmware** (`pico/n8-host`, in place): `main.c` and `box.c` become `main.cpp` and `box.cpp` (`git mv`); `edio.c`
and `edio.h` are deleted. `pio_usb_root.c` is a one-function C shim: `pio_usb_ll.h` does not compile as C++ (an inline
function stores an int into an enum), and `main` reads `PIO_USB_ROOT_PORT(0)->connected` through it. TinyUSB's
`osal_none.h` applies `++` and `--` to a volatile, which C++20 deprecates, so a `#pragma GCC diagnostic` around the two
`#include "tusb.h"` silences it there only.

- `UsbCdcTransport` implements `rp::n8::Transport` over TinyUSB's CDC host: its waits pump `tuh_task` and the idle
  hook (`box_feed`: the watchdog and the LED), `linkUp()` reads the mount state, the drain after a write is best
  effort, the input flush waits for 20 ms of silence. One `Edio` and one transport live in static storage.
- **A remount never resumes a call on the new N8.** `tuh_task` drains its event queue and TinyUSB's enumeration delays
  block inside it, so an unmount and a re-mount can both happen inside one pump. The unmount callback therefore calls
  `usb.unbind()`; the CDC mount callback only records the index (and bumps `n8_gen`); and **the main loop rebinds**,
  `usb.bind(n8_cdc_idx)`, at its top level just before the new N8's probe, never inside an Edio call. From the unmount
  to that bind `linkUp()` is false, so the call in flight fails at once (`LinkDown`), `bootRom` returns and the boot
  is abandoned, and a MIDI batch flushed in the window fails fast instead of landing in the new N8's menu FIFO. The
  box's `BOOT_GONE()` is `n8_gen != gen || !n8_ready || !usb.linkUp()`: the link term is what stops a boot whose N8
  went away during the probe at its first guard, before one futile `*t`. The Edio object starts the next call clean:
  `send()` zeroes its frame and `begin()` rebuilds it for every command, and `name_` is valid only during a visitor
  call.
- **The N8 calls:** the probe is `status` and `sysInfo` (never a cart-memory read); the menu handshake is `flushInput`
  then `Menu::test`, as `edio_menu_test` flushed first; `box.cfg` is read with the primitives, a plain `fileOpen`,
  `fileRead(16)` and `fileClose` (a close is always sent once the open succeeded, `edio_file_get`'s wire;
  `Ops::readFile` would add a directory listing) and saved with `Ops::writeFile`; the expansion-volume check, the
  sniff, the MIDI batches and the health check are `memRD`, `memWR(ADDR_EXP_VOL)`, `memRD(ADDR_SSR + 0x80)`, `fifoWR`
  and `status`.
- **The boot is `bootRom` with its hooks, and its wire is `edio.c`'s.** The box keeps its handshake loop, its CRC skip,
  its saved choice and its retry, and calls `bootRom(path, rom when uploading, {}, Always or Never, nullptr, {},
  {handshake false, boot_step, &run})`. Its `boot_step` hook prints each line where the old firmware printed it: before
  Upload `upload <path> (<n> bytes)...`; before Install `uploaded in <n> ms`, the `box.cfg` save when the choice changed
  (an interrupted upload never leaves a CRC that claims the ROM is on the card, which is why the save lies between the
  upload and the install, inside the hook), the pending pick cleared, `BOOT_GONE`, `menu up -> install <path> (*n)...`;
  before Start `BOOT_GONE` and `installed -> boot (*s)`. The retry (an install refused with a status other than 0x44
  when the upload was skipped) is `bootRom(Always)` again. An install that answers no map index (`Where::MapIndex`)
  runs the Start hook and sends `*s` itself, as `edio_menu_install` ignored that read. The commands are the same, in the
  same order, with no extra `*t`, and every log string the gate's scripts match is unchanged.
- Unchanged: the box policy and its flags, the LED modes, the watchdog and the recovery with backoff, the ROM table in
  flash at `XIP_BASE + 0x200000`, the UART1 IRQ ring and the 192-byte batches, the SysEx `7D 50` pick, `midi.c` (C,
  behind `extern "C"` guards), the TinyUSB callbacks (`extern "C"`), and `loop.sh`, `stress.sh` and `faults.sh`.
- **The SOF timer** (`SofTimer.{hpp,cpp}`): the PIO-USB config sets `skip_alarm_pool`, and the firmware claims
  hardware alarm 2 of the default timer (`TIMER0_IRQ_2`, exclusive handler, priority `0x80`, the
  `PICO_DEFAULT_IRQ_PRIORITY` the pool's handler kept) and re-arms it 1000 us after the last target (absolute, no
  drift; late, the handler fires again in the same IRQ until the target is ahead), calling `pio_usb_host_frame()`: the
  alarm, the IRQ and the cadence `alarm_pool_create(2, 1)` gave, with nothing allocated. The first frame comes 1000 us
  after the start, armed from a forced IRQ (`gTarget` is stored before the handler is installed, so the forced IRQ is
  the only thing that can start it); the handler runs from RAM, where the pool's ran from flash and called into RAM.
  It differs in starting right after `tuh_init()` returns rather than inside it, microseconds later and before any
  device can attach. **`skip_alarm_pool` alone is not enough**: `pio_usb_host_init` still contains the call
  `alarm_pool_create(2, 1)` behind its `if`, so `alarm_pool_create_on_timer`, which mallocs, stays linked (the trap
  fired at `time.c.o` naming `__wrap_malloc` and `__wrap_calloc`). The firmware links with
  `--wrap=alarm_pool_create_on_timer` and defines the wrapper in `SofTimer.cpp` as a `panic()` that never runs, and
  `--gc-sections` drops the SDK's function and the heap with it. The wrapper is not one of the 87 trapped symbols, and
  Pico-PIO-USB itself is not edited.
- **The stack is 4 KB.** `n8_host` sets `PICO_STACK_SIZE=0x1000` (all of SCRATCH_Y, which holds nothing else) and
  `PICO_CORE1_STACK_SIZE=0x800` (core 1's reservation keeps the 2 KB it had; the macro defaults to the main stack's).
  The linker checks only the reservation: scratch code that would overlap the reserved 4 KB fails the link. The stack's
  real depth is a hand sum with no limit at run time: the box's deepest wait is `box.cfg`'s save inside the Install
  hook, about 970 bytes of frames, below it TinyUSB's enumeration and the SDK's printf chain (about 840) and above it
  the interrupts nested on the same stack (about 500), a pessimistic 2.3 KB against the nominal 2 KB it was and the
  4 KB it is. If E3 puts anything in scratch or starts core 1, `rp-device-check`'s successor wants the same size.

**The build.** The firmware becomes `retroplug_product(n8-host DEVICE PACKAGES n8 TARGETS n8_host)`, as
`rp-device-check` is (the target keeps its name, so `loop.sh`'s `n8_host.elf` is unchanged): the pack's portable
sources compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti`, E1's runtime with `pico_malloc` removed, the
87-symbol trap over the whole image (TinyUSB and Pico-PIO-USB included) and `n8_host_trap_probe`, the firmware's own
probe that must fail to link. What both device projects share is hoisted: `cmake/reflectcpp-pin.cmake` (the
reflect-cpp pin and finder), `cmake/device-image.cmake` (`pico_malloc`'s removal in place, the trap list,
`rp_device_executable()` and `rp_device_trap_check()`) and `cmake/check-trap.cmake` (moved from `device-check`). Its
generated registration includes `rp.n8.nes`, which no plan uses until E3 and which `--gc-sections` would drop, so
`main` references `rp_register_packages` and prints `[n8-host] node type rp.n8.nes (registered, not planned)` at boot:
the node linking into the real image is proven by the image naming it. The `RP_NATIVE_USB=ON` variant builds and links
under the trap too; the rig is PIO-USB, so only that one runs. A scratch build with every `loop.sh` option on at once
(`-DRP_NATIVE_USB`, `N8_TUSB_DEBUG`, `N8_MENU_TRIES`, `N8_APU_LAT`, `N8_EXP_VOL`, `N8_TEST_HOOKS`, `N8_ROM_PATH`)
links under the trap. The image carries no UF2 in `device:box` (`-DPICO_NO_PICOTOOL=1`, as E1 decided, or the SDK
would fetch and build picotool in CI); `loop.sh`'s own build still makes its UF2, and the flashed bytes are identical.
A fresh `loop.sh` build dir passes the desktop build's `reflectcpp_SOURCE_DIR` from `$repo/build/CMakeCache.txt` when
there is one, so it needs no network, and falls back to the clone otherwise. `cmake_minimum_required` is 3.20 (the
packages need 3.19) and the standard C++20.

| Image (Release, flash / data / bss) | Before E2 | After E2 |
|---|---|---|
| PIO-USB | 43664 / 16528 / 32192 | 48508 / 15324 / 32360 |
| `RP_NATIVE_USB` | 41256 / 1412 / 25592 | 48468 / 1560 / 26128 |
| `rp-device-check` (E1: 65156 / 1844 / 412742) | | 82012 / 2000 / 416402 |

The PIO-USB image before linked a heap (`pico_malloc`'s wrappers, newlib's `malloc`, `calloc`, `free`, `_sbrk` and
`alarm_pool_create_on_timer`, the only caller); after, it links no heap symbol, and its data shrank because newlib's
malloc state is gone. The native image already linked none, so its growth is the pack (Edio, Menu, Ops and `rp.n8.nes`)
with nothing removed to offset it. The box links `writeBattery` and `batteryPath` and never calls them (`--gc-sections`
cannot drop what a kept function, `bootRom`, refers to without LTO).

**CI.** `pnpm device:box` (`tools/build-box.sh`) builds the firmware in both USB variants, as a step beside
`device:check` in the linux job: two steps in `build.yml`, the first initialising the CI's pico-sdk 2.1.1 clone's
`lib/tinyusb` (0.18.0 by the SDK's pointer) at depth 1 and fetching Pico-PIO-USB at `5a37a66`, the second running
`pnpm device:box` with `PICO_SDK_PATH` and `PICO_PIO_USB_PATH`. The script's SDK rule is `loop.sh`'s, not
`device:check`'s strict one: `PICO_SDK_PATH` when it carries TinyUSB, else `/workspaces/pico-sdk` (the devcontainer
bakes an SDK with no TinyUSB at `/opt/pico-sdk` and points the variable at it), said aloud when it falls back; a path
that is set and is no SDK, or a `PICO_PIO_USB_PATH` that is no checkout, stops the run. `PICO_PIO_USB_PATH` keeps its
`/workspaces/Pico-PIO-USB` default for the rig; nothing else in the repository needs Pico-PIO-USB, so it is not
vendored. An `env -i` dry run against fresh shallow clones passed both.

**The gate** is the baseline (2026-11-03, `pico/n8-host` from `feature/pico` 3865956d), repeated on the new image by
the controller and never by an implementer, under the nes-hardware-lab rules:

1. `./loop.sh --rom /workspaces/bliptoaster/build/bliptoaster.nes --until 'booted by the Pico' --fail "no 'k'|FAILED"`:
   boots through the N8 menu with no PC. (A flash without `--rom` boots `N8_ROM_PATH`, `n8-midi.nes`, which no check
   uses.) The baseline's 8.4 s was `n8-midi.nes`; the reference from E2 on is BlipToaster's 12.2 s from the saved
   choice with the CRC skip, about 5 s more with an upload.
2. `./stress.sh --rom /workspaces/bliptoaster/build/bliptoaster.nes`: every case passes with zero overrun, framing or
   ring loss and zero failed writes (baseline: rx=153607 msgs=50775 fwd=50277 fail=0, latency 0.71 ms average, 9.9 ms
   maximum).
3. Audio control through the whole chain on the L6's channel 3: A4 and C5 on pitch and tracking the note (baseline:
   440.30 Hz, +1.2 c; 522.16 Hz, -3.6 c).
4. `./faults.sh --rounds 8`: at least 7 of 8 recovered, the only failure allowed the known Zigbee late-cut quirk (it
   restages a ROM only when given `--rom`, so the one `loop.sh` staged stays).

E2's reference figures, which a later gate compares per window: `[stats] lat=` is the last window's average and
maximum, not cumulative; isolated notes 587 us average and 1110 us worst (baseline 625 / 1089); the final line's
recovery window 10.0 ms (baseline 9.9); the window holding a 40-byte SysEx pair 7.2 ms average and 20.9 ms peak (about
13 ms of it the SysEx on the wire); sustained-flood windows peak at 3.4 to 7.0 ms. A window may not exceed its
reference by more than noise.

## 9. Tests

- **Held unchanged:** parity (strict), the kit goldens, the render baseline, every descriptor golden and the
  `buildGraph` goldens (which place `rp.n8.nes` by its type id, unchanged). The one golden that changes on purpose is
  `[packages]`, where `rp.n8.nes` moves under the n8 package and no product's type ids change.
- **Moved and retyped, case counts kept:** `retroplug-n8-test` (`Edio`, `N8Host`, `N8CartLink`, `N8CartJobs`),
  `retroplug-serial-test`, and the engine-graph `[n8-node]`, `[devices]`, `[capability]` and `[n8-adapters]` cases,
  the last now driving the real provider behind a real engine to `FakeN8`. The pack's tests read no `packages/native`
  header: a fake job host of their own (`FakeJobs.hpp`) stands for the engine's record.
- **New:** the Edio's cases for what it did not have (status codes for every failure each primitive can meet, one
  write per frame, `TooLarge`, a cancel at each step of `bootRom`, `listDir`'s visitor over a long name, `linkUp`
  failing a wait); the C++ cases that replace `menu-load.test.ts` (`Ops.test.cpp`, 12 cases with `bootRom`'s options and
  the FatFs bound); the facet's sessions and link methods (`[n8-rpc]`: the port guard both ways and by device, the
  WHATWG names, a gone port); the generated provider and RPC registration (`[providers]`: a product with the pack mounts
  it, one without does not; the start, restore and stop order, an ABI mismatch, a throwing create); `[n8-provider]`; the
  configure check's refusals; the TS wrapper test and the `test:native` session. **The golden has 28 cases**, its
  original 15 and 13 recorded from the old TS clients (a listing and an empty one, two file reads, the menu's five
  commands, four ROM boots), each also checked for its parsed values (the map index, the listing's entries).
- **The device build:** `retroplug-n8-device` compiles in the default build; `rp-device-check` links the pack and runs
  its cases (`N8Protocol.cpp`: a scripted cart, a static Edio, a boot, a file read, a cancelled boot, every primitive;
  a `[device-check]` case holds its constants on the desktop); `device:box` links the firmware under the trap, in both
  USB variants, each with a probe that must fail to link.
- **Sanitizers:** TSan and ASan over `retroplug-n8-test` and the engine-graph tags above (`[n8-node]`, `[n8-adapters]`,
  `[providers]`, `[devices]`, `[capability]`).

## 10. Landing

Five commits, each leaving every suite green:

1. **The device ABI:** `rp_device.h`, `rp_jobs.h` and the desktop `DeviceJobSlot` in `node-sdk`; the engine's named
   registry and `rp.host.devices/2`; the cart and surface interfaces beside their nodes. No behaviour change but the
   NUL refusal (section 5).
2. **The pack:** `packages/n8` by `git mv`, the portable and desktop targets split, the provider mechanism and the
   generated provider and RPC registration, the products naming no N8 class, the `[packages]` golden gaining `rp.n8`,
   the facet's link half, `rp.n8.nes` a `device: true` entry in the device check. The old C++ Edio moves as it is, into
   the desktop target.
3. **The one Edio:** the Edio, the menu commands and the operations in the portable target; N8Host and N8Link on
   them; the facet's sessions; the TS swap; `edio.ts` and its kin, `SerialRpcService` and the four hooks deleted;
   `rp-device-check` linking the pack.
4. **The Pico:** the C++ firmware, `UsbCdcTransport`, the static SOF timer, the trap on the whole image,
   `cmake/reflectcpp-pin.cmake` and `device-image.cmake`, `pnpm device:box` and its CI step, `Where::MapIndex`.
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
  box's half of the device ABI (a `rp::n8::CartLink` over the firmware's Edio, no thread, keeping `push()`'s promise
  never to block: `rp.n8.nes` declares no `threads` and a Pico target plans it). The main loop splits into the generic
  host and the pack's part then.
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

## 13. As built

Five commits on `feature/modularize`: the device ABI, the pack, the one Edio, the Pico and these docs. Parity bit for
bit, the kit goldens, the render baseline, the descriptor goldens and `buildGraph`'s goldens were untouched throughout
(the one golden E2 changes on purpose is `[packages]`, where `rp.n8.nes` moved under the n8 package and no product's
type ids changed), both consumer suites passed at every commit, and nothing was re-recorded. The desktop's N8
behaviour is the old clients' wire: identical bytes in identical order, on every path but a short write, for every
operation the CLI, `N8Host` and `N8Link` perform, in fewer `write` calls. Sections 1 to 11 are corrected in place
where the build made them false (the Edio's status, the report interface, the providers' per-instance API, what stays
in `retroplug`, the box on `bootRom`). The final counts: `retroplug-n8-test` ends at 89 cases (1220 assertions; the
baseline's 57 / 529) and `retroplug-serial-test` at 1 / 7, `retroplug-engine-graph-test` at 330 (313),
`retroplug-device-mode-test` at 11 cases (10), `pnpm test` at 1781 cases over 174 files (1814), `test:native` at 606
passed and 2 skipped over 168 files (602 / 167), and `device:check`'s image at 82012 / 2000 / 416402 flash / data /
bss (65156 / 1844 / 412742).

### 13.1 The device ABI (commit 1)

- **Headers.** `rp_device.h`, `rp_jobs.h` and `rp_stage.h` in `node-sdk`, as section 5 records them: the fixed 8-entry
  registry, the opaque `rp_job` over the engine's record (`rp::host::Job`, `handle()` and `record()`), the four-entry
  `rp.host.jobs/1` with no `ctx`, `DeviceJobSlot` under `graph/desktop/`. `Engine::setDeviceLinks`, `HostDevices` and
  `DeviceLinks.hpp` are gone; `installDevice` / `clearDevice` and `installLink` / `clearLink` replace them, and the
  three products that installed links (the SDL standalone, the plugin, the test host) call them.
- **The cart and surface interfaces** are `rp::n8::CartLink` and `rp::launchpad::SurfaceLink`; `rp.n8.nes` was renamed
  from `N8Nodes` to `N8Node` and `LaunchpadNodes.cpp` includes no engine header either.
- **Tests.** `DeviceAbi.c` and `DeviceAbi.test.cpp` compile the headers as C, compare C's layout with C++'s and drive
  a C node through the registry and the report table; `Devices.test.cpp` gains the registry's table, a clear, a
  release after a clear and a full table; the engine-graph count rose by 11 (313 to 324).

### 13.2 The pack (commit 2)

- **`packages/n8`** by `git mv`: the two manifest entries, `retroplug-n8` (`rp_n8_package`) and `retroplug-n8-desktop`
  (`rp_n8_provider`, the `rpc` registration); the N8 tests' targets with their names; `N8ReliableLink.hpp`, which
  `N8Link` and the emulated NES FIFO role both speak (`retroplug-core` links the pack's include roots for it), and
  `FixedQueue` to `node-sdk`, so the pack depends on `node-sdk` alone.
- **The provider mechanism** (`rp_provider.h`, `rp::Providers`, the generated header, `ProviderHost.hpp`, the
  configure check's refusals) and the products naming no N8 class: `sdl/main.cpp`, `PluginDSP.cpp` and `cli/main.cpp`
  hold an `rp::Providers` and a `ProviderHost`; `N8Hooks.{hpp,cpp}` and the four `__rp_*N8` hooks are deleted;
  `RP_DESKTOP_PACKAGES` is `core dsp graph-runtime n8 native`. The facet's link half (`N8Rpc.hpp`, header-only at the
  time) and `n8Devices.ts` on `createN8Client`.
- **`rp.n8.nes` is `device: true`** (section 13.6): `retroplug-n8-device` compiles in the default build,
  `rp-device-check` links the pack (`RP_DEVICE_CHECK_PACKAGES` is `core dsp n8`) and its host offers
  `rp.host.devices/2` with an acquire that always answers null, so the node runs on its stub. The image grew from
  65156 to 68964 flash.
- **The sanitizers.** `tip-verify.sh`'s sanitizer legs had used the tag `[n8]`, which matches nothing, so the node's
  16 `[n8-node]` cases had never run under TSan or ASan; the legs gain `[n8-node]`, `[n8-adapters]` and `[providers]`
  (21 cases; 162 in all), and `retroplug-n8-test` has legs of its own. Clean.

### 13.3 The one Edio (commit 3)

- **The protocol, menu and operations** in the portable target, `retroplug-n8-device` included; the desktop's `Edio.cpp`
  and `N8Menu` are deleted, `N8Link` and `N8Host` run on the new Edio, `WjwwoodSerialPort` became `SerialTransport` over
  `PortTransport`, and the facet's sessions were added (section 6). `SerialRpcService`, the CLI's raw serial facet,
  and `RETROPLUG_N8_BRIDGE`'s serial half are deleted (it now builds the MIDI facet alone and links rtmidi only).
- **The TS swap:** `edio.ts`, `n8Menu.ts`, `transport.ts`, `fakeSerial.ts` and `src/n8/index.ts` deleted, `n8Load.ts`
  became `load.ts`, the CLI sessions on `openN8`, `createSerialClient` left `realBackend.ts`.
- **The golden** gained 13 cases recorded from the old TS clients (section 13.6), the device check gained
  `N8Protocol.cpp` (a scripted cart, a static Edio, a boot, a file read, a cancelled boot and every primitive; 1390
  bytes counted by hand and an FNV-1a held on the desktop by a `[device-check]` case), and `test-native` gained
  `n8-session`.
- **The size** went to 81608 / 2000 / 416402 (the n8 row 3401 to 13021 flash): the primitives the first build let the
  section GC drop are kept under the trap.

### 13.4 The Pico (commit 4)

- **The firmware** is C++ on the pack (section 8): `main.cpp`, `box.cpp`, `UsbCdcTransport`, `SofTimer`, the C shim, the
  link trap over the whole image, `cmake/device-image.cmake`, `cmake/reflectcpp-pin.cmake`, `cmake/check-trap.cmake`,
  `pnpm device:box` (`tools/build-box.sh`) and its two CI steps.
- **One pack change:** `Where::MapIndex`, with a `retroplug-n8-test` case, so the box can tell a missing map index
  from a failed install as `edio.c` could.
- **Tests and checks:** both USB variants link under the trap and both probes fail as they must; the `env -i` dry run
  of `device:check` and `device:box` against fresh shallow clones passed; the desktop suites are as before.

### 13.5 The prototype questions, answered

1. **A `test:native` session reaches a fake cart through the real facet by the test host calling
   `rp::n8::createProvider` with `FakeN8`'s factory and a one-port lister** (`"fake-n8"`), `FakeN8.hpp` compiled into
   that target alone, its `n8.cfg` in a per-process temp directory (a static, since the harness ends the process
   through txiki's `exit`, which runs static destructors and not `main`'s frame). A test-only port name or an
   environment hook in the production provider would have shipped fake-cart code and a test switch in every product.
   The consequence: the test host (which the authoring tools run too) has the N8 facet, and its `rp.n8.nes` binds the
   disconnected `N8CartLink` instead of its stub; both refuse jobs "not connected", and no suite saw a difference.
2. **`rp_register_package_rpc<Server>` fits the products' one server type**: the CLI's and SDL's `BackendRpcServer` and
   the plugin's `PluginRpcServer` are all `TypedRpcServer<Empty, QuickJSCodec>`. It does not instantiate for the JSON
   codec (`rfl::Bytestring` is unsupported by `rfl::Generic`), so the pack's tests mount on QuickJS. The Node addon's
   `NodeCodec` was not instantiated: the addon mounts no facet.
3. **A remount inside an Edio wait** is handled by unbinding in the unmount callback and rebinding at the top of the
   main loop, with `linkUp()` false in between and `BOOT_GONE` carrying the link term (section 8). The Edio object
   leaves the next call clean. The first build missed a remount inside the probe, before `boot_rom` read its
   generation, which cost about 8 s of failed handshakes and a false `no 'k'`; the link term closed it.
4. **The static SOF timer matches the pool's timing**: the stress latencies, the recovery window and 8 of 8 fault
   rounds say so (section 13.7). It differs in starting microseconds later, before any device can attach, and in
   running from RAM.
5. **`rp.n8.nes` compiles and links in device mode and plans on the Pico**, once it declares no `threads` (section
   13.6). The first try failed twice: `#include <iterator>` made libstdc++ pull in `<string>`, `<stdexcept>` and
   `<exception>`, which fired `node.hpp`'s heap-header `#error` (the include is gone; only `std::size` was used and
   `<string_view>` declares it), and `threads = true` tripped `DeviceTypeCheck`. The device build needs only what
   `CartLink.hpp`'s `NoCart` has (32-bit atomics); the object's undefined symbols are `__stack_chk_fail`, `lround` and
   `strlen`, then the soft-double helpers, `memcmp`, `memcpy` and `memset`.
6. **A 1 MB upload keeps the watchdog fed by the transport's idle hook alone**, by trace: every wait loop pass is
   `pump()` (`tuh_task(); box_feed();`), thousands of times a second against the 3 s watchdog, and `rom_crc32` feeds it
   every 64 KB. The longest stretch without a feed is TinyUSB's blocking enumeration delay (about 0.5 s), which predates
   E2 and cannot happen mid-upload without first failing it. The upload path was also run on the rig (section 13.7).
7. **The generated provider code costs a product with no providers nothing**: a generated header it never includes, and
   for a product whose packages name no provider inline stubs (an empty `rp::Providers`, no allocation).

### 13.6 What was found

- **`threads` is the link's business, not the node's.** The first build had `rp.n8.nes` declare `threads`, which kept
  `Target::Pico` from planning it and `device: true` from holding. The thread belongs to the desktop's link, so the node
  declares none and relies only on `CartLink::push()` never blocking, never allocating and taking no lock, whoever
  drains the ring. The flag means what the node itself needs, not its device link; the Launchpad nodes still declare it
  for theirs (the exception, until their pack). The planner now plans `rp.n8.nes` for a Pico target (`N8Node.test.cpp`
  holds `"threads":false` and both rigs), E1's portable-types rule counts it, and nothing in TS reads the flag for it.
- **`rp.host.jobs/1` needed a fourth entry**, `abandon`, and an opaque `rp_job` over the engine's record (an earlier
  build defined `struct rp_job` as the record itself), and a request with an embedded NUL had to be refused (section 5).
- **Providers are per instance.** The design's global `rp_start_providers(host)` would have kept provider state in
  statics, and a DAW holds several instances each with its own engine.
- **`chrImage.ts` and `risaSyncTranslator.ts` could not move to the pack**: each imports `retroplug` modules, a cycle
  (section 7).
- **The golden did not cover the operations.** The review of the Edio found that its 15 cases carried no listing, file
  read, menu command or boot, so a wrong wire there would have passed. Thirteen cases were recorded from the old TS
  clients at the pack commit (`3eb5a7928`'s `edio.ts`, `n8Menu.ts` and `n8Load.ts` over their fake port) and appended;
  every one matched the new Edio byte for byte on the first run, with every queued reply read.
- **A short write used to be ignored** by both old clients, the root of the 2026-10-22 mid-command bug; it now fails
  the command (section 4).
- **`skip_alarm_pool` alone left the heap in the image**: the call to `alarm_pool_create(2, 1)` stays linked behind the
  config's `if`, so the firmware wraps `alarm_pool_create_on_timer` (section 8).
- **The first box kept the upload outside `bootRom`**, which sent one extra `*t` before each `*n` and broke the wire
  equality with `edio.c`; `BootOptions` (no handshake, a before-step hook) lets the box keep `bootRom` and the old wire.
- **`pnpm install --offline` without `--frozen-lockfile` silently pruned 464 lines of `pnpm-lock.yaml`** (every other
  platform's esbuild and typescript optional packages). The lockfile was restored and the two entries added by hand;
  never run the non-frozen offline install here.
- **The 4 KB stack.** The hand sum for the box's deepest chain is a pessimistic 2.3 KB against the SDK's nominal 2 KB;
  the old firmware's was about 2.0 KB. Today an overrun would land in unused SCRATCH_Y, but nothing guards it, so the
  box asks for 4 KB (section 8); `rp-device-check`'s protocol chain is about 1.9 KB against its nominal 2 KB and its
  types chain (E1's settings decode) about 2.3 KB, and it is never run.
- **Writing a battery to the SD card is not for the box**: `writeBattery` is linked and never called; its 789-byte
  path buffer (an 864-byte frame) is on the stack only in the facet's boots (the CLI's) and the device check's.

### 13.7 The gate

Run by the controller on the prototype's image (sha256 `10a6b09edb99...`, BlipToaster, never `n8-midi.nes`), against the
baseline of section 8. The image is byte-identical to the one the final fix pass produced.

| Leg | Baseline | E2 |
|---|---|---|
| `loop.sh --rom .../bliptoaster.nes --until 'booted by the Pico'` | booted 8.4 s after reset (the default ROM, `n8-midi.nes`) | booted by the Pico in 12.2 s with BlipToaster (saved choice, CRC skip), expansion volume 129 (the stage commit's smoke booted the default ROM in 8.0 s) |
| `stress.sh` | rx=153607 msgs=50775 fwd=50277 fail=0, 0.71 ms average | rx=153277 msgs=50663 fwd=50185 fail=0, overrun, framing and ring loss 0; isolated latency worst 1110 us, average 587 us (baseline 1089 / 625) |
| audio on the L6's channel 3 | A4 440.30 Hz +1.2 c, C5 522.16 Hz -3.6 c | identical |
| `faults.sh --rounds 8` | 7 of 8 (the Zigbee late-cut quirk) | 8 of 8; abandoned boots 7, stuck menus 0, recovery reboots 0, uploads 0 |
| the upload path (added) | | two ROMs staged, SysEx `7D 50` pick and an NES power-cycle: 294928 bytes uploaded in about 4.9 s and booted; the next cycle booted the saved choice by CRC skip (mapper 24 exp vol 139; mapper 69 exp vol 129) |

- **The `lat=` figure in the `[stats]` line is per window, not cumulative.** The 9.9 ms the baseline attributed
  "maximum latency" to is the final line's recovery window; E2's is 10.0 ms there. Sustained-flood windows peak at 3.4
  to 7.0 ms, and **the window holding the SysEx pair peaks at 20.9 ms (average 7.2 ms)**: a 40-byte SysEx is about 13
  ms on the 31250 baud wire, and no baseline window was kept for it. The gate condition, that the maximum not grow,
  held where it can be compared.
- **A `loop.sh` reset clears a pending pick**, as `box_start` documents; a pick needs an NES power-cycle only.
- The desktop leg of the acceptance (`n8-load`, `n8-play`, `retroplug-n8-hwtest`) is the controller's, run after the
  plan is replayed.

### 13.8 What is left

- **E3.** The box's half of the device ABI: a `rp::n8::CartLink` over the firmware's Edio with no thread, which must
  keep the promise `CartLink.hpp` makes, that `push()` never blocks, never allocates and takes no lock (a box that
  forwards from its main loop through the same link its Edio waits on would break it). The stack: `rp-device-check`'s
  successor wants `PICO_STACK_SIZE=0x1000`, and a `-Wstack-usage` check would replace the hand sum. A host-side fake
  for `UsbCdcTransport` (the transport's remount, unmount and drain rules are proven on the rig and by reading, and
  have no host test). The main loop still splits into the generic host and the pack's part then.
- **The Launchpad's `threads` flag** is the one node still declaring `threads` for its link; it goes with its pack,
  provider and facet in place of `bindLaunchpadHooks`.
- **E4**, the rest of the package split and a web transport, as section 11.
