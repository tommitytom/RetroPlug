# Phase B3b: one protocol node per role, and the kernel deleted

**Date:** 2026-10-27 · **Branch:** `feature/modularize` · **Status:** designed; not yet built.

Phase B of [modular-graph-design.md](modular-graph-design.md) moves the engine onto the graph in steps, each held bit
for bit to the B0 parity baseline ([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)). B3a
([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md)) made the DSP kernel's input and output real
wires and ran the unchanged `DspKernel` as one TS script node, `rp.dsp.kernel`. B3b, the rest of "the kernel
dissolves", splits that node into one script node per role, folds `kernelProjection` into `buildGraph`, takes zod
out of the script-node bundle and deletes the kernel. The LSDj master-sync fix
([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)) landed between the two.

## 0. Where B3b sits

The kernel node runs seven behaviours for the systems it is given, each a function over a per-system context
(`SystemCtx`) or the project's (`ProjectCtx`), in a fixed order every block: the project stages first (the MIDI
routing, which fans the host's MIDI into a per-system inbox, then the Launchpad controller, which appends its
launches to its target's inbox), then each system's pipeline in slot order. Its structure (every system's roles,
the project stages) is its settings, which the stores publish through `kernelProjection.ts`, and its outputs are
one port per system input plus one host MIDI out port whose messages carry a private 4-byte system-id prefix.

What the code says about splitting it:

- **No two roles on one system write the same system input.** mGB writes `serialIn`; LSDj writes `serialIn`,
  `buttons` and host MIDI out; a NES writes `midi` (`nes-n8-midi`) and, for a risa ROM, `bytes` (`risa-sync`); an
  SMS or GG writes `bytes`. So splitting needs no merge between protocols.
- **Tick state is per system, and no system has two ticking roles**, so a tick counter per node is the same.
- **One stream is merged today**: the controller's launches join its target system's routed MIDI, appended after
  the routed events at frame 0.
- **Host MIDI out is one port in emission order**: the controller's first, then the systems' in slot order, every
  message at frame 0.
- **The role registry is the app's model of roles**: each role's zod schema, `onConstruct` and menu stay on the
  control plane. Only each role's `dsp` behaviour is the kernel's.

## 1. Decisions taken

| Question | Decision |
|---|---|
| How is B3b cut? | **One design and one plan**, prototype first, then subagent-driven execution, as B3a. Every task stays bit-identical. |
| How does zod leave the audio side? | **A small zod-free settings schema** that `defineNode` takes (section 3). One bundle still serves both script runtimes; zod leaves the bundle entirely. |
| How is a protocol node written? | **Against the `Block` API**, as `rp.bench.pass` is: indexed loops over the input `Message` views, `send` on its own ports, state and tick counter on its instance, nothing allocated per block. The protocols' pure functions (the Arduinoboy and master-sync decoders, `routeBlockInto`, `risaLocate`, the SMS levels, `walkTicks`) and their unit tests stay as they are; only each role's glue around them is rewritten. |
| The capture mode as a Game Boy node setting? | **Not in B3b**: Phase C, with the other live knobs (`ConfigField` stays until then, as B2 decided). |
| Master-sync clocks at their byte's frame? | **Not in B3b**: it changes MIDI out, so it is its own change after B3b, with its own re-record. |
| One QuickJS context per script node? | **Not in B3b**: decided before user-written nodes (spec/07). |

## 2. The node set

Script nodes, one per role. Node ids follow the main design's convention: `sys-<id>/<role-kind>` for a system's
protocol node, `routing` and `controller` for the project's.

| Type | Replaces | Inputs | Outputs | Settings |
|---|---|---|---|---|
| `rp.midi.route` | `midi-routing` | `midiIn` | one MIDI output per system, in slot order | `mode`; the system ids in slot order |
| `rp.mgb.midi` | `mgb` | `midi` | `serialIn` | none |
| `rp.n8.midi-to-fifo` | `nes-n8-midi` | `midi` | `midi` | none |
| `rp.risa.sync` | `risa-sync` | none (the transport) | `bytes` | none |
| `rp.sms.sync` | `sms-sync` | none (the transport) | `bytes` | `machine` |
| `rp.lsdj.sync` | `lsdj-sync` | `midi`, `launches`, `serialOut` (a feedback wire, a Game Boy only) | `serialIn`, `buttons`, `midiOut` | `mode`, `tempoDivisor`, `autoStart` |
| `rp.controller.session` | `launchpad` | `controllerIn`, `controllerConnected` | `controllerOut`, `launches`, `midiOut` | its app, its target, the resolved system id, the parsed app config, the timing table, the anchor |

- Each protocol node's outputs are wired straight into its system node's inputs of the same name.
- `rp.lsdj.sync` reads its routed MIDI and then its launches, in that order, as the kernel's inbox held them. A
  frame merge would not be the same: the launches arrive at frame 0 after routed events at later frames.
- Every type is `reshapes: true`, so a config edit keeps the instance and its state, as the kernel's per-stage
  state survived a config change. A system removed and added again gets fresh instances, which the graph gives for
  free (the kernel pruned a dead id's state).
- `rp.lsdj.sync` keeps master sync's state in the sub-object the dispatcher drops outside the mode, as now.

## 3. Settings without zod, and the bundle

`graph/settingsSchema.ts`, zod-free, is what `defineNode` takes:

- `s.object`, `s.enum(values, default)`, `s.int({ min, max, default })`, `s.bool(default)`, `s.number(default)`,
  `s.string(default)`, `s.array(item, default)`, and `s.json(default)` for an opaque value the control plane has
  already checked (the controller's app config and its timing table).
- `parse(raw)` fills defaults and refuses bad input with today's one-line `field: why`; `toJSONSchema()` emits what
  zod's `toJSONSchema({ io: "input" })` emitted for the same shapes, which the shared golden descriptors pin byte
  for byte. Settings types are inferred (`Infer<typeof schema>`), as `z.infer` was.
- Every TS node type moves to it, the script test bundle's toy nodes included. Zod stays where the app already uses
  it: the role configs, the project files, the controller apps' configs.

The controller apps' registry pairs each app's runtime with a zod schema, and the session parses its app config
through it on the block thread today. `buildGraph` parses it on the control plane instead, where zod is loaded
anyway, and the controller node imports only the apps' runtime, never the registry's schemas.

One bundle still serves both of an Engine's script runtimes, so the host's check that a node has the same
descriptor in both contexts is unchanged. It holds the protocol and controller nodes and nothing that registers
roles. A TS test bundles `script/bundle.ts` and fails if any zod module is in it, so the property holds on every
build. B3b reports the new size (660 KB in B3a, about 567 KB of it zod).

## 4. `buildGraph` and the stores

`buildGraph` places, in this order, every script node before the systems:

- the routing node, always;
- the controller node when a controller is enabled, its default target ("0 is the first system") resolved to an id
  at the publish;
- each system's protocol nodes, in slot order;
- then the host I/O and the systems, as now.

`kernelProjection`'s special cases move in with it: the synthesized routing and controller nodes, and the forced
`rp.lsdj.sync` mode (`midiMap` or `off`) for the MI.MAP controller's target, written only into that node's settings,
so nothing of it reaches the `.rplg`, as today. The store's "roles the kernel runs" becomes "roles that have a
protocol node".

A profile build's `apu-render` span begins where the last script node's `process` ended, which is right only while
every script node runs before the systems. The planner's order among ready nodes is checked as part of the flip; if
it does not keep document order, the span is fixed in B3b.

## 5. Host I/O, capacities, and what must match

**Host MIDI out by wiring.** `rp.host.midi-out` becomes a native node set by `{ systems: [id, ...] }`, one input per
entry (as `rp.core.merge` takes `{"inputs": N}`). Each block it appends each input's messages, input by input in
order, under that input's system id. `buildGraph` orders the inputs as the kernel emitted: the controller's
`midiOut` first (under its resolved target), then each `rp.lsdj.sync` in slot order. The 4-byte system prefix goes
with the kernel; every host MIDI out message stays at frame 0, as now. `rp.host.controller-out` is unchanged (one
producer).

**Capacities**, port for port as the kernel's: `serialIn` and `midi` 4096 messages / 72 KiB, `bytes` 256 / 1 KiB,
`buttons` 64 / 128 bytes, `midiOut` 1024 / 8 KiB, `controllerOut` 256 / 4 KiB. A routing output takes
`rp.host.midi-in`'s capacity (2048 messages / 64 KiB), since under send-to-all it carries everything the input passed;
`launches` is small. Drops stay counted per port.

**What must match the kernel exactly**, each checked by the equivalence test (section 6):

1. Within a node, each port's emission order is what the kernel emitted for that system and sink, so the
   out-of-order senders (Arduinoboy's START after a row byte, MI.MAP's clocks after its rows) get the same clamp-up
   from the frame-ordered queue.
2. Host MIDI out's order across nodes.
3. Launches after routed events.
4. Tick counters and state as the per-system ones were, across a config edit, a removal, and a re-add.
5. The MIDI-channel-to-instance rewritten copy.
6. The controller's default target.

How messages are grouped is free where no core can see it: `rp.mgb.midi` sends each MIDI message's bytes as one
`serialIn` message at its frame where the kernel coalesced all same-frame bytes; a system node applies a message's
bytes in order, and the digests hash `serialIn` byte by byte. Every other port is hashed message by message, so those
nodes send the kernel's messages as they were.

**A throw** keeps the node's output up to the throw, counted, and its next block runs again: the kernel's rule, per
node. One difference: a throw in one system's node no longer drops later systems' output for that block. No parity
case throws.

## 6. Testing

- **Equivalence.** The C++ kernel-node rig's cases (every LSDj mode, risa and smsggdj sync, NES MIDI, a controller
  session whose launches interleave with MI.OUT, structure changes with state surviving) are rebuilt on the per-role
  graph, their document generated as `buildGraph` places it, and must hold their recorded digests unchanged (recorded
  while the old kernel stage ran beside the kernel node). The rig's thread-sanitizer case (the control side
  restructuring while the audio thread runs) moves over too.
- **Node-level tests.** The role tests that drive `DspKernel` (roles, LSDj modes, routing, risa, sms, the controller,
  ticks, the kernel-level Arduinoboy and master-sync cases) port to node-level tests over a `Block` stand-in, as
  `kernel-node.test.ts` already drives one; the pure-function tests stay; `project-ctx`'s kernel-specific cases go
  with the kernel.
- **`buildGraph`.** The explicit wiring tables are updated and `kernelProjection`'s tests move here (the controller's
  sync override, the synthesized nodes); the goldens are re-recorded by a one-off script, and the native golden
  runner still plans and runs every one with no fault, throw or drop.
- **The bundle.** The no-zod test; the bench re-pointed at the per-role node set at 1, 64 and 512 frames; the profile
  benches (`dsp-bench`, `nes-bench`) and spec/08's figures.
- **Parity**, strict at every task, nothing re-recorded.
- **Acceptance**, B3a's full matrix: strict parity with the fixtures and `RP_FAIL_ON_SKIP=1`, and the CI view (an
  unfiltered `test:native` with the fixture directories absent, since a filtered run does not judge the skip
  baseline); `test:native`, `pnpm test`, plugin, ui and node suites; the thread and address sanitizers (with the
  fixtures, so `dsp-threaded` runs); the Reaper suite; `sdl:pipewire`; BlipToaster, NESVJ and `cli-ts`.

## 7. How it lands

Every commit bit-identical:

1. The zod-free settings schema; `defineNode` and the toy nodes on it.
2. The seven node types and their node-level tests, in the bundle beside the kernel node, not yet placed.
3. The equivalence rig over the per-role graph, holding the recorded digests.
4. `rp.host.midi-out`'s `systems` form, beside its prefixed form.
5. The flip: `buildGraph` places the per-role graph, `kernelProjection` folded in; the stores publish it; the goldens
   re-recorded.
6. The deletion: the kernel node, `DspKernel`, `kernelProjection`, the roles' `dsp` behaviours and the role type's
   `dsp` field, the prefixed host MIDI out, the kernel-based tests; the bundle's last zod goes with them, and the
   no-zod test passes.
7. The bench and the profiles.
8. Docs: this design's status, the main design, spec/01 to 04, 06, 07 and 08, AGENTS.md.
9. Acceptance (section 6).

## 8. Left for later

- The capture mode as a Game Boy node setting, which makes a mode switch atomic (Phase C).
- Master-sync clocks at their byte's frame (its own change after B3b).
- One QuickJS context per script node (before user-written nodes).
