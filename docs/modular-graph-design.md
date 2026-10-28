# Design: the modular node-graph engine

**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B3b built (Phases C to E not yet built).

RetroPlug's long-term direction is a modular system in the spirit of Reaktor: emulated consoles (and
the real hardware they can drive) appear as nodes in a graph alongside DSP effects and routing nodes.
This document is the design for the first project on that road: **the node contracts and the graph
engine**. It keeps today's UI, CLI and `.rplg` format unchanged; they build graphs from the settings
they already have.

Everything here is forward-looking. The [spec/](../spec/README.md) set describes the build as it is;
as each phase below lands, the spec doc it touches is rewritten to match.

---

## 0. Decisions taken

| Question | Decision | Consequence |
|---|---|---|
| What comes first? | **Contracts + graph engine** | The package split, web host and downloads are later projects that build on these contracts (§11) |
| What can a TS node process? | **The same ports as a C++ node**, audio included | Fully symmetric contract; heavy DSP stays C++ by convention, not by rule (§4.9) |
| What is "LSDj support" relative to a SameBoy? | **Nodes + extensions** | Signal work is a node; control-plane features (songs, assets, menus, views) are an extension attached to a node instance (§2.6) |
| What may a downloadable pack contain? | **TS, assets, and native code** | The node interface is a stable **C ABI** (CLAP-style), and statically linked packages use the very same ABI (§4) |
| How far does the Pico go? | **Nodes and a tiny runtime** | The executor is no-heap by construction; node memory is supplied by the host; plans export as data (§5.8) |
| Where is the graph planned? | **A C++ planner inside RetroPlug** | TS owns the graph document; native validates and plans it; one executor runs plans everywhere (§5) |
| Can several wires feed one input? | **No. Zero or one wire per input, every port kind** | Combining is always an explicit node (`+`, `merge`); the executor never allocates hidden mix buffers (§3) |
| Implicit channel adaptation? | **None** | Mismatches are errors; `join` / `split` / `downmix` / `relabel` fix them explicitly (§3) |
| What is a control value? | **A number, typed by the descriptor** | float / int / enum / bool all travel as a double; enums save as labels, run as indices (§2.4) |
| Strings, buffers, ROM images on wires? | **Resources, passed by handle** | Created and freed only on the control thread; plans pin them (§2.5) |
| Is the graph saved? | **Not in this project** | `.rplg` and its `K_PROJECT` stay as they are; a pure TS `buildGraph` derives the graph from the project model (§7.2) |
| UI owned by nodes and extensions? | **Views**, defined now, migrated later | The LSDj HD player and BlipToaster's synth panel are views; the contract is in this spec, the move happens in the package-split project (§2.7) |
| reflect-cpp | **Upgrade 0.24 -> 0.26**, including the rpcpp pin inside the dpf.js submodule | Needed for JSON Schema defaults on settings (approved) |

---

## 1. Summary

### What this project builds

1. **A node contract** with one C ABI, filled three ways: C++ nodes (avendish-shaped, reflected with
   reflect-cpp), TypeScript nodes (`defineNode`), and nodes in downloaded native packs. Every node
   returns a descriptor JSON of the same shape.
2. **A graph engine**: a C++ planner that turns a graph document into an immutable plan on the control
   thread, and a no-heap executor that runs plans on the audio thread, in offline renders, and on the Pico.
3. **System nodes**: SameBoy and Mesen cores as ordinary nodes, with their console-specific knowledge
   moved out of `Engine`, `Project` and `BlockRunner` and into the nodes themselves.
4. **An extension and view contract** for everything that sits on top of a node (LSDj songs and assets,
   the HD player, BlipToaster's panel).
5. **A TS graph builder** so the existing UI and CLI keep working unchanged: they build graphs from the
   project model they already have.

### Why

The current code is not console-neutral where it claims to be. `Engine`, `Project`, `BlockRunner` and
`EngineRpcService` include the cores and `dynamic_cast` to `SameBoySystem` / `MesenNesSystem` for
config, channel splits, link groups and LSDj MI.OUT. `SystemBase` carries GB serial, the N8 FIFO and
per-console button enums. The six audio routing modes are six hand-written router classes, each a
hard-coded topology. On the TS side every registry is populated by hard-wired imports, and every
bundle carries every cart. A graph engine turns all of that into data.

### Non-goals for this project

- Persisting the graph (and any `K_PROJECT` bump).
- A modular graph-editing UI.
- The package split, per-product bundles, Mesen per-console compile-out.
- The web host, downloadable packs, and their trust model.
- Moving menus, song/asset catalogs and views into extensions (contract defined here, migration later).
- A multi-threaded executor (the plan format leaves room for it).

---

## 2. Concept model

### 2.1 Node types and instances

A **node type** has a stable id (`rp.sameboy.gb`, `rp.dsp.gain`, `rp.lsdj.sync`), a version, and a
descriptor. A **node instance** lives in a graph under an id minted by TS, exactly as system ids are
minted by TS today; native never mints an id.

### 2.2 Port kinds

| Kind | Carries | Examples |
|---|---|---|
| `audio` | N float channels per block, with a layout (§3.4) | system `mix`, `+` in/out |
| `events` | MIDI-shaped messages stamped with a frame offset | host MIDI in, LSDj MI.OUT notes |
| `bytes` | a timestamped byte stream | GB serial, N8 FIFO, SMS controller port |
| `control` | numeric value changes stamped with a frame offset | every param, LFOs, envelope followers |
| `link` | coupling, not data | the Game Boy link cable |
| `resource` | a handle to a typed resource (§2.5) | a sample buffer, a ROM image, a string |

Transport is not a port. Tempo, ppq at block start, play state and sample rate arrive in every node's
block context, as in CLAP.

### 2.3 The three kinds of input

| | Types | Wireable | Changes applied |
|---|---|---|---|
| **Ports** | audio, events, bytes, link, resource | yes | per block |
| **Params** | float, int, enum, bool | yes (each param is also a `control` input) | live, timestamped to the frame |
| **Settings** | anything JSON Schema can express | no | control plane only; prepared, then swapped in |

The test for param versus setting is whether it can change live. LSDj sync mode, tempo divisor and gain
are params. A ROM, a core's model and a region are settings, because changing them rebuilds or resets
the core.

A **wired param is driven entirely by its wire**; an unwired param keeps its own value. To modulate
around a knob, wire a constant node and an LFO into a `+` (Reaktor's convention).

### 2.4 Control values versus events

The difference is state versus occurrence, not data type.

- A **control value is state**: it has a current value at every moment, a default, and can be saved.
  The latest change wins, so a receiver may coalesce rapid changes, and a float may be smoothed.
- An **event is an occurrence**: it has no current value, every one matters, order matters, and merging
  two changes the meaning. Note-on, MIDI clock, "press Start" and "reset" are events.

On the wire every control value is a **double**. The descriptor says how to read it: continuous range,
integer steps, enum or bool. Any control can therefore drive any control (an LFO can sweep an enum; the
receiver rounds and clamps), values are fixed-size, and there is one wire type to check.

An **enum has two representations**: the **index** at runtime, and the **label** in saved state and in
the JSON Schema (`"mode": "midi-sync"`, never `3`). Reordering an enum can never corrupt an old project,
and migrations stay readable. This is today's `roleConfigForNative` label-to-ordinal step, made one
shared rule that C++ and TS both follow.

### 2.5 Resources

A **resource** is a typed block of data that lives outside the wires: an audio buffer, a wavetable, a
ROM image, a kit, a string. Nodes pass **handles**; a handle is fixed-size and moves through the
executor with no copy and no allocation. A `resource` port is typed by resource type, so the planner
rejects a ROM wired into a sampler. Packages register their own resource types (`lsdj.kit`,
`risa.dmc-kit`).

**Lifetime rule: the audio thread can pass handles around but can never create or free a resource.**

- Every resource is created on the control thread (`putResource`).
- Pins come from two places. A document binds a resource straight to a resource input
  (`"resources": {"sample": "res:12"}`), and the plan **pins** every such binding; the pin drops only when
  that plan comes back on the release ring. A node that references a resource from its *settings* (a
  system node's ROM) acquires it through the host while preparing those settings, and that settings object
  holds the pin until it is released. (That host service arrives in Phase B with the first system node;
  Phase A's host hands nodes no services yet.)
- A resource counts its **owner** (its creator, who drops it once with `releaseResource`) apart from its
  **pins**, and is freed only when the owner has dropped it and no pin remains. Counting them together
  would let a duplicate release (a dispose run twice, a retried RPC) take a plan's pin with it and free
  a buffer the audio thread is still reading. Once dropped, a resource can no longer be bound by a new
  document, even while an older plan still pins it, so whether a document plans does not depend on when
  the last plan happened to be reclaimed. Ids are never reused, so a stale `res:N` is refused rather than
  binding whatever took its place. Resource types, like node type and package ids, are plain ids: no
  whitespace or control character, so the C string a node reads is the string the host checked.
- A handle emitted at runtime must therefore point at something a node in the plan already holds (a
  sample bank picking one of its 16 slots; a recorder writing into the buffer its settings asked for),
  so the audio thread never holds the last reference.

**Mutability rule:**

- **Immutable resources** (samples, strings, ROM images) are shared by any number of readers in any order.
- **A mutable resource** (a record buffer, a shared delay line, a system's live RAM) has exactly **one
  writer port**: a resource input declared with write access (`"access": "write"`), always bound to its
  resource in the document and never wired, so the one-writer rule is checked over bindings. Readers are wired
  from the writer's resource output, so the planner orders them after the writer within the block, as it
  does for audio; binding a reader to the same resource directly is rejected, because nothing would order
  it. A cycle through a resource is broken by marking one of its data wires feedback (§3.3); a resource
  wire itself cannot be one.

**Strings** on a wire are handles to immutable string resources, interned on the control thread (Pd's
symbols). Settings remain the place for values whose change needs a rebuild.

Where resources pay off: the kit patcher's processed sample buffers (previewed through a sampler,
encoded into a kit); patched ROMs (asset overrides become "build a new ROM resource and point the system
node's ROM setting at it", replacing today's in-memory folding); recording a console into a buffer for
resampling; and memory taps (a system's live RAM as a mutable resource feeding a "memory to control"
node, so an LSDj table value or a NES APU register becomes a modulation source).

### 2.6 Extensions

An **extension** attaches to a **node instance**, chosen by a matcher over that node's identity (its
type, and the ROM title / marker / header that `rp.system.rom/1` publishes). Extensions are TypeScript
and run on the control plane. They have no ports. An extension can contribute:

- **nodes it inserts and wires** (the LSDj extension adds `lsdj.sync` feeding the SameBoy's `serialIn` input);
- song and asset **catalogs**;
- **actions** and **menu** entries (these two arrive with the package split, once the menu's item model
  is extracted from `menuDefs.ts`; §7.7);
- **views** (§2.7);
- its own **persisted settings**.

It reaches its node through the node's capabilities (memory, SRAM, ROM, buttons, state), always via host
RPC routed through the snapshot / command-ring doors (§6.4).

### 2.7 Views

A **view** is a UI component that renders for one node instance: Reaktor's structure/panel split. The
graph is the structure; nodes and extensions contribute panels. Today's app is the panel view of a
graph the menus build.

```ts
views: [{
  id: "bliptoaster.panel", label: "Synth",
  placement: "tile",                 // or "overlay" | "fullscreen"
  default: true,
  component: BlipToasterPanel,       // React, on today's LVGL UI
  needs: { memory: [{ region: "wram", range: [0xc000, 0xc200] }], rate: "frame" },   // optional
}]
```

| Placement | Meaning | Today's example |
|---|---|---|
| `tile` | replaces the node's tile content | a BlipToaster synth panel, NES video one switch away |
| `overlay` | draws over the node's own output | the LSDj / risa overlays `EmulatorTile` always mounts |
| `fullscreen` | takes the window; Escape returns to the grid | the LSDj HD player |

- Every node instance has a list of available views and one **active** view. A system node always
  contributes `screen` (the framebuffer via `rp.system.video/1`). An extension's `default` view beats
  the node type's default; the user's choice persists per instance; the tile gets a "View" row.
- **Input goes to the active view first**, then falls through to the node's `buttons`. That is what
  keeps LSDj playable under the HD player.
- A view reads and writes only node params, extension settings, snapshots, and events it sends to a
  node. BlipToaster's panel edits the params of a `bliptoaster.synth` node, which turns param changes
  into FIFO writes; the DAW automation lanes map to the same params, so panel and automation never
  disagree.
- `needs` lets the host arm only the memory ranges a view reads, the "arming seam"
  [spec/07](../spec/07-remaining-work.md) wants for the HD player. Optional in v1.
- Views are always TS, even in a pack whose nodes are native.

### 2.8 Packages

A **package** is the unit of linking and of downloading. One manifest whatever it holds: id, version,
dependencies, an optional native part, an optional TS part, and assets. It contributes node types,
extensions, views, ROM matchers, resource types and CLI commands. It is either statically linked into a
product or downloaded. The package split itself is a later project (§11); this project defines the
manifest and the native entry point (§4.1) so that split has a target.

---

## 3. Wiring rules

### 3.1 One wire per input

**Every input accepts zero or one wire, for every port kind.** Outputs fan out to any number of inputs
for free (the readers share one buffer). Combining is always explicit:

- `+` for audio and control (input count is a setting; channels follow `match`);
- `merge` for events, ordered by frame;
- no generic merge for `bytes`: interleaving two serial streams corrupts the protocol, so only a
  protocol-aware node may combine them.

An unconnected audio input reads silence; an unconnected events / bytes input reads an empty queue.

### 3.2 No implicit adaptation

A channel-count or layout mismatch is an error. The fixes are explicit nodes:

- `join` concatenates inputs into one port and declares the output layout as a setting (`stereo`, `discrete(2)`);
- `split` breaks any port into monos;
- `downmix` sums channels from a layout its settings name explicitly (`{"from": "5.1", "to": "stereo"}`,
  so it never infers what a source means): ITU-style coefficients for 5.1 -> stereo, an equal sum (no
  scaling, L + R) for anything -> mono, which is exactly today's 1 Ch / Inst bus;
- `relabel` asserts a layout (`discrete(6)` -> `5.1`).

Duplication needs no node: fan one mono output into both inputs of a `join`.

### 3.3 Cycles and feedback wires

A wire may be marked `feedback: true`. Only feedback wires may close a loop; they deliver their value one
block late. A cycle with no feedback wire is rejected, and the error names the loop. Audio, events, bytes
and control wires can be feedback; a resource or link wire cannot (a handle or a link has no previous
block to deliver). Data between two members of one coupled group needs a feedback wire too: the group
steps as one unit, so a same-block wire inside it would have no order. Messages on a feedback events or
bytes wire keep the frames of the block that produced them, so a reader sees last block's coordinates,
and a frame can lie past the end of a shorter reading block. The Game Boy's `serialOut` into its
`rp.lsdj.sync` node (the DSP kernel's input until B3b) is the first feedback wire to carry frames other
than 0: each byte at the frame its core completed it, which master sync times its stop by
([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)).

LSDj needs this today: `lsdj.sync` feeds the GB's `serialIn`, and the GB's `serialOut` comes back
to `lsdj.sync` for MI.OUT. The current engine already gives that return path exactly one block of latency
(it gathers `serialOutLog_` for the next block), so the graph makes existing behaviour visible. A wire
flag was chosen over Reaktor's auto-inserted delay because the delay's position matters: the sync-to-GB
edge must stay in the same block.

### 3.4 Channels and layouts

An audio port carries N channels, stored non-interleaved (one float array per channel, as today's
`float* const* outs`, CLAP and VST3), with a layout:

| Layout | Channels | Example |
|---|---|---|
| `mono` | 1 | a NES pin, `downmix` output |
| `stereo` | 2 (L, R) | Game Boy `mix`, `ch1`-`ch4` |
| `5.1`, `7.1`, ... | standard speaker order | a surround panner's output |
| `discrete(n)` | n unrelated channels | a bundle of the NES's 5 core channels |

A port's channel spec is one of: a **fixed** count and layout; **`any`**; or **`match("<port>")`**,
which copies both count and layout from another port. The planner propagates counts and layouts along
wires, so one gain or filter node works on mono, stereo, or 8 channels (VCV Rack's polyphonic cables).
Only layout-aware nodes (panners, `downmix`, wideners) declare a specific layout.

There is no channel limit in the ABI; the planner enforces a sanity cap (64), and on the Pico the limit
is whether the plan fits the arena.

### 3.5 Dynamic ports

Three mechanisms, each for a different kind of dynamism:

1. **Every output exists; only connected ones are computed** (system nodes). A Game Boy always exposes
   `mix` and `ch1`-`ch4`; a NES always exposes `mix`, `pins.*` and `channels.*`. Unconnected outputs get
   a shared scratch buffer, never null, so a node that ignores the mask cannot crash; the connected mask
   is what lets it skip the work. When a plan swap changes which outputs are connected, the node gets an alloc-free
   `ports_changed` call on the audio thread and arms or disarms its taps (today's `syncSplitPlan` /
   `setChannelExportMode`, owned by the node instead of the engine). Connecting the pin outputs *is*
   choosing pins; there is no output-mode setting. A core that can run only one tap at a time declares
   an **exclusive group** (`exclusive: [["pins.*", "channels.*"]]`) and the planner reports a clear error
   instead of producing silence.
2. **Counts chosen by the user come from settings** (an 8-input `+`, a 4-way splitter). Changing the
   setting means a re-layout and a replan; the planner rejects wires to ports that no longer exist, and
   the document's owner (today the graph builder, later the modular UI) removes them. A node that can keep
   its state through the new layout reshapes rather than rebuilds (`RP_PREPARE_RESHAPE`, §4.3): the instance
   is kept and its ports are rebound by id, as the MIDI routing node's are whenever a system comes or goes
   ([B3a §2.4](modular-graph-phase-b3a-design.md), [B3b §2](modular-graph-phase-b3b-design.md)).
3. **Inferred channel counts** (`any` / `match`, §3.4) for the DSP library.

### 3.6 Host bus layout

`rp.host.audio-out` follows the host's bus layout: the plugin's four stereo pairs are four stereo ports
`out.1-2` ... `out.7-8`; the standalone's layout comes from its `Out Channels` setting. Inside the graph
any layout works. Whether a 5.1 *port* reaches a DAW as a surround bus depends on the plugin format: DPF
predefines only mono and stereo port groups today, so until it exposes surround buses a 5.1 signal leaving
the plugin goes through `split` / `join` onto stereo pairs.

---

## 4. The node ABI

### 4.1 One C ABI, three ways to fill it

Every node, whether statically linked C++, a downloaded native pack, or TypeScript, reaches the planner
as the same C structures. The shape follows CLAP: a small core table, plus optional interfaces looked up
by versioned string id. Statically linked packages are registered through the same entry point as
downloaded ones, so there is one loading path.

A sketch, abridged from [rp_node.h](../packages/native/src/graph/rp_node.h), which is the reference:

```c
#define RP_ABI_VERSION 1

typedef struct rp_package {
    uint32_t abi_version;
    const char* manifest_json;
    uint32_t node_type_count;
    const struct rp_node_type* const* node_types;
} rp_package;  // no init / deinit in ABI v1: a pack needing one-time setup does it in its first create,
               // thread-safely (std::call_once): several hosts in one process may create concurrently

struct rp_host {   // host services; grows through get_interface, never by reshaping this table
    uint32_t abi_version;
    void* ctx;
    void* (*alloc)(void* ctx, size_t size, size_t align);
    void  (*free)(void* ctx, void* ptr);
    void  (*log)(void* ctx, int level, const char* message);
    const void* (*get_interface)(const struct rp_host* host, const char* id);
};

typedef uint32_t rp_status;  // RP_OK / RP_FAULT: fixed-width, since an enum's size is not portable

typedef struct rp_settings { const char* data; size_t size; uint32_t encoding; } rp_settings; // JSON on desktop

// Every type-level call but destroy is handed the type it is made through, so one set of functions can serve
// several types, each telling itself apart by its ctx (the script host serves every TS node type so).
typedef struct rp_node_type {
    const char* id;
    const char* (*descriptor)(const struct rp_node_type*);                       // control
    size_t   (*layout_for)(const struct rp_node_type*, const rp_settings*, char* out, size_t capacity,
                           rp_error* err);     // control: the layout these settings give, before any instance
    bool     (*query_memory)(const struct rp_node_type*, const rp_settings*, rp_memory_req* out,
                             rp_error* err);   // control: size + align
    rp_node* (*create)(const struct rp_node_type*, const struct rp_host*, void* memory, const rp_settings*,
                       rp_error* err);         // control
    void     (*destroy)(rp_node*);                                               // control
    uint32_t flags;  // RP_TYPE_*: promises the type makes the host (RP_TYPE_STATELESS_PREPARE, §4.3)
    void*    ctx;    // the type's own data, for functions that serve several types; null otherwise
} rp_node_type;

// rp_prepare_result, like rp_status, is a uint32_t with named constants: RP_PREPARE_SWAP, RP_PREPARE_REBUILD,
// RP_PREPARE_INVALID, RP_PREPARE_RESHAPE (§4.3)
typedef struct rp_node_vtable {
    bool        (*activate)(rp_node*, double sample_rate, uint32_t max_frames, rp_error*); // control
    void        (*deactivate)(rp_node*);                                   // control
    void        (*ports_changed)(rp_node*, const uint8_t* connected, uint32_t output_count); // audio, alloc-free
    rp_status   (*process)(rp_node*, const rp_process*);                   // audio, alloc-free
    rp_prepare_result (*prepare_settings)(rp_node*, const rp_settings*, void** prepared, rp_error*); // control
    void        (*commit_settings)(rp_node*, void* prepared, void** old);  // audio, pointer swap
    void        (*release_settings)(rp_node*, void* old);                  // control
    const void* (*get_interface)(rp_node*, const char* id);                // any
} rp_node_vtable;
```

A downloaded pack exports `rp_package_entry` from a dynamic library; a statically linked package hands
the same struct to a **generated** registration list. (A registration object in a static library is
dropped by the linker when nothing references it; a generated `rp_register_packages()` per product
avoids that.)

### 4.2 Memory is supplied by the host

`query_memory(settings)` returns the size and alignment an instance needs; `create(host, memory,
settings)` constructs into memory the host provides. On the desktop that memory is an allocation on the
control thread; on the Pico it is a slice of a static arena. A node that needs a heap beyond its block
(an emulator core) declares the `heap` capability and uses the host allocator on the control thread only.

### 4.3 The instance calls and their threads

| Call | Thread | Purpose |
|---|---|---|
| `layout_for` (on the type) | control | resolved ports, params and exclusive groups for a settings object, before any instance exists |
| `activate` / `deactivate` | control | sample rate, maximum block size; must preserve state (§5.3) |
| `ports_changed` | audio | connected-output mask changed at a plan swap, or a reshape changed the layout |
| `process` | audio | one block; no allocation, no exceptions |
| `prepare_settings` / `commit_settings` / `release_settings` | control / audio / control | the settings swap |
| `get_interface` | any | optional interfaces |

**How an instance takes new settings.** The planner asks `prepare_settings` on every settings change, whether or not
the layout moved, and it answers one of four results: **swap** (`RP_PREPARE_SWAP`: the layout is unchanged, and the
prepared object commits at the plan swap), **reshape** (`RP_PREPARE_RESHAPE`: keep this instance with the layout
`layout_for` gives the new settings; the planner rebinds its ports by id, carries over by id what its params,
control outputs and resource slots hold, and commits at the swap, §5.3), **rebuild** (`RP_PREPARE_REBUILD`: a fresh
instance, a new ROM say) or **invalid**. A swap for a changed layout is a `node-contract` error (before B3a it was a
silent rebuild), and a reshape that leaves the layout as it was is a swap. A reshape that changes the layout always
brings a `ports_changed`, after its `commit_settings`, so the node has the settings the mask belongs to.

**What a commit holds back.** Unless a type declares `RP_TYPE_STATELESS_PREPARE` in `rp_node_type.flags`, the host
never calls its `prepare_settings` while a `commit_settings` for the same node may still be in flight, so the node
may read its current settings there to compare against; `setGraph` waits for such a commit to be adopted first
(§5.3). A type that declares the flag promises its prepare reads nothing a commit writes, so its commits never
make `setGraph` wait. Every script type declares it ([B3a §2.4](modular-graph-phase-b3a-design.md)); a C++ node does
with `static constexpr bool stateless_prepare`, which rules out `needs_rebuild`. The wait is global: one commit to a
type without the flag holds back an edit that touches only nodes with it (a gate per instance is possible later). And
a `node.hpp` node without `needs_rebuild` already prepares statelessly, so for C++ nodes the flag could be derived
rather than declared.

**Ports and params are resolved per instance**, because a `+`'s input count or a mixer's width depend on
its settings. The type descriptor shows the defaults (for documentation and future UIs). Everything is
addressed by **stable string ids** in documents and saved state; the planner maps ids to indices for the
audio thread (CLAP's lesson: stable ids, never names).

`process` returns a status. A node that faults (an emulator that wedges, a bad input it cannot handle)
returns a fault instead of throwing; §9 says what the executor does with it.

### 4.4 Optional interfaces

| Interface | Purpose |
|---|---|
| `rp.state/1` | save and load node state |
| `rp.step/1` | prepare / step-if-below-target / finish, for coupled groups (§5.5) |
| `rp.link/1` | peer connection for `link` wires |
| `rp.snapshot/1` | what the node publishes into the `SnapshotRegistry` |
| `rp.system.memory/1`, `.sram/1`, `.state/1`, `.rom/1`, `.video/1`, `.buttons/1`, `.debug/1` | system capabilities (§6.4) |

A missing interface returns null. Interface ids carry their own version, so an interface can evolve
without bumping `RP_ABI_VERSION`.

### 4.5 Capability flags

The descriptor declares what a node needs from its host: `heap`, `threads`, and any future flags. The
planner checks them against the target: an emulator core declares `heap`, so the Pico planner rejects it
with a clear error rather than at flash time.

### 4.6 The descriptor JSON

C++ and TS nodes produce the same shape:

```json
{
  "id": "rp.dsp.gain",
  "version": "1.0.0",
  "name": "Gain",
  "category": "dsp/utility",
  "requires": { "heap": false, "threads": false },
  "ports": [
    { "id": "in",  "dir": "in",  "kind": "audio", "channels": { "any": true } },
    { "id": "out", "dir": "out", "kind": "audio", "channels": { "match": "in" } }
  ],
  "params": [
    { "id": "gain", "name": "Gain", "type": "float", "min": -60, "max": 12, "default": 0, "unit": "dB" }
  ],
  "settings": { "$schema": "https://json-schema.org/draft/2020-12/schema", "type": "object", "properties": {} }
}
```

An enum param is `{ "id": "mode", "type": "enum", "values": ["off", "midi-sync", "master"], "default":
"midi-sync" }`; a resource port is `{ "id": "sample", "dir": "in", "kind": "resource", "resourceType":
"audio-buffer" }`; a fixed audio port is `"channels": { "count": 2, "layout": "stereo" }`.

An events or bytes output may declare its queue's capacity, `"capacity": { "messages": 1024, "bytes": 65536 }`,
either part or both: a part it leaves out takes the default (256 messages, 4096 bytes), and neither may pass
1 Mi messages or 16 MiB, so a typo cannot allocate gigabytes. Only an output: a wire carries its source's queue,
and a feedback wire's front is sized from the source too, so a capacity on an input would bound nothing, and
both parsers (and the C++ layer, which gives an input no `sized`) refuse one rather than ignore it.

The descriptor is emitted in one canonical spelling (keys sorted, no whitespace, numbers and strings exactly
as JSON.stringify writes them), so its ports, params and metadata are **byte-stable**: across compilers,
and between a C++ node and a TS node of the same declared shape (§10). The one exception is a node's
`settings` JSON Schema: reflect-cpp and zod generate structurally different schemas for the same
constraint (reflect-cpp writes min / max as an `allOf` pair), so settings schemas are generator-specific.
A node with no settings writes `{"type":"object"}` in both languages, which keeps that case identical.

### 4.7 C++ nodes

```cpp
struct Gain {
    static constexpr auto id = "rp.dsp.gain";
    struct { rp::audio_in<rp::any> in; }                                         inputs;
    struct { rp::audio_out<rp::match<"in">> out; }                               outputs;
    struct { rp::param<float, "Gain", rp::range{-60, 12, 0}, rp::unit::db> gain; } params;
    void process(const rp::Block&) noexcept;
};
RP_NODE(Gain);   // emits the rp_node_type + vtable
```

- **Field names become port and param ids**; metadata comes from the wrapper types (non-type template
  parameters). C++ enum params take their labels from the enumerator names via reflect-cpp's enum
  reflection, converted to kebab-case (`MidiSync` -> `"midi-sync"`), so C++ and TS labels follow one
  convention. The member names `inputs`, `outputs` and `params` are reserved for the three port structs.
- **Settings are plain named reflect-cpp structs**, so their JSON Schema is `rfl::json::to_schema`, with
  `rfl::Validator` / `rfl::Description` / `rfl::DefaultVal` for ranges, descriptions and defaults. Named,
  and declared in a named namespace, because reflect-cpp derives `$defs` keys from compiler type names,
  which differ between gcc and clang for unnamed structs and for anonymous namespaces (gcc
  `rp__graph___anonymous___X`, clang `rp__graph___anonymous_namespace___X`).
- **Ports and params get their descriptor from our own table**, not from reflect-cpp's schema output, so
  descriptors stay byte-stable.
- **The field walk sits behind one `rp::fields_of<T>`**, built on reflect-cpp's `named_tuple_t` /
  `num_fields`. Measured: a 128-field node costs ~12 s on gcc 13 at -O0 (clang 18: ~3 s; 16 fields: ~1 s).
  If gcc build times bite on big nodes, `fields_of` can switch to standalone PFR (header-only, no Boost,
  ~0.7 s for the same node) without touching any node.
- **No avendish or halp dependency**: avendish needs Boost.MP11 in its core, and halp plus the bindings
  are GPL-3.0. Only avendish's permissively licensed concept vocabulary (`avnd/concepts/*.hpp`, BSL / CC0 /
  0BSD) is a model for our naming.
- **Exceptions never cross the ABI.** Every control-thread entry catches and reports through `rp_error`
  (a descriptor that cannot be built is an empty string, which the registry refuses); every audio-thread
  hook (`process`, `on_settings`, `ports_changed`, the step and link hooks) must be `noexcept`.
- **Optional hooks are detected by name, then checked by signature.** Once a node defines a member
  function called `validate`, `adjust_layout`, `extra_memory` or any other hook name, its exact signature
  is a compile-time assertion (a data member that shares the name, a sequencer's `step`, is not a hook), so a
  near miss (a missing `noexcept`, a `bool` where `std::optional<std::string>` is expected, two of the
  three step hooks) is an error rather than a hook silently ignored. A port in the wrong struct is an
  error too, and `adjust_layout` may patch and append but never move, drop or re-kind a reflected port
  or param (they bind by position) or relabel a reflected enum, which is checked when the layout is built.
- **Settings decode** uses reflect-cpp with the JSON Schema-validated types, so a bad value in a
  downloaded document is rejected with a message ("expected >= 20, got 5"), not silently clamped.
- **reflect-cpp is upgraded to 0.26.x** (schema `default`, which 0.24 lacks). The pin lives in rpcpp
  inside the dpf.js submodule; bumping both is approved.

### 4.8 TS nodes

```ts
export const lsdjSync = defineNode({
  id: "rp.lsdj.sync",
  ports: { midi: events.in(), serialIn: bytes.in(), serialOut: bytes.out(), buttons: events.out() },
  params: {
    mode: param.enum({ name: "Mode", values: ["off", "midi-sync", "master"], default: "midi-sync" }),
    divisor: param.int({ name: "Divisor", min: 1, max: 16, default: 1 }),
  },
  settings: s.object({ /* ... every field with its default */ }),
  create: (ctx) => ({ process(block) { /* ... */ } }),
});
```

- `ports` (and `params`) may be a function of settings:
  `ports: (s) => ({ in: audio.in(channels.fixed(s.channels, "any")) })`.
- **A TS node can declare only what the C++ parser accepts.** `defineNode` applies the same per-port,
  per-param and layout checks as `Descriptor.cpp` (and the port-spec types reject most of the same shapes
  at compile time), so a descriptor the native registry would refuse is an error where it is written.
  Two shared golden descriptors pin the byte-for-byte agreement.
- **Settings are validated on the control plane.** The audio-thread code receives plain, already-checked
  objects, so **zod leaves the audio-thread bundle**: a node's settings are a zod-free schema
  ([settingsSchema.ts](../packages/retroplug/src/graph/settingsSchema.ts), `s.object`, `s.enum`, `s.int`, ...,
  each parsing and writing its JSON Schema as its zod twin does), `defineNode` imports no zod at all (it takes
  any schema with `safeParse` and `toJSONSchema`, so a zod one still serves on the control plane), and the
  descriptor's schema comes from the definition's own schema object (`toJSONSchema({ io: "input" })`, the
  shape a document may write: defaulted fields optional, as reflect-cpp's `DefaultIfMissing` says too).
  The script-node bundle, which both of an Engine's script runtimes load, was about 660 KB in B3a, 567 KB
  of it zod; since B3b it is 78 KB with none, and a `pnpm test` case (not a build gate) fails a bundle that pulls zod back in
  ([B3b §3](modular-graph-phase-b3b-design.md)). That is also a hard requirement for MicroQuickJS on the
  Pico, which `defineNode` itself does not yet meet (it uses ES2015's library). Every settings field needs a
  default, since the descriptor describes the layout for default settings (a C++ node has `S{}`).
- A TS node sees an audio port as an array of `Float32Array`s, an events port as a frame-ordered array,
  a resource port as a handle with typed accessors (an audio buffer's samples as a `Float32Array`). The arrays
  are the node's own: B3a's script host copies an audio input's samples into JS-owned buffers before `process`
  and an output's out after it, one copy per channel each way, rather than hand JS views of the plan's buffers,
  which a node could keep, transfer or edit past the buffer's life
  ([B3a §2.3](modular-graph-phase-b3a-design.md)); a resource accessor will hold to the same rule. Phase A's
  `Block` and `NodeInstance` types carry audio, messages and control. B3a's script-node runtime added the
  settings hook, `onSettings(settings)`, run on the block thread at the plan swap that kept the instance
  through a settings change, before the first block that runs with them. It carries no rebuild decision:
  under `RP_TYPE_STATELESS_PREPARE` the host decides on the control thread, from the layout the new settings
  give and the definition's `reshapes` (a reshaping type keeps its instance through every change its settings
  accept; any other is swapped while its layout holds and rebuilt when it moves, §4.3). A definition may also
  say `faultOnThrow`, so a throw in `process` faults the node, counted, rather than being run again next
  block (a throw in `onSettings`, `portsChanged` or `activate` faults it at its next block), and
  `startValue(stream)` reads a control's value at frame 0 (its start, or a change at frame 0, since a node
  cannot set an output's start).
  The resource accessor and link peers are still to come.

### 4.9 The script-node runtime

The host wraps each TS node instance in the C table. One script runtime per engine holds every TS node,
and each node's `process` calls into it. On the desktop that is B3a's script host
([ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp), [B3a §2](modular-graph-phase-b3a-design.md)),
with a second, control-side runtime that answers `layout_for` and `prepare_settings` and holds no instance.

| Platform | Script runtime |
|---|---|
| Desktop (plugin, standalone, CLI, SDL, Node) | bare QuickJS, the script host that replaced `DspRuntime` |
| Web | the browser's own JS engine, in the AudioWorklet |
| Pico | MicroQuickJS, under the existing `check:portable` rules |

**Cost.** QuickJS is an interpreter: a stereo gain over a 512-frame block is about 1% of a core, a biquad
about 5%. Fine for event-rate work (sync, MIDI, sequencing) and light audio; heavy DSP belongs in C++.
In the browser the same node runs under a JIT and is far faster.

**Per-node call overhead.** One JS call per TS node per block replaced the kernel's single `processBlock`
call. Measured in B3a ([B3a §7.4](modular-graph-phase-b3a-design.md), a Release build at 44.1 kHz, where a 1-frame
block has 22.7 us), and again in B3b: a script node's call costs about 0.09 us with nothing to pass and 0.18 us with
one message in and out, marshalling included, flat in node count and block size, with nothing allocated per block for
a node written with indexed loops. An audio port adds one copy per channel each way. One node per role was therefore
affordable even at 1-frame blocks, and so it proved: B3b's protocol nodes for an mGB, an LSDj and a NES take about
0.94 us a 1-frame block, 2.2 at 64 frames and 11 at 512 with the LSDj in MIDI sync, allocating nothing (the same
plan with the LSDj decoding MI.OUT takes 0.84, 3.9 and 23 us, with 0.23, 15 and 120 JS allocations a block), which is
about 3 to 3.6 times less than B3a's one kernel node ([B3b §9](modular-graph-phase-b3b-design.md)). Batching
consecutive script nodes into one call is not needed: it would matter only at hundreds of script nodes on 1-frame blocks (1,000 empty nodes take about 90 us a
block), which is the Reaktor-scale case to revisit then. What a node costs beyond that is what its own JS does.

---

## 5. Graph document, planner and executor

### 5.1 The graph document

```json
{
  "nodes": [
    { "id": "gb1", "type": "rp.sameboy.gb", "settings": { "rom": "res:12", "model": "cgb-e" }, "params": { "gain": 0 } },
    { "id": "sync1", "type": "rp.lsdj.sync", "params": { "mode": "midi-sync" } },
    { "id": "sum", "type": "rp.dsp.add", "settings": { "inputs": 2 } }
  ],
  "wires": [
    { "from": "gb1.mix", "to": "sum.in1" },
    { "from": "sync1.serialOut", "to": "gb1.serialIn" },
    { "from": "gb1.serialOut", "to": "sync1.serialIn", "feedback": true }
  ]
}
```

TS sends the **whole document** on every change with one RPC call, `setGraph(doc)`. Whole-document sends
keep planning deterministic, and the planner diffs against the running graph by node id. Today's projects
are tens of nodes; the long-term goal is user-built synths of thousands, which §12 measures (at about 1,000
nodes a one-node edit parses and plans in about 9 to 13 ms) and says where the time goes. A patch form of
`setGraph`, applied to native's stored copy of the last document, would save the transfer and the native
parse (about 10 ms at 10,000 nodes) and, with the TS side keeping each node's checked canonical text, most of
`toGraphJson` (about 105 ms at that size, §12), but not the planning. Live params never send a document.

`setGraph` returns `{ ok, errors[], layouts }`. Each error is `{ nodeId, port?, code, message }`;
`layouts` are each instance's ports and params as its settings resolve them, so TS can show and check
them (channel counts the planner infers from wiring, `any` and `match`, are not reported yet). **A failed plan
changes nothing**: the running plan continues, so a bad edit can never glitch the audio. B2 built a subset:
the RPC is `setGraph({ document, slots, tap? })` and returns `{ ok, errors[] }`, where `slots` names the
systems the document places, in slot order, and `tap` carries the NES tap re-arm
([modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md) §2.5); `layouts` is not sent back.

### 5.2 The planner

C++, on the control thread, five passes:

1. **Validate.** Each node type exists; settings are valid (`prepare_settings` may reject with a
   message, which matters for downloaded packs); ports exist; kinds match; each input has at most one
   wire; channel counts and layouts resolve; exclusive groups are respected; capability flags suit the
   target. (In Phase A the checks that need a layout run after reconcile, because ABI v1 gives a layout
   only per instance: a document refused late builds and tears down the instances it needed, and a
   rebuild briefly runs two instances under one id. Both are rolled back and tested; §8 Phase B owes the
   decision on a type-level layout.)
2. **Reconcile instances.** Same id, type and settings: reuse the instance. Changed settings: the node's
   `prepare_settings` decides, returning either a prepared swap or "needs rebuild" (a new ROM, a
   different SameBoy model). Needs rebuild: build a fresh instance. New id: allocate, create, activate.
   Missing id: schedule release. All of this is control-thread work, as cores are built today; carrying
   state across a rebuild stays TS orchestration (today's reload, and Swap ROM keeping SRAM). §5.3 says
   what a reused instance keeps.
3. **Order.** Topological sort; nodes joined by `link` wires collapse into one **coupled group** (a
   connected set; link wires are undirected).
4. **Cycles.** Only feedback wires may close a loop (§3.3).
5. **Assign buffers.** Each output port gets a buffer: audio (channels x max frames); events, bytes and
   control (fixed-capacity queues: a wired events or bytes output's queue holds what its port declares,
   §4.6, and a control stream a fixed number of changes); resource (a handle
   slot). Buffers are reused by lifetime; feedback wires get persistent double buffers owned by the wire, so they carry over to the next plan
   (§5.3). Every input
   reads straight from its single source's buffer. An output nothing is wired to writes a scratch buffer
   the plan's unconnected outputs share, and for events and bytes that is one queue at the default size,
   whatever the port declares: nothing reads it. A plan's events and bytes queues (each wired output's,
   each feedback front) are held to a budget of 64 MiB of message items and bytes (`kPlanQueueBudget`), so
   the caps on one port do not leave a document unbounded; a document past it is refused (`queue-budget`,
   naming the port whose queue crossed it) before any instance is made, and an allocation the planner
   still cannot make refuses the plan (`out-of-memory`) rather than throwing.

The output is an **immutable plan**: a flat list of steps with port bindings already resolved, the buffer
pool, the pinned resources, and the nodes to release.

### 5.3 The plan swap

The existing rings carry it; their invariants ([spec/01](../spec/01-architecture.md) §7) are unchanged.
(Phase A's `GraphHost` has rings of its own, since nothing else runs on it yet; Phase B either folds them
into the engine's, or fixes the order between the two - see §8 Phase B.)

- **Out:** a `SwapPlan` command on the command ring. It replaces today's `AddSystem` / `ReplaceSystem` /
  `RemoveSystem` / `SetAudioRouting` / `SetConfigField` / `SetSystems` / `LoadKernel` commands.
- **At the start of the next block:** the audio thread swaps the plan pointer, commits prepared settings,
  and calls `ports_changed` where connections changed. **A whole graph edit lands atomically, in one block.**
- **Back:** the old plan returns on the release ring; the control thread frees its buffers, unpins its
  resources, and deactivates and destroys removed nodes.
- **Quiescent:** the inline-flush rule stays (every push flushes when no audio thread owns the engine).
- **Live params** stay lightweight POD commands (`SetParam(node, param, value, frame)`), never a replan.

**What survives a swap.** A plan says which instances run, in what order, with which buffers; the
instances live outside it. A reused instance keeps its memory and therefore all of its state (emulator
RAM, CPU, APU, SRAM, a TS node's JS state); the swap only rebinds its ports. Rewiring, switching routing
modes, adding or removing *other* nodes, linking or unlinking Game Boys, param changes, live-swappable
settings and `ports_changed` tap arming all leave every system running untouched. **The only resets are
changes that reset today** (a new ROM, a different model), and the node, not the planner, decides which
those are. Seven rules keep it that way:

1. **Node ids are stable.** Reuse is keyed on id, so the graph builder derives ids from the TS system id
   (`sys-3`, `sys-3/lsdj-sync`), never from position (§7.2). An id is a plain id with no `.`, because an
   endpoint (`sys-3/lsdj-sync.serialIn`) splits at its first dot.
2. **Settings compare in canonical form**, so key order or other harmless JSON differences are not a change.
3. **`activate` / `deactivate` preserve state.** A sample-rate or block-size change re-activates without
   resetting, as `onSampleRateChanged` does today. Reset is its own explicit action, never a side effect.
4. **Feedback buffers belong to the wire, not the plan.** If a feedback wire exists in both plans, the
   new plan points at the same buffer, so an edit never drops the block of data in flight (LSDj's MI.OUT
   bytes), with two exceptions: a change of channel count starts the front silent, and a change of its
   source's queue capacity while the audio thread runs starts it empty (the control thread may not read a
   front the audio thread is filling). Quiescent, a capacity change carries the block across, counting
   what the new capacity cannot hold as the source's drops.
5. **Script-node state lives with the instance** in the script runtime, so `lsdj.sync`'s clock phase
   survives an edit exactly as a core's state does, and a reshape keeps it through a change of layout.
6. **Streams that hold a value belong to the instance too.** A param's stream and every control output
   stream (and every resource output slot) are owned by the instance, not the plan, so a constant node's
   output keeps its value through a swap instead of reading 0 until its knob next moves. They live in the
   instance's `PortState` for its layout, which each plan holds; a reshape builds a new one for the new
   layout and shares into it, by id, every stream and slot whose param or port persists (a param of the same
   type, range and labels, an output of the same kind and resource type), so those keep their values too, and
   the rest start afresh. Only data with no meaning beyond one block (audio buffers, events and bytes queues)
   is plan-owned.
7. **The control thread never plans while a commit it could race is in flight.** When a published plan
   carries a settings commit to a node whose prepare reads its current settings, `setGraph` first waits for
   the audio thread to adopt it (about one block while audio runs; it gives up with a `busy` error after a
   timeout if the audio thread has stalled), so planning never reads node state that the audio thread is in
   the middle of committing. A commit to a type that declares `RP_TYPE_STATELESS_PREPARE` (§4.3), as every
   script type does, never waits: such plans queue on the command ring and are adopted in push order, so
   while no block runs the ring (255 usable slots, shared with params and the engine's commands) bounds the
   edits, and once it is full `setGraph` answers `busy` ([B3a §10](modular-graph-phase-b3a-design.md)).

Audio *routing* can still click when a mode change moves a stream to different outputs mid-block, as it
does today; a short fade at swaps is a possible later refinement. The cores never notice either way.

### 5.4 Host I/O nodes

The host implements these node types itself:

| Node | Role |
|---|---|
| `rp.host.audio-out` | copies into the host's output buffers; ports follow the host bus layout (§3.6) |
| `rp.host.audio-in` | the host's inputs (a capture card, for a real console) |
| `rp.host.midi-in` | fed from the host's MIDI events |
| `rp.host.midi-out` | drained to the host after the block |

DAW automation keeps today's CC-slot mechanism ([spec/12](../spec/12-dynamic-parameters.md)) in this
project. A later `rp.host.params` node whose outputs are control ports would make automation just more
wiring.

### 5.5 The executor

No heap, no exceptions, the same code on every target. Per block:

1. Drain the command ring.
2. Walk the steps. A plain node is one `process` call with pointers already bound. A **coupled group**
   runs prepare on every member, then round-robin `step` until every member reaches the block target,
   then finish (today's `runUnit` in `BlockRunner`, generalised over `rp.step/1`). `step` returns
   below / done / fault, and may be called again after reporting done. A group still not done after a
   generous bound of passes per frame (4096) has a member making no progress; each member that still is
   not done is faulted, which is the guard against a wedged core spinning the audio thread forever.
3. Reset event, bytes and control queues.
4. Publish snapshots through the `SnapshotRegistry` (via `rp.snapshot/1`).

Queues are fixed-capacity; an overflow drops and bumps a counter (the same never-allocate policy as
today's full ring). An events or bytes queue's count lives on the queue, which only the block's thread
touches; after each block the executor moves it into atomics charged to the queue's producer, which the
host reads on the control thread (`GraphHost::droppedMessages`) and names in its log. Not counted: the
shared scratch queue of unconnected outputs, since nothing reads what it drops, and control streams, whose
own `dropped` only accumulates (a full stream keeps its end value right, §9) and is not reported. The
counts are 32-bit and wrap rather than saturate, which keeps the executor's add one wait-free `fetch_add`;
what reads them compares or subtracts, which a wrap leaves right. TS reads them through the harness facet's
`droppedMessages` (`audio.droppedMessages()`, B3a), which until B3b also added what `rp.host.midi-out` dropped
(a message too short for its system prefix, which went with the prefix). Events stay
frame-stamped; v1 uses the host's block size with no sub-blocking. v1 is single-threaded; the plan records
dependencies, so running independent branches in parallel (enkiTS) can come later without changing the plan
format.

### 5.6 Library targets

All of it lives in `packages/native/src/graph/`:

| Target | Contents | Depends on |
|---|---|---|
| `graph/rp_node.h` | the C ABI | nothing |
| `retroplug-graph-exec` | the executor (`Executor`, `Plan.hpp`, `Queue.hpp`) | nothing (freestanding C++20; builds for the RP2350) |
| `retroplug-graph` | canonical JSON, descriptors, the graph document, registry, resources, planner, `GraphHost`, the host nodes, and the `rp.core` node package | exec, reflect-cpp |
| `graph/node.hpp` | the C++ authoring layer (header-only, on `retroplug-graph`'s include path) | reflect-cpp |

The `rp.core` package (`+`, `merge`, `join`, `split`, `downmix`, `relabel`, `delay`, `constant`,
`buffer-player`, `buffer-recorder`) sits in `retroplug-graph` for now and moves into the `dsp` package
when the package split happens.

None of these link a core, txiki, efsw or LVGL. They are the seed of the system-free "retroplug core".
The TS half (`defineNode`, `defineExtension`, descriptors, `buildGraph`) lives in
`packages/retroplug/src/graph/`.

### 5.7 Offline graphs

A render loop pulls the same executor on any thread. That covers CLI `render`, background render jobs
([spec/11](../spec/11-ui-rendering.md)) and **kit compiles**: a kit compile is a graph `buffer-player` ->
filter -> dither -> `buffer-recorder`, run as fast as possible, with the same DSP nodes a live graph
uses. This is how one sample patcher serves LSDj, risa and BlipToaster: the generic chain plus a
per-cart profile (sample rate, bit depth, slot layout, codec) contributed by each cart's package.
Today's `src/kit/KitCompiler` (codec-neutral) and `src/lsdj/Effects` (gain, biquad, dither) are already
most of this.

### 5.8 The Pico runtime

- The desktop planner exports a plan as a **versioned binary blob** for a named target (`rp2350`). The
  export applies the target's capability rules, so a `heap` node fails here, on the desktop.
- The firmware sizes a static arena from the blob (node memory from `query_memory`, plus buffers) and
  runs the identical executor over node types compiled into the firmware.
- The blob's settings encoding (likely msgpack, already in the tree via dpf.js) is the Pico phase's
  decision; the ABI already carries an encoding tag (`rp_settings.encoding`).
- TS nodes on the Pico run on MicroQuickJS within `check:portable`'s rules.

---

## 6. System nodes

### 6.1 Node types

| Node type | Package | Covers |
|---|---|---|
| `rp.sameboy.gb` | `sameboy` | Game Boy and Game Boy Color (model is a setting) |
| `rp.mesen.nes` | `mesen` | NES |
| `rp.mesen.gba` | `mesen` | Game Boy Advance |
| `rp.mesen.sms` | `mesen` | Master System and Game Gear (Game Gear is a setting) |

The type ids are fixed now; the later package split only changes which ones are compiled into a
product, so `mesen` can be split per console without renaming anything.

### 6.2 Ports

| Node | Inputs | Outputs |
|---|---|---|
| GB | `serialIn` (bytes), `buttons` (events), `link` | `mix` (stereo), `ch1`-`ch4` (stereo), `serialOut` (bytes: MI.OUT capture) |
| NES | `fifo` (bytes: the emulated N8 cart), `buttons` | `mix` (stereo), `pins.pulse` / `.tnd` / `.expansion` (mono), `channels.sq1` / `.sq2` / `.tri` / `.noise` / `.dmc` (mono); exclusive `["pins.*", "channels.*"]` if the tap requires it |
| SMS / GG | `port` (bytes: the controller-port level smsggdj sync drives), `buttons` | `mix` (stereo) |
| GBA | `buttons` | `mix` (stereo) |

MIDI never reaches a core directly; it goes through a node that knows a protocol: `mgb.midi` (MIDI ->
GB serial bytes), `n8.midi-to-fifo` (MIDI -> FIFO writes, replacing native `NesN8FifoRole`). That also
retires a current oddity: SameBoy's `onMidi` stores MIDI that nothing reads.

### 6.3 Settings and params

- **Settings**: the ROM (a resource handle), SRAM and state seeds, model, region, fast boot, Game Gear mode.
- **Params**: gain, highpass, expansion volume, colour/palette knobs, and every other live knob.

Each node handles its own params, so `Engine::applyConfigField` (its `dynamic_cast` switch and the mixed
`ConfigField` enum) goes away, and so does `EngineRpcService::applyRoleConfig`'s `"sameboy"` / `"mesen"`
branch: decoding a node's settings is the node's job.

### 6.4 System capability interfaces

Today's fat `SystemBase` splits into optional interfaces:

| Interface | Purpose |
|---|---|
| `rp.system.memory/1` | named regions (rom / ram / sram / io, size, writable) |
| `rp.system.sram/1`, `rp.system.state/1` | battery and savestate save / load |
| `rp.system.rom/1` | header bytes, title, size: what extension matchers read |
| `rp.system.video/1` | framebuffer triple, size, format |
| `rp.system.buttons/1` | button names to ids, so bindings and extensions say `"start"`, not per-console enums |
| `rp.system.debug/1` | today's `IDebugTarget` |

**The threading rule is unchanged: the control plane never touches a live core.** The executor calls
these interfaces at safe points on the audio thread. Reads come from published snapshots; writes and
state loads queue on the command ring and apply at a block boundary (as `WriteRam` does today).
Extensions only ever see host RPC that routes through those two doors.

### 6.5 Where each `dynamic_cast` goes

| Today | Becomes |
|---|---|
| `Engine::applyConfigField` casts | node params and settings |
| `Engine::syncSplitPlan` casts | `ports_changed` (§3.5) |
| `Engine`'s gather of SameBoy `serialOutLog_` | the GB `serialOut` port |
| `LinkGroup`, `Project::rebuildLinkGroups` | `link` wires, the planner's coupled groups |
| `BlockRunner` / `OfflineRender` including SameBoy headers | the console-neutral executor |
| `SystemKind`, `ConfigField`, native `AudioRouting` | deleted |

What remains of `Engine` is the executor, the rings and the registry.

### 6.6 Real hardware as nodes

`rp.n8.nes` (`n8` package) has the **same `fifo` input as `rp.mesen.nes`**. Its `process` pushes bytes
onto the lock-free ring `N8Link` already uses to feed its serial thread (so it declares `threads`). It
implements `rp.system.sram/1` (dump and restore over USB) and `rp.system.rom/1` (upload and boot). It has
no audio output: the sound comes from the console, or from a capture input through `rp.host.audio-in`.

**Today's "mirror the emulated FIFO to the hardware" (`coreByteSink_`) becomes plain fan-out**: the risa
sync node's `fifo` output feeds both the emulated NES and the N8 node. Emulated, real, or both is just
wiring. The Launchpad works the same way: `rp.launchpad.surface` has events ports to and from the device.

---

## 7. The TS side

### 7.1 The extension contract

```ts
export const lsdj = defineExtension({
  id: "rp.lsdj",
  matches: (node) => node.type === "rp.sameboy.gb" && hasLsdjMarker(node.rom),
  settings: z.object({ syncMode: ..., tempoDivisor: ..., autoStart: ... }),
  contribute: (ctx) => ({
    nodes: [/* lsdj.sync, wired between the MIDI router and ctx.node.serialIn */],
    songs, assets, menu, views,
  }),
});
```

- `matches` reads what `rp.system.rom/1` publishes. That one predicate replaces `romProviders.ts`, the
  marker roles, and `TRACKER_INTEGRATIONS`' marker lookup.
- `ctx` provides the extension's persisted settings, its host node's capabilities (§6.4), and
  `putResource` (how asset overrides become a patched ROM resource).
- **Port ownership:** each node input may be claimed by only one contributor. If two extensions both
  claim a GB's `serialIn`, the graph builder reports the conflict by name instead of silently picking.
  A contributor is an extension on a node, or the base graph the builder made itself (its own wires and
  nodes are claimed first); two contributors inserting nodes with the same id is a conflict too.
  Persisted extension settings that no longer parse are reported and replaced by the defaults, so one
  stale setting never loses the rest of the graph. (Phase A's `ExtensionRegistry.collect`.)
- **What crosses to C++:** `toGraphJson` writes only the fields the C++ parser reads (it refuses a whole
  document over one unknown field, which TS's structural types would let through) and checks ids,
  values and strings as the parser and planner do. A golden document both sides read pins the shape.

### 7.2 The graph builder

`buildGraph(project, registry): GraphDocument` is a **pure TS function**, testable in the plain-TS
`pnpm test` suite with no native code. It builds the graph in layers:

1. **One system node per system**; settings from today's system-role config (`sameboy` / `mesen`).
2. **Extension contributions**: `lsdj.sync`, `mgb.midi`, `risa.sync`, `sms.sync`, `n8.midi-to-fifo`
   (BlipToaster), wired to their system node.
3. **MIDI routing**: `rp.host.midi-in` -> a `midi.route` node (today's four MIDI routing modes as a
   setting) -> each system's protocol node.
4. **Audio routing**: the wiring patterns in §7.6.
5. **Link groups**: `link` wires between GB nodes sharing a `linkGroupId`. How many peers one GB may have
   is the `sameboy` package's rule, carried over from today's `LinkGroup`.
6. **Host and controller nodes**: audio and MIDI in/out, the Launchpad surface.

Node ids derive from the TS system id (`sys-3`, `sys-3/lsdj-sync`, `sys-3/mix`), never from position,
so the same system keeps the same node id through every edit (§5.3).

The stores call `setGraph(buildGraph(...))` whenever the project changes; the planner's diff keeps
unchanged nodes running. The builder applies today's applicability gates (channel / pin splits need a
single system, pins need a NES) so behaviour is unchanged.

### 7.3 Where today's roles go

| Today | Becomes |
|---|---|
| System roles (`sameboy`, `mesen`) | system node settings and params |
| DSP roles (`mgb`, `lsdj-sync`, `risa-sync`, `sms-sync`, `nes-n8-midi`, `midi-routing`, Launchpad controller) | TS node types, each in the module that will become its package (built in B3b: a protocol node per role, and the routing and the controller nodes placed from the project's settings, those two no longer roles) |
| Marker and asset roles (`lsdj-assets`, `risa`, `risa-assets`, `bliptoaster`, `bliptoaster-assets`) | extensions |

**`.rplg` does not change.** `RoleInstance` stays the saved form and the graph builder reads it; no
`K_PROJECT` bump, no migration step.

### 7.4 The DSP kernel dissolves

In two steps. B3a ([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md)) built the first:

- `DspRuntime` evolved into the script-node runtime, the script host (§4.9), and the kernel-loading commands
  went: the script-node bundle is embedded in every Engine and loaded at construction.
- The kernel ran, unchanged, as one TS node, `rp.dsp.kernel`, whose settings were its structure.
- The sink thunks (`pushSerialIn`, `emitCoreMidi`, `pushCoreBytes`, `pressButton`, `emitMidiOut`,
  `emitControllerOut`) became that node's output ports, wired to each system node's `serialIn`, `midi`, `bytes`
  and `buttons` inputs and to `rp.host.midi-out` / `controller-out`, and the per-block input object was built
  from its input ports, fed by `rp.host.midi-in` / `controller-in` and each Game Boy's `serialOut`.

B3b ([modular-graph-phase-b3b-design.md](modular-graph-phase-b3b-design.md)) built the rest:

- The kernel's per-system pipelines became individual TS nodes in the script runtime, one per role
  (`rp.mgb.midi`, `rp.n8.midi-to-fifo`, `rp.risa.sync`, `rp.sms.sync`, `rp.lsdj.sync`), beside the MIDI routing
  (`rp.midi.route`) and the control-surface session (`rp.controller.session`), each wired straight into its
  system node's inputs; `rp.host.midi-out` takes each input's system from its settings, so the private system
  prefix went too.
- `kernelProjection.ts`'s special cases (the synthesized `midi-routing` and Launchpad stages, the forced
  `lsdj-sync` mode for MI.MAP) became graph-builder logic (`graph/controller.ts`, and one table of which role is
  placed as which node, `graph/protocolNodes.ts`).
- The one script-node bundle (about 660 KB, most of it zod) became a **script-node bundle** of 78 KB with no zod in
  it, and the kernel, `DspKernel` and the kernel node are deleted.

### 7.5 The Backend surface

| Stays or arrives | Goes |
|---|---|
| `setGraph(doc)` (built in B2: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md)) | `constructSystem`, `removeSystem` (gone in B2; building a core stays a native call, `buildCore`, until Phase C) |
| `putResource(type, bytes)` -> handle, `releaseResource(handle)` | `setAudioRouting` (gone in B2) |
| capability calls (memory, SRAM, state, ROM info) addressed by node id | `applyRoleConfig` (Phase C) |
| `setParam(node, param, value)` | `setSerialOutCapture` (Phase C) |
| snapshot reads (unchanged) | `dspSetSystems`, kernel loading (gone in B3a: the kernel's structure became its node's settings in the document, and every Engine embeds its bundle; since B3b each role's node's settings are its config) |
| host I/O, transport (unchanged) | |

Duplicate and reload become "a new node id whose settings point at a state-seed resource": the same
TS-owned orchestration as today, over the new calls.

### 7.6 Today's routing modes as graphs

| Mode | Graph |
|---|---|
| Stereo | every system's `mix` -> `+` -> `out.1-2` |
| 2 Ch / Inst | system *i* `mix` -> `out` pair *i* |
| 1 Ch / Inst | each `mix` -> `downmix`; pairs of monos -> `join` -> each pair |
| Channels | GB `ch1`-`ch4` -> pairs 1-4; NES `channels.*` -> `join`s -> pairs |
| Pins | `join(pulse, tnd)` -> `out.1-2`; `join(expansion, none)` -> `out.3-4` |
| Stereo Pins | each pin fanned into both inputs of a `join` -> its own pair |

`MultiOutRouter`, `ChannelSplitRouter`, `PerSystemRouter`, `PerChannelRouter`, `syncSplitPlan` and the
native `AudioRouting` enum disappear. The enum survives only in TS, as a menu choice the builder turns
into wiring.

### 7.7 UI and CLI scope

This project defines the **full** extension and view contract but migrates only the graph-facing parts:
matchers and node contributions. Menus, song and asset catalogs, the HD player and the overlays keep
their current wiring and move onto extensions and views in the package-split project. The UI looks and
behaves exactly as today. The extension contract's `menu` and `actions` members are the one part not
typed in Phase A: they are defined in the package-split project, against the menu item model extracted
from `menuDefs.ts`, rather than invented now and reworked then.

The CLI's `render` builds its graph with the same `buildGraph`, from a one-system project, and runs the
offline executor. The SDK gains `session.graph` (`set`, `describe`, `layouts`) for sessions that want to
wire graphs directly.

---

## 8. Migration

Five phases; each lands working and verified before the next starts.

### Phase A: foundations (no behaviour change)

Built: see docs/modular-graph-phase-a-plan.md.

- `rp_node.h`, `retroplug-graph-exec`, `retroplug-graph`, `graph/node.hpp`; reflect-cpp 0.26 (dpf.js /
  rpcpp bump).
- The planner, executor and plan swap, exercised on toy nodes: `+`, `join`, `split`, `downmix`,
  `relabel`, `delay`, `buffer-player`, `buffer-recorder`, `merge`.
- TS `defineNode` / `defineExtension` / descriptor emission; the C++/TS conformance test.

### Phase B: the engine runs on the graph

Split into four sub-projects, each checked bit for bit against one baseline: B0 records today's engine
([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md); built: `pnpm test:native parity`),
B1 runs native on the executor with TS untouched, B2 hands the graph to TS, B3 dissolves the kernel. The B0
doc's §0 has the table, and its §4 the requirements recording today's engine fixed for B1.

- A `SystemNode<T>` adapter over today's `SystemBase` subclasses, mapping prepare / step / finish and
  port buffers onto the C ABI, so the engine moves before any core is rewritten.
- The host I/O nodes; `Engine::processBlock` runs the executor.
- The script-node runtime; the DSP roles ported to TS nodes.
- `buildGraph`; the stores call `setGraph`; the Backend surface switches (§7.5).
- **Parity target: bit-identical audio** in every audio routing mode, MIDI routing mode and sync mode.
  Expected to hold: same cores, same inputs at the same frame offsets; `+` sums in system order into
  silence, as today's routers do; MI.OUT keeps its one-block latency through the feedback wire. Any case
  that cannot hold is documented explicitly, not waved through.
- Then delete: the routers and native `AudioRouting`; `LinkGroup`; the add / replace / remove / routing /
  config / kernel commands; the DSP kernel.
- Settled in Phase A, owed by the host that adopts it: call `GraphHost::reclaim()` from an idle tick (a
  removed node and its resource pins otherwise linger until the next `setGraph`); expect `setGraph` to
  block the control thread up to its adoption timeout while the audio thread is stalled (since B3a only
  behind a commit to a type without `RP_TYPE_STATELESS_PREPARE`, §4.3); and read the host's inputs
  before `processBlock` silences its outputs, since a DAW often hands both as one buffer. Two more it
  owed are paid: a second `rp.host.audio-out` is refused (`audio-out-twice`, where Phase A let the last
  one win), and `rp.host.audio-out` takes its buffers through `rp_host.get_interface` rather than the
  `ctx` cast Phase A gave it.
- Two decisions Phase A leaves open, both cheap now and costly once Phase B builds on them:
  - **One ring or two.** `GraphHost` has its own command and release rings. The engine's ring also
    carries commands (RAM writes, state loads) that name nodes. Either give `GraphHost`'s `Command` a
    slot for those so one ring carries everything, as §5.3 describes, or have the engine drain its own
    ring BEFORE `GraphHost::processBlock`, never after: `GraphHost`'s drain hands the replaced plan back,
    and a command naming a node of that plan must not run once `reclaim()` may have freed it.
  - **Validate before creating.** Add a type-level `layout_for(settings)` to `rp_node_type` while ABI v1
    is unshipped (both authoring layers can already compute it from settings alone), so the planner can
    validate a whole document before creating anything; or require of every node that create and
    activate have no effect outside the instance and are safe with two instances under one id. Real
    cores (a ROM load, an N8 USB link, Mesen's per-id scratch paths) make the first the safer choice.
- Snapshot publishing (§5.5 step 4, `rp.snapshot/1`) is not built in Phase A.
  `buildGraph` also has to order its layers around `ExtensionRegistry.collect`, which takes the base graph
  as already claimed: the MIDI-routing layer wires into nodes extensions insert, so it needs `collect`'s
  output first (a second, cheap `collect` pass, or a claims check over the merged graph).

B1 is built: [modular-graph-phase-b1-design.md](modular-graph-phase-b1-design.md), executed from
[modular-graph-phase-b1-plan.md](modular-graph-phase-b1-plan.md).

B2 is built: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md), executed from
[modular-graph-phase-b2-plan.md](modular-graph-phase-b2-plan.md). TS owns the graph's structure:
`buildGraph` writes the document, the stores send one per edit, and the translator, `constructSystem` (now
`buildCore`, which builds without placing), `removeSystem` and `setAudioRouting` are gone. Two things
moved out of B2 as designed: the role-config commands
(`applySystemSetting`, `applyRoleConfig`, `ConfigField`) stay until Phase C, where the live knobs become
node params, and the kernel commands stay until B3 deletes the kernel (B3a deleted them). Building a core
stays a native call (`buildCore`) until Phase C, when system nodes build their own from settings and resources.

B3 is split in two. B3a is built ([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md)): the
kernel's input and output became real wires (host I/O nodes, input ports on every system node, the Game Boy's
`serialOut`), and the kernel ran unchanged as one node, `rp.dsp.kernel`, in the new script-node runtime, with its
structure as its settings; the sealed kernel, its fan-out, the kernel RPCs and the native kernel node went. Between
the two, LSDj master sync was fixed as its own change, before B3b ported the decoder
([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)): its stopped-LSDj detector, which B3a's tiny-block
parity groups pinned as a defect, turned out to be reading the core's own clocking of a stopped LSDj. B3b is built
([modular-graph-phase-b3b-design.md](modular-graph-phase-b3b-design.md)): that node is split into one protocol node
per role beside the MIDI routing and the controller session, `kernelProjection` moved into `buildGraph`, zod is out
of the bundle (78 KB, from 660), and the kernel is deleted. Every recorded digest of the kernel node's rig held on
the per-role graph, and strict parity held bit for bit at every commit.

### Phase C: nodes own their knowledge

- Console-specific code moves into each node; every `dynamic_cast` in §6.5 goes.
- `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a preallocated ring (it
  currently allocates on the audio thread).
- `rp.n8.nes` and `rp.launchpad.surface` replace the N8 mirror sink and the controller special cases.

### Phase D: the DSP library and offline graphs

- Library nodes: gain, biquad, dither, resample (porting `lsdj/Effects` and the r8brain path).
- Kit compiles as offline graphs; proof is **byte-identical kit output** against today's `KitCompiler`.
- CLI `render` and background renders on the offline executor.

### Phase E: the Pico runtime

- `exportPlan` for a named target; the static-arena executor built for the RP2350.
- First proven on the desktop with heap allocation forbidden, then on hardware, running a real plan with
  a C node.

---

## 9. Error handling

- **Planner errors are structured** (`nodeId`, `port`, `code`, `message`). A failed plan changes nothing.
- **A faulting node is bypassed, not fatal.** `process` cannot throw but can return a fault (as can a
  coupled member's `step`, or the group pass bound); the executor then bypasses that node (audio silenced,
  queues emptied, control changes dropped) and bumps a fault counter TS can read. The fault flag belongs
  to the instance, so an unrelated graph edit does not revive a wedged node, and `GraphHost::isFaulted`
  names which node it was. Only a fresh instance clears a fault: a rebuild, or removing the node and adding
  it back (with a state seed, if its state should carry over), which is what a "restart node" action does.
- **Strictness:** what crosses a language or a package boundary as structure is strict. The graph
  document and every descriptor (ports, params, channels, the descriptor itself) refuse an unknown
  field, because a misspelt one would change meaning silently (`"acess"` declares a reader). A node's
  settings are the one lenient object: unknown keys are ignored on both sides (reflect-cpp's
  `DefaultIfMissing`, zod's default strip), as the role-config that crosses to native is today, so a
  project saved by a newer build keeps loading its known fields.
- **Queue overflow** drops and bumps a counter; nothing allocates. A full control stream keeps its end
  value right by overwriting its last change, so a burst loses intermediate values, never the final one.
- **Downloaded native packs** are checked at load for ABI version and a valid descriptor. There is no
  sandbox: a crashing native node takes the host down. Trust prompts and signing belong to the downloads
  project.
- **Resource pins**: a debug assertion that every pin has dropped at teardown.

---

## 10. Testing

All headless, all backed by an exit-zero run.

- **A new Catch2 graph-core binary** (built by name like the other test binaries): planner validation,
  ordering, cycles and feedback wires, coupled groups, channel / layout propagation, exclusive groups,
  buffer reuse, plan swaps under ThreadSanitizer (`build-tsan/`), and a block-path run with global
  `operator new` / `delete` replaced to count, which proves the no-heap claim for C++ nodes and the host's
  allocator (a C pack calling `malloc` directly would need a malloc interposer as well).
- **Descriptor conformance**: a C++ node and a TS node with the same declared shape produce
  byte-identical descriptor JSON (settings schemas excepted, §4.6), checked against one shared golden
  file; the canonical writers themselves are held to a shared table of cases that JavaScript generated.
- **`buildGraph` golden tests** in `pnpm test`: one per audio routing mode, MIDI routing mode, and cart,
  plus an id-stability check: cycling the routing mode and adding / removing other systems leaves every
  surviving system's node ids unchanged.
- **State across swaps**: render a system while making unrelated graph edits every few blocks
  (rewiring, adding and removing other nodes, cycling routing modes, linking a second GB); that system's
  own output stream and SRAM must be bit-identical to a render with no edits, and MI.OUT bytes on the
  feedback wire must all arrive.
- **Parity**: `test:native`, the multichannel `retroplug-audio-test`, the `reaper:lsdj-*` and
  `reaper:risa-sync` drift renders, and `test:ui` (unchanged, since the menus do not change).
- **Kit byte-identity** (Phase D) and **Pico arena** runs (Phase E) as above.

---

## 11. The road after this project

Each is its own spec -> plan -> implementation cycle. What this investigation already learned is recorded
here so it is not lost.

### Package split

- Carve `core` (the graph libraries, §5.6), `dsp`, `sameboy`, `mesen` (per console), `lsdj`, `mgb`,
  `risa`, `smsggdj`, `bliptoaster`, `n8`, `launchpad` into packages, each with a native part, a TS part,
  or both.
- **Mesen** compiles all seven consoles into one 426-object archive (NES 6.6 MB, SNES 6.3 MB of objects,
  ...), and `Core/Shared/Emulator.cpp` plus `Core/Debugger/Debugger.cpp` reference every console, so the
  linker cannot drop any. Per-console compile-out is a `MESEN_ENABLE_<X>` patch to ~20 files plus a
  filtered glob (Mesen is vendored, not a submodule). NES-only removes roughly half the object bytes.
- Generated `registerCoreBackends` / `rp_register_packages()` per product; `txiki` and `efsw` out of
  `retroplug-backend` into opt-in libraries; LSDj kit DTOs out of `BackendTypes.hpp`; the risa DMC codec's
  dependency on LSDj files removed; per-product esbuild entries (today every bundle carries every cart).
- Menus (`menuDefs.ts`'s 240 cart-specific lines, `TRACKER_UI`), catalogs, the HD player and the overlays
  move onto extensions and views.
- The Edio protocol exists three times (TS source of truth, C++, Pico C), kept in sync by hand; a package
  boundary is the moment to fix that.

### Web host

- txiki / libuv is the blocker, and it is avoidable: the TS control plane can run in the browser's own
  engine through a third rpcpp codec (an `emscripten::val` sibling of the QuickJS and Node codecs), with
  the synchronous `__rpcSend` unchanged. The control plane already runs on bare QuickJS today (the render
  worker), which proves it needs no libuv.
- The executor maps onto a Wasm AudioWorklet; with pthreads that needs SharedArrayBuffer (COOP / COEP
  headers), or the whole engine can live in the worklet single-threaded (the invoker's inline-flush
  regime already supports that).
- efsw has no web backend; RtMidi has WebMIDI; WebSerial is async while the TS `SerialTransport` read is
  synchronous (needs a worker with `Atomics.wait`, or asyncify); LVGL in a canvas versus a DOM UI for
  embeds is that project's decision. DPF is not needed on the web.
- An embed (a risa developer's site) is a product built from `core` + `dsp` + `mesen` (NES only) + `risa`.

### Downloadable packs

- Native code is allowed (decided): per-platform builds of the C ABI, plus TS and assets. That project
  owns signing / notarisation inside a signed plugin, trust prompts, dependency and version resolution,
  and the install location. On the web, packs are compiled in at build time.

### Modular UI

- A structure view over the same graph document; the graph becomes the saved source of truth (with a
  `K_PROJECT` bump and a migration from the role model), and the menus become a panel view of it.

---

## 12. Open questions and risks

- **gcc compile time** on very large C++ nodes (§4.7); mitigated by the `fields_of` seam.
- **Mesen tap exclusivity**: whether pins and channels can be armed at the same time decides whether the
  NES declares the exclusive group. To be checked against the patched `NesSoundMixer` in Phase C.
- **Link groups with more than two Game Boys**: today's `LinkGroup` semantics carry over as the `sameboy`
  package's rule; the exact peer topology is confirmed in Phase B against the existing link tests.
- **Bit-identical parity** relies on summing order and frame offsets matching the current engine; any
  deviation is documented in Phase B.
- **Script-node call overhead** versus the kernel's single call: measured in B3a and B3b (§4.9), about 0.09 to
  0.18 us a node per block, so batching is not needed below hundreds of script nodes on 1-frame blocks.
- **Surround to the DAW** depends on DPF port groups (§3.6).
- **Large graphs.** The goal past chiptune is user-built synths and music apps, so graphs of thousands of
  nodes. Measured in B2 with `build/bin/retroplug-graph-test "[bench]"`, a Release build on a Ryzen 9 9950X:
  the planner's `GraphHost::setGraph` on a bare graph rig in a quiescent host, the document's parse timed
  apart from the plan (plan, publish, adopt, and free the plan it replaced). A chain feeds each delay into
  the next; a wide fan sums every delay through a tree of 16-input adds. A cold plan builds every instance
  anew (the median of 3 fresh rigs); an edit gives one delay another ring, so it alone rebuilds (the median
  of 5 edits, each checked to have rebuilt that delay, kept the first delay's instance and left the instance
  count unchanged). Node counts are the ones the bench prints:

  | Shape | Nodes | Wires | Cold: parse | Cold: plan | Edit: parse | Edit: plan |
  |---|---|---|---|---|---|---|
  | chain | 1,001 | 1,000 | 1.0 ms | 9.1 ms | 1.0 ms | 7.8 ms |
  | chain | 10,001 | 10,000 | 9.7 ms | 150 ms | 10.0 ms | 141 ms |
  | wide fan | 1,069 | 1,068 | 1.0 ms | 12.6 ms | 1.0 ms | 11.7 ms |
  | wide fan | 10,670 | 10,669 | 9.9 ms | 308 ms | 11.1 ms | 303 ms |

  The plan grows faster than the graph (ten times the nodes, 16 to 26 times the time). A one-node edit costs
  about what a cold plan does: with the reuse the bench asserts, that puts the time in planning, not in
  creating instances. It does not by itself rule the document out, since both parse it, which is why the
  parse is timed apart: about 10 ms at 10,000 nodes, 4 to 7 percent of an edit. Timers around each planner
  pass, in a local build of the bench not committed, put the 10,000-node edit's costs (chain about 156 ms,
  fan about 319 ms in that run, parse included) in the order they bite:

  1. **The planner's `order()`.** Kahn's algorithm picks the next ready group by scanning every group at
     every step, quadratic in the node count: 36 ms (23%) of the chain's edit, 154 ms (48%) of the fan's. A
     min-heap keyed by document order fixes it.
  2. **`structure()` and `reconcile()`.** Per-node JSON round trips: the settings canonicalized, the type's
     `layout_for` answer parsed (`parseLayout`) and written back out (`layoutJson`), and the live instance's
     layout written again for the reuse compare. 68 ms (44%) of the chain's edit, 91 ms (29%) of the fan's.
     Caching the layout by type and settings text would skip most of them.
  3. **`assignOutputs`.** Each output port scans every wire for its last reader, all of the chain's 21 ms
     (14%); in the fan each group also scans every buffer's last use, a second quadratic pass, and the fresh
     buffers are allocated here too: 37 ms (12%) of the fan's edit, the buffers' share of it in item 4.
     Indexing the wires, and the buffers by the group that frees them, once per plan removes both scans. (The
     rest of the plan's build, its inputs and steps, adds another 13 to 16 ms.)
  4. **Fresh audio buffers.** Every plan allocates its buffers anew and zero-fills them, reused by lifetime
     within a plan and never across plans: about 2 ms (under 1%) of the fan's edit, about 7 ms when its pages
     are new, and next to nothing in the chain, which needs only a couple. That is at the bench's 1,024-frame
     blocks (4 KB buffers). The engine plans at 8,192 frames (32 KB): with the same timers allocating buffers
     that size, the fan's edit spends about 10 ms on them, level with the parse, and about 50 ms when the
     pages are new (the first plan that needs them, or after the allocator handed them back). Then they are
     the largest part of `assignOutputs` (item 3), behind only `order()` and `structure()` with
     `reconcile()`. Pooling them across plans removes it.
  5. **The document parse.** About 10 to 11 ms at 10,000 nodes: 7% of the chain's edit, 4% of the fan's. A
     patch form of `setGraph` would save it and the transfer and, with the TS side keeping each node's
     checked canonical text, most of `toGraphJson` (below), but none of the planning.

  The table is the planner alone: not `Engine::setGraph`'s own work (the slot and system-node checks, the
  system table, freeing parked cores), and not TS. On the TS side `EngineGraph` sends what `buildGraph` and
  `toGraphJson` write, and for a 10,030-node document (4,700 unlinked Game Boys under stereo, 14,728 wires)
  those cost about 7 ms and 105 ms under QuickJS (medians of 7, a one-off script run by `retroplug-cli`, not
  committed): `toGraphJson`'s checks about 40 ms and its canonical JSON about 59 ms, where a plain
  `JSON.stringify` of the same document takes about 9 ms. So at that size the TS side costs about what the
  chain's native plan does, plus the RPC hop, and `toGraphJson` is a cost of its own. `EngineGraph` pays it
  on every publish, even one its dedupe then skips: it builds and serialises the whole document before
  comparing it with the last one that planned (`engineGraph.ts`). The bench's nodes
  have two-key settings, no params, no control, event or feedback wires and no coupled groups; real nodes
  with bigger layouts pay more per node in the JSON round trips. None of this is needed at today's sizes;
  it is recorded here so the graph-editing project starts from it.
