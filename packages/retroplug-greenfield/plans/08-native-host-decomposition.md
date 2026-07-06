> **Status:** implemented — the decomposition below shipped in `packages/native-greenfield`
> (`SystemFactory`/`SameBoyBackend`, `Engine`, `EngineInvoker` with `Direct`/`Queued`,
> and `HostRpcService`/`EngineRpcService`/`AudioDriverRpcService` behind `BackendFacade`).
> One deviation from the sketch below: the dual-purpose test host selects Direct↔Queued
> *dynamically* via an `active_` invoker pointer the audio driver swaps on start/stop
> (rather than a static per-host composition). Deferred items are unchanged.

# Native host decomposition — Engine, Factory, Invoker, and a thin RPC facade

## Context

The greenfield native host ([`packages/native-greenfield`](../../native-greenfield)) grew a real
background audio thread and concurrent core lifecycle (see
[architecture/07 — Multithreading](../../../architecture/07-multithreading.md) and
[03 — DSP JS runtime](./03-dsp-js-runtime.md)). It works and is TSan/ASan-clean, but the
functionality all landed in **one class**, `BackendRpcService` (~36 methods), and that class is now
doing far too much:

- **fs / config / codec** (`readFile`…`configDir`, `zip`/`unzip`, `savFromJson`) — pure, stateless,
  touches no emulator;
- **emulator lifecycle** (`constructSystem`/`duplicate`/`reload`/`removeSystem`) + **live reads**
  (`readState`/`readSram`/`screenshot`) + **kernel** (`compileScript`/`dspLoadKernel`/`dspSetSystems`)
  + **transport/MIDI** (`sendMidi`/`pressButton`/`setBpm`/`setTransport`/`stageMidiIn`);
- **the audio-thread driver** (`startAudio`/`stopAudio`/`sleepMs`/`audioCaptured`/`systemCount`/
  `drainReleased`) plus the thread, the `SpscRing<DspCommand>` command queue, the
  `SpscRing<DspEvent>` release ring, and the captured/count atomics.

Two concrete smells fall out of the mixing:

1. **Threading leaks into the RPC methods.** `constructSystem`/`removeSystem`/`dspLoadKernel`/
   `dspSetSystems`/`stageMidiIn` each branch on `audioRunning_` (enqueue-a-command vs apply-directly),
   and the read/MIDI methods each branch to fail-safe. A method that answers "add a system" should not
   know whether an audio thread exists.
2. **Every mutation is written twice.** The direct path *and* `applyDspCommand`'s matching case both
   implement "the same operation" — and they've already **diverged** (`removeSystem` deletes in place;
   the `RemoveSystem` case does `removeSystemAndRelease` + hand-back). `applyDspCommand` calls
   `project_.*` directly where it should be calling the same `removeSystem` the RPC does.

Zooming out, the whole codebase re-implements the **emulator build** (ROM resolve → `SameBoyConfig` →
ctor → `onActivate`) four times — `Project::addSystem`, `TestHarnessImpl::loadRom`,
`PluginRpcService::constructInstanceCore`, and greenfield's own `constructSystem`+`reloadSystem` — and
each host layers its *own* concurrency model over the shared `Project` primitive. `Project` is the
only shared "engine," and there's nothing above it.

This doc proposes the decomposition that removes both smells and the duplication.

## The principle

> **Separate *what* the emulator does (the `Engine`) from *how a call reaches it* (the `Invoker`).**

The `Engine` is single-threaded and knows nothing about audio threads, queues, or RPC. The `Invoker`
is the *only* place that knows about threading: it either runs an op on the `Engine` now, or enqueues
it for the audio thread to apply. The RPC layer becomes a thin, thread-agnostic facade over
`(SystemFactory + Engine + Invoker)`. This mirrors the doc-02 split one level up: native owns bytes
and cores; here we further split "own the cores" (Engine) from "decide when the mutation lands"
(Invoker).

## The layers

```
HostRpcService          Engine (owns Project + DspRuntime)        SystemFactory
(fs/config/codec)            ▲            ▲                         (build a core)
                             │ direct     │ drainInto()
                       DirectInvoker   QueuedInvoker ── SpscRing<Cmd> ⇄ SpscRing<Freed>
                             ▲            ▲
                       EngineRpcService (thin; no `if (audioRunning_)`)
                       AudioDriverRpcService (spins the audio thread; owns the QueuedInvoker)
                             ▲
                       BackendFacade (has-a the 3 services; forwards; one object per RPC server)
```

## Components

### `SystemFactory` — the one build path

Kills the 4× build duplication. Runs on the control thread (heavy, non-RT); the `Engine` only adopts
the finished pointer.

Two things are deliberately kept OUT of the generic spec:

- **Feature roles** (mgb / lsdj-sync / kit-patch) — TS behaviours in the DSP kernel, pushed via
  `setSystems`; never native config (per [02 — DSP data model](./02-dsp-data-model.md)). The same leak
  `lsdjSyncMode` on `BackendConstructSpec` is — systems construct **bare**, and dropping that seed +
  the C++ sniffer is a named greenfield follow-up.
- **Backend-specific emulator settings** (SameBoy model / highpass / fast-boot, Mesen equivalents).
  These genuinely *are* native — they're handed straight to the `SameBoySystem` ctor — but they're
  **SameBoy-specific**, so they must not sit in a *generic* build spec. They ride as an **opaque
  per-backend blob** (schema + authority in TS, like a "system"-role config); only the backend module
  decodes it.

So the generic spec carries what every core shares (which ROM, seed bytes, backend kind + its opaque
settings), and construction **dispatches by backend kind** to a per-backend builder. The SameBoy
builder is the *only* place `SameBoyConfig` touches the construct path.

```cpp
// Backend-AGNOSTIC. No SameBoy/Mesen types here.
struct SystemBuildSpec {
    std::string          backendKind;   // "sameboy" | "mesen-nes" | …
    std::string          romPath;       // "" when embedded
    std::string          embeddedRom;   // marker, e.g. "mgb"
    std::vector<uint8_t> sram, savestate;  // seed bytes (may be empty)
    std::vector<uint8_t> settings;      // opaque per-backend config (TS owns its schema); backend decodes
};

// One builder per backend. The SameBoy builder decodes `settings` into a SameBoyConfig
// (model/highpass/fastBoot) and constructs a SameBoySystem — that emulator-specific config IS native
// (it feeds the ctor) but stays INSIDE this module, out of the generic spec.
class SystemBackend {
public:
    virtual ~SystemBackend() = default;
    virtual std::unique_ptr<SystemBase> build(SystemId, const SystemBuildSpec&, double sampleRate) = 0;
};

// A registry keyed by backend kind — the generic build path. null on an unknown kind / unreadable ROM.
class SystemFactory {
public:
    void registerBackend(std::string kind, std::unique_ptr<SystemBackend>);
    std::unique_ptr<SystemBase> build(SystemId, const SystemBuildSpec&, double sampleRate) const;
};
```

### `Engine` — the single source of truth, zero thread awareness

Owns `Project` + `DspRuntime`. Every op is "do it now, on the calling thread." Removed/displaced cores
are **returned** to the caller (who deletes them, or routes them back across a thread) — the Engine
never frees on a hot path. Note there are **no atomics** here: transport changes are ordinary ops the
Invoker routes, so the whole engine is plain single-threaded state.

```cpp
class Engine {
public:
    explicit Engine(double sampleRate);

    // structure — alloc-free swaps vs a pre-reserved Project; `sys` was built off-thread
    SystemId nextSystemId();
    void adoptSystem(std::unique_ptr<SystemBase> sys);
    std::unique_ptr<SystemBase> removeSystem(SystemId id);                         // returns removed
    std::unique_ptr<SystemBase> replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys); // displaced
    std::size_t systemCount() const;

    // DSP kernel
    bool loadKernel(std::span<const uint8_t> bytecode);
    bool setSystems(std::string_view json);
    void stageMidi(std::span<const uint8_t> bytes);

    // transport (plain members; mutated only by the Engine's owning thread)
    void setBpm(double);  void setTransport(bool playing);

    // per block: run kernel → fan sinks to cores → onProcess
    void processBlock(uint32_t frames, float* outL, float* outR);

    // live-state reads (valid only on the Engine's owning thread — see "per host")
    std::vector<uint8_t> readState(SystemId), readSram(SystemId);
    bool screenshot(SystemId, const std::string& path);
    void sendMidi(SystemId, std::span<const uint8_t>);
    void pressButton(SystemId, uint8_t button, bool down);

private:
    Project project_;  DspRuntime dsp_;  bool dspActive_ = false;
    double sampleRate_, ppq_ = 0, bpm_ = 120;  bool transport_ = false;
    std::vector<MidiIn> pendingMidi_;
};
```

### `EngineInvoker` — where "direct vs queued" lives

The `applyDspCommand` switch and the per-method `audioRunning_` branches both collapse into this. The
`DirectInvoker` and the `QueuedInvoker` call the **same** `Engine` methods — one definition each.

```cpp
class EngineInvoker {
public:
    virtual ~EngineInvoker() = default;
    virtual void adoptSystem(std::unique_ptr<SystemBase>)             = 0;
    virtual void removeSystem(SystemId)                              = 0;
    virtual void replaceSystem(SystemId, std::unique_ptr<SystemBase>) = 0;
    virtual void loadKernel(std::vector<uint8_t>)                    = 0;
    virtual void setSystems(std::string)                            = 0;
    virtual void stageMidi(std::vector<uint8_t>)                    = 0;
    virtual void setBpm(double)  = 0;   virtual void setTransport(bool) = 0;
};

// CLI / quiescent: straight through; removed cores drop (delete) here.
class DirectInvoker final : public EngineInvoker {
public:
    explicit DirectInvoker(Engine& e) : engine_(e) {}
    void removeSystem(SystemId id) override { engine_.removeSystem(id); }   // unique_ptr → delete
    // …each op forwards to engine_…
private:
    Engine& engine_;
};

// Threaded: producer half enqueues a POD Cmd; the audio thread's consumer half applies it INTO the
// Engine and ships removed cores back for the control thread to delete. `Cmd` is the thin wire format
// (a small POD union, today's DspCommand) — it carries NO logic; drainInto maps each case to one
// Engine call, and the returned unique_ptr is routed to the release ring.
class QueuedInvoker final : public EngineInvoker {
public:
    void removeSystem(SystemId id) override { commands_.tryPush(Cmd::remove(id)); }
    void adoptSystem(std::unique_ptr<SystemBase> s) override { commands_.tryPush(Cmd::adopt(s.release())); }
    // …each op pushes a Cmd…

    void drainInto(Engine& e);                    // audio thread, block start
    std::unique_ptr<SystemBase> popReleased();    // control thread: drain + delete
private:
    SpscRing<Cmd,   256> commands_;   // control → audio
    SpscRing<Freed, 256> released_;   // audio  → control (raw SystemBase* to delete)
};
```

### The RPC surface — three classes

```cpp
// fs + config + codec. Pure, stateless, no Engine, no threads. Reusable verbatim across hosts.
class HostRpcService {
public:
    std::optional<Bytestring> readFile(std::string);  bool writeFile(std::string, std::vector<uint8_t>);
    /* fileExists, rename, listDir, deleteFile, canonicalize, readFilePrefix, configDir */
    Bytestring zip(std::vector<ZipInput>);  std::vector<ZipEntry> unzip(std::vector<uint8_t>);
    Bytestring savFromJson(std::string);
};

// The emulator surface as THIN RPC over (SystemFactory + EngineInvoker). NO threading branches.
class EngineRpcService {
public:
    EngineRpcService(Engine& e, EngineInvoker& inv, SystemFactory& f, double sr);

    std::optional<uint32_t> constructSystem(BackendConstructSpec spec) {
        auto sys = factory_.build(engine_.nextSystemId(), toBuildSpec(spec), sampleRate_);
        if (!sys) return std::nullopt;
        const auto id = sys->id();
        if (spec.replaceId) invoker_.replaceSystem(*spec.replaceId, std::move(sys));
        else                invoker_.adoptSystem(std::move(sys));
        return id;                                   // id known up front → returns even when queued
    }
    bool removeSystem(uint32_t id)             { invoker_.removeSystem(id);           return true; }
    bool dspLoadKernel(std::vector<uint8_t> bc){ invoker_.loadKernel(std::move(bc));  return true; }
    bool dspSetSystems(std::string json)       { invoker_.setSystems(std::move(json));return true; }
    bool setBpm(double b)                      { invoker_.setBpm(b);                  return true; }
    bool setTransport(bool p)                  { invoker_.setTransport(p);            return true; }
    std::optional<Bytestring> readState(uint32_t id) { return engine_.readState(id); }  // see "per host"
private:
    Engine& engine_;  EngineInvoker& invoker_;  SystemFactory& factory_;  double sampleRate_;
};

// Test/dev harness control (only the threaded host mounts it). Spins the audio thread that OWNS the
// Engine, drains the QueuedInvoker into it each block, publishes observation.
class AudioDriverRpcService {
public:
    AudioDriverRpcService(Engine& e, QueuedInvoker& inv);
    bool startAudio();   // loop: while running { inv.drainInto(engine); engine.processBlock(...); publish }
    bool stopAudio();  bool sleepMs(double);
    AudioCaptured audioCaptured();          // audio → control atomics
    uint32_t systemCount();  uint32_t drainReleased();   // popReleased() → delete
};
```

### `BackendFacade` — one object per RPC server

`rpcpp::TypedRpcServer` binds a **single** service object, and the codebase has no multi-service
composition. Rather than add that capability, a thin facade has-a the three services and forwards —
concerns stay separated (all logic is in the sub-objects) while the single `__rpcSend` channel keeps
one registration target.

```cpp
class BackendFacade {
public:
    BackendFacade(/* wires Engine + Invoker + Factory + the 3 services for this host's mode */);
    // fs/config/codec → host_ ; emulator/kernel → engine_ ; audio-thread → driver_
    std::optional<Bytestring> readFile(std::string p) { return host_.readFile(std::move(p)); }
    std::optional<uint32_t> constructSystem(BackendConstructSpec s) { return engine_.constructSystem(std::move(s)); }
    bool startAudio() { return driver_.startAudio(); }
    // …one forwarding line per RPC method…
private:
    HostRpcService        host_;
    EngineRpcService      engine_;
    AudioDriverRpcService driver_;
};
```

## What this deletes

- **`applyDspCommand`** (its cases become `QueuedInvoker::drainInto` calling `Engine` methods).
- **Every `if (audioRunning_)` branch** in the RPC methods (the Invoker *is* the mode).
- **The direct-vs-queued divergence** (one `Engine::removeSystem`, called by both invokers).
- **The `bpm_`/`transportPlaying_` atomics** (transport is a queued op like any other).
- **Three of the four build copies** eventually collapse toward `SystemFactory` (CLI/plugin can adopt
  it incrementally; see [07 — Host consumption](./07-host-consumption.md)).

## Composition per host

- **CLI / test (`retroplug-cli`, quiescent)** — `BackendFacade` with a `DirectInvoker` and **no** audio
  thread. Live reads (`readState`/`screenshot`) are always safe; the "fail-safe-when-running" guard
  disappears because there's no run.
- **Threaded greenfield host** — `BackendFacade` with a `QueuedInvoker` + the `AudioDriverRpcService`.
  The audio thread owns the Engine; the host simply does **not** expose live reads during a run until
  the snapshot triple-buffers land (a composition choice, not a per-method `if`).
- **Plugin (future, the endgame)** — the same `Engine` folded into `PluginDSP`'s audio loop, driven by
  the plugin's existing `CommandQueue` acting as the `QueuedInvoker`'s transport.

## Considered and rejected (for now): an rpcpp SPSC transport

The maintainer's idea — add a new rpcpp **transport** that ferries calls over an SPSC ring, so *one*
`EngineService` is exposed over a QuickJS server (from TS) **and** an SPSC server (cross-thread) with
no hand-written `Cmd` union — was evaluated against the rpcpp source. Findings:

- **Supported already:** the `Transport` concept is just `send(output_t)` (an `SpscTransport` is a
  clone of the existing `QuickJSTransport`/`QueueTransport`), and **one service instance can back two
  servers** (`TypedRpcServer` holds the service by reference; registration is `template<class Server>`).
- **Blocked without rpcpp surgery:** params are hardwired through `rfl::Generic`
  (`RpcRequest.params` + `rfl::from_generic<ParamTuple>`), so even a POD codec allocates per drained
  call; dispatch uses `unordered_map<string,std::function>`; **raw pointer params don't compile**
  (`addSystem(SystemBase*)` would have to become `addSystem(uint64 handle)`); and there is **no C++
  request encoder** — the control-thread "push a call" side would be written from scratch.

**Decision: not worth it now.** The only calls that ever cross to the audio thread are rare,
user-initiated structural ops whose *application* already allocates — so a ~60-line typed-command ring
(`QueuedInvoker`) is simpler, already RT-adequate, and needs no rpcpp changes. If the op set grows
enough that the small `Cmd` enum becomes the maintenance cost, revisit — at that point `QueuedInvoker`
is the *only* class that would change.

## Migration / sequencing (tests green throughout)

1. **`SystemFactory`** — extract the build from `constructSystem`/`reloadSystem`; both call it. No
   behaviour change. (native + pure suites green.)
2. **`Engine`** — move `Project`/`DspRuntime` ownership + the mutation/read/processBlock bodies out of
   `BackendRpcService` into `Engine`. `BackendRpcService` temporarily calls `engine_.*` directly
   (still one class, but the logic has moved). Green.
3. **`EngineInvoker`** — introduce `DirectInvoker`/`QueuedInvoker`; `QueuedInvoker::drainInto` replaces
   `applyDspCommand`; the RPC methods lose their `audioRunning_` branches and call `invoker_.*`. The
   `AudioDriver` owns the `QueuedInvoker` and the thread. Re-run TSan + ASan
   ([`tools/run-greenfield-sanitizer.sh`](../../../tools/run-greenfield-sanitizer.sh)) — the seam is
   unchanged, so it must stay clean.
4. **Split the services** — carve `HostRpcService` / `EngineRpcService` / `AudioDriverRpcService` out
   of `BackendRpcService`, and reduce `BackendRpcService` to the `BackendFacade` (pure forwarding).
   Registration stays one server / one object.

Each step is independently green and independently committable.

## Status / links

Implemented (the four-step sequence above landed as four commits, each independently green;
the audio-thread seam stayed TSan/ASan-clean). The named deferred items below remain open.

- [architecture/07 — Multithreading](../../../architecture/07-multithreading.md) — the audio/UI seam
  this decomposition cleans up on the greenfield side.
- [03 — DSP JS runtime](./03-dsp-js-runtime.md) — the `DspRuntime` the `Engine` owns.
- [07 — Host consumption](./07-host-consumption.md) — how CLI / plugin / standalone consume this layer
  and where `emu.*` recedes to a test/dev facade.
