# Phase B3b: one protocol node per role, and the kernel deleted

**Date:** 2026-10-27 · **Branch:** `feature/modularize` · **Status:** built, in the commits section 7 lists by subject
(a hash would go stale when the plan is re-executed); section 9 records what building it found.

Phase B of [modular-graph-design.md](modular-graph-design.md) moves the engine onto the graph in steps, each held bit
for bit to the B0 parity baseline ([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)). B3a
([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md)) made the DSP kernel's input and output real
wires and ran the unchanged `DspKernel` as one TS script node, `rp.dsp.kernel`. B3b, the rest of "the kernel
dissolves", splits that node into one script node per role, folds `kernelProjection` into `buildGraph`, takes zod
out of the script-node bundle and deletes the kernel. The LSDj master-sync fix
([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)) landed between the two.

## 0. Where B3b sits

Before B3b the kernel node ran seven behaviours for the systems it was given, each a function over a per-system
context (`SystemCtx`) or the project's (`ProjectCtx`), in a fixed order every block: the project stages first (the
MIDI routing, which fanned the host's MIDI into a per-system inbox, then the Launchpad controller, which appended its
launches to its target's inbox), then each system's pipeline in slot order. Its structure (every system's roles, the
project stages) was its settings, which the stores published through `kernelProjection.ts`, and its outputs were one
port per system input plus one host MIDI out port whose messages carried a private 4-byte system-id prefix.

What the code said about splitting it:

- **No two roles on one system were meant to write the same system input.** mGB writes `serialIn`; LSDj writes
  `serialIn`, `buttons` and host MIDI out; a NES writes `midi` (`nes-n8-midi`) and, for a risa ROM, `bytes`
  (`risa-sync`); an SMS or GG writes `bytes`. So splitting needs no merge between protocols for a system the providers
  build. It is not true of every loaded project: a project stored with `mgb` whose ROM path now holds LSDj gains
  `lsdj-sync`, and a hand-edited file can name a kind twice. The kernel merged what both sent; the graph places one
  (section 9).
- **Tick state is per system, and no system has two ticking roles**, so a tick counter per node is the same.
- **One stream was merged**: the controller's launches joined its target system's routed MIDI, appended after the
  routed events at frame 0.
- **Host MIDI out was one port in emission order**: the controller's first, then the systems' in slot order, every
  message at frame 0.
- **The role registry is the app's model of roles**: each role's zod schema, `onConstruct` and menu stay on the
  control plane. Only each role's `dsp` behaviour was the kernel's.

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

Script nodes, one per role ([src/script/nodes/](../packages/retroplug/src/script/nodes)). Node ids follow the main
design's convention: `sys-<id>/<role-kind>` for a system's protocol node, `routing` and `controller` for the
project's.

| Type | Replaces | Inputs | Outputs | Settings |
|---|---|---|---|---|
| `rp.midi.route` | `midi-routing` | `midiIn` | `sys-<id>`, one MIDI output per system, in slot order | `mode`; the system ids in slot order |
| `rp.mgb.midi` | `mgb` | `midi` | `serialIn` | none |
| `rp.n8.midi-to-fifo` | `nes-n8-midi` | `midiIn` (section 9) | `midi` | none |
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
- `rp.lsdj.sync` keeps master sync's state in the sub-object the dispatcher drops outside the mode, as the role did.

## 3. Settings without zod, and the bundle

[`graph/settingsSchema.ts`](../packages/retroplug/src/graph/settingsSchema.ts), zod-free, is what `defineNode` takes:

- `s.object`, `s.enum(values, default)`, `s.int({ min, max, default })`, `s.bool(default)`, `s.number(default)`,
  `s.string(default)`, `s.array(item, default)` (the item carries a default of its own, section 9), and
  `s.json<T>(default)` for an opaque value the control plane has already checked (the controller's app config and
  its timing table).
- `safeParse(raw)` fills defaults and refuses bad input with today's one-line `field: why`; `toJSONSchema()` emits
  what zod's `toJSONSchema({ io: "input" })` emitted for the same shapes, which the shared golden descriptors pin
  byte for byte. Settings types are inferred (`Infer<typeof schema>`), as `z.infer` was.
- Every TS node type moves to it, the script test bundle's toy nodes included. Zod stays where the app already uses
  it: the role configs, the project files, the controller apps' configs.

The controller apps' registry paired each app's runtime with a zod schema, and the session parsed its app config
through it on the block thread. The publisher parses it on the control plane instead, where zod is loaded anyway
(`parseAppConfig` in [controller/registry.ts](../packages/retroplug/src/controller/registry.ts), section 9), and the
controller node imports only the apps' runtime, never the registry's schemas.

One bundle still serves both of an Engine's script runtimes, so the host's check that a node has the same
descriptor in both contexts is unchanged. It holds the protocol and controller nodes and nothing that registers
roles. A TS test bundles `script/bundle.ts` and fails if any zod module is in it, so the property holds on every
build. The bundle went from 660567 bytes in B3a, about 567 KB of it zod, to 78152 (section 9).

## 4. `buildGraph` and the stores

`buildGraph` places, in this order, every script node before the systems:

- the routing node, whenever the model has a MIDI routing mode (a project always has one; a `DirectGraph` or a lone
  `SystemsStore` routes none until a mode is set);
- the controller node when a controller is enabled, its default target ("0 is the first system") resolved to an id
  at the publish;
- each system's protocol nodes, in slot order;
- then the host I/O and the systems, as before.

`kernelProjection`'s special cases move in with it: the synthesized routing and controller nodes, and the forced
`rp.lsdj.sync` mode (`midiMap` or `off`) for the MI.MAP controller's target, written only into that node's settings,
so nothing of it reaches the `.rplg`, as before. The store's "roles the kernel runs" becomes "roles that have a
protocol node", one table ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts), section 9).

A profile build's `apu-render` span begins where the last script node's `process` ended, which is right only while
every script node runs before the systems. The planner keeps document order among nodes ready at once
([Planner.cpp](../packages/native/src/graph/Planner.cpp) `order()`), so the span needed no change.

## 5. Host I/O, capacities, and what must match

**Host MIDI out by wiring.** `rp.host.midi-out` becomes a native node set by `{ systems: [id, ...] }`, one input per
entry, `in1` to `inN` (as `rp.core.merge` takes `{"inputs": N}`); an empty list is a node with no inputs. Each block
it appends each input's messages, input by input in order, under that input's system id. `buildGraph` orders the
inputs as the kernel emitted: the controller's `midiOut` first (under its resolved target), then each
`rp.lsdj.sync` in slot order. The 4-byte system prefix goes with the kernel; every host MIDI out message stays at
frame 0, as before. `rp.host.controller-out` is unchanged (one producer).

**Capacities**, port for port as the kernel's ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts)):
`serialIn` and `midi` 4096 messages / 72 KiB, `bytes` 256 / 1 KiB, `buttons` 64 / 128 bytes, `midiOut` 1024 / 8 KiB,
`controllerOut` 256 / 4 KiB. A routing output takes `rp.host.midi-in`'s capacity (2048 messages / 64 KiB), since
under send-to-all it carries everything the input passed; `launches` is small (64 messages / 256 bytes). Drops stay
counted per port.

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
  graph, their documents the ones `buildGraph` places, and must hold their recorded digests unchanged (recorded while
  the old kernel stage ran beside the kernel node). The rig does not write those documents: each structure is a case of
  `golden/build-graph-roles.json` (`rig/<name>`), `buildGraph`'s own record, and the rig plans it by name with its
  system nodes replaced by probes. The rig's thread-sanitizer case (the control side
  restructuring while the audio thread runs) moves over too. Built:
  [ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp).
- **Node-level tests.** The role tests that drive `DspKernel` (roles, LSDj modes, routing, risa, sms, the controller,
  ticks, the kernel-level Arduinoboy and master-sync cases) port to node-level tests over a `Block` stand-in, as
  `kernel-node.test.ts` already drove one; the pure-function tests stay; `project-ctx`'s kernel-specific cases go
  with the kernel. Built: `test/script/nodes/`, one file per type.
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

Every commit bit-identical. As built, each step's commit named by its subject:

1. The zod-free settings schema; `defineNode` and the toy nodes on it ("feat(graph): a zod-free settings schema that
   defineNode takes beside zod's, and the toy nodes on it").
2. The seven node types and their node-level tests, in the bundle beside the kernel node, not yet placed ("feat(dsp):
   one script node per role - routing, mGB, N8, risa, smsggdj, LSDj and the controller session - beside the kernel,
   not yet placed").
3. `rp.host.midi-out`'s `systems` form, beside its prefixed form ("feat(engine): rp.host.midi-out takes each input's
   system from its settings, beside its prefixed form"). It moved ahead of the equivalence rig, which needs it
   (section 9).
4. The equivalence rig over the per-role graph, holding the recorded digests ("test(engine): the kernel node's
   recorded digests, held by the per-role graph as by the kernel").
5. The flip: `buildGraph` places the per-role graph, `kernelProjection` folded in; the stores publish it; the goldens
   re-recorded ("feat(graph): the kernel dissolved into the graph - buildGraph places one protocol node per role, the
   routing and the controller, kernelProjection folded in").
6. The deletion: the kernel node, `DspKernel`, the roles' `dsp` behaviours and the role type's `dsp` field, the
   prefixed host MIDI out, the kernel-based tests; the bundle's last zod goes with them, and the no-zod test passes
   ("refactor(dsp): delete the DSP kernel - DspKernel, the kernel node and the roles' behaviours; the script-node
   bundle holds no zod"). `kernelProjection` had already gone in the flip.
7. The bench and the profiles ("test(engine): the bench and the profiles on the per-role nodes - each script node's
   span names its type").
8. Docs: this design's status and section 9, the main design and B3a's, every spec doc that described the kernel
   (spec/04 renamed to [04-roles-protocol-nodes.md](../spec/04-roles-protocol-nodes.md)), AGENTS.md, and the code
   comments that still spoke of the kernel as present ("docs: B3b built - the protocol nodes, the zod-free bundle and
   the kernel's deletion, and what building it found").
9. Acceptance (section 6).

The follow-up round after the docs commit, each by its subject: "fix(systems): a loaded project's role configs reach
their protocol nodes parsed by the role's schema"; "fix(graph): one writer per system input - of two roles whose nodes
write one input, the first is placed"; "fix(systems): serial-out capture follows the lsdj-sync the pipeline places, so a
left-out one arms none"; "test(graph): buildGraph's role goldens - every protocol node type, a controller each way and
every routing, planned and driven on the engine in CI"; "test(graph): PROTOCOL_NODES' port names held to the ports each
node module declares, and every role's node type to the bundle's"; "test(engine): the equivalence rig plans buildGraph's
own documents, read from its role goldens, its systems stood in for"; "test(engine): the rig claims only the cases it
runs, states the controller's two orders outright, and keeps a session through a system added"; "fix(graph):
protocolPipeline leaves out a kind already placed, and DirectGraph.setPipeline reports what it leaves out"; "fix(systems):
of two colliding roles, a loaded system places the one its ROM calls for, and logs once it has built"; "fix(systems): a
refused role config falls back to what the ROM calls for, and the keys a parse drops are logged"; "fix(plugin): the
reported LSDj sync latency follows the lsdj-sync the pipeline places"; "test(systems): every adversarial stored config
reaches its node as settings it accepts, and the refusals check fails closed"; "test(engine): the role goldens' driven
blocks have a floor under what reaches the host"; "test(engine): the rig's feeds lead its document, so its script nodes
run in buildGraph's plan order, which a case now holds"; "fix(host-io): rp.host.midi-out keeps the host's MIDI out
frame-sorted across its inputs"; "test(profile): the benches prove their allocation window live, and the profile cases
pin more"; "fix(settings): the zod-free schema refuses a duplicate enum value, bad int bounds, and normalizes an array's
default"; "refactor(nodes): the controller session builds no input object per block, locals stop shadowing s, and the
notes say why"; "test(nodes): inject the lsdj throw through the block, port the kernel's arm and 48 kHz stop cases, pin
the collision rule's round trip"; "test(bundle): the no-zod test uses the runner's node and bounds the bundle's size";
"test(nodes): the poisoned message carries the flags a Message does, so the suite typechecks"; "test(engine-graph): the
controller floor needs Programmer mode, and the rig's order and drops are held"; "docs: the B3b follow-up round - the design's section 9 and the specs say what the stores, the goldens and the host MIDI out now do";
"docs(comments): comments that described the deleted kernel, roles and tests say what exists"; "docs: the follow-up C review - the
bench window's real mechanism, the midi-out merge the clamp will need, and wrapped lines"; "fix(settings): an array default its
item schema refuses throws when built"; "test: the review's strengthenings of the node, store and goldens tests, and their
comments"; and the docs pass that lists itself, "docs: the last review's false statements, stale comments, wrapped lines and
the limitation it found".

## 8. Left for later

- The capture mode as a Game Boy node setting, which makes a mode switch atomic (Phase C).
- Master-sync clocks at their byte's frame (its own change after B3b). It would be the first producer of host MIDI
  past frame 0. `rp.host.midi-out` already keeps the host's list frame-sorted across its inputs (each message goes out
  at the later of its frame and the last frame handed out in the block, as the kernel's one queue clamped), so a host-side
  sort is no longer the alternative. But the clamp raises a later input's earlier frames to an earlier input's (a
  second LSDj in master sync would have its clocks bunched behind the first's), so that change has to turn the clamp
  into a merge by frame, or its re-record would bake the bunching into the baseline.
- One QuickJS context per script node (before user-written nodes).
- The MI.OUT and master-sync decoders' emitted arrays: about 3 JS allocations for every host MIDI message they
  decode, the one per-block JS allocation left on a production path (section 9). It is the decoders' own change, in
  `lsdjArduinoboy.ts`'s pure functions, not the node's.
- `defineNode` itself keeps to ES2015's library (`Set`, `Object.entries`, `includes`), so the bundle is zod-free but
  not yet MicroQuickJS-ready; `settingsSchema.ts` already keeps to ES5's.

## 9. Found while building

- **The role-to-node mapping is a table, not a `RoleType` field.** Section 4 left open where "roles that have a
  protocol node" is decided, and a `node` field on `RoleType` was the obvious place. It is
  [graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts) instead, pure, zod-free and registry-free:
  each role kind with a node, the node type it is placed as, and how `buildGraph` wires that type (the input its routed
  MIDI arrives on, the system inputs it writes, whether it takes launches, serial out or sends host MIDI). The node
  types are code in the bundle every Engine embeds, so which role kinds have one is a fact of that bundle, not of
  whichever registry a store was handed; and three publishers need it, two of which hold no registry at all (a
  `DirectGraph`, and a `SystemsStore` built without one, as the tests build them). A field on `RoleType` would need
  this table as its fallback anyway, which makes two sources that can disagree about a role. Three tests hold it: the
  registry test that it names a node for exactly the roles `registerDspRoles` registers, `build-graph.test.ts`
  that its type ids are the node modules' own (importing the modules would pull every protocol into whatever imports
  `buildGraph`), and `test/graph/protocol-nodes.test.ts`, which reads each node module's declared ports and holds every
  entry's port names to them exactly (a name a module lacks is a wire the planner refuses, and with it the whole
  document), and holds every type a role is placed as, and every type the table names, to one the production bundle
  registers. `golden/build-graph-roles.json` holds what the table makes `buildGraph` do: one case per protocol node
  type, a controller each way and every routing, recorded from `buildGraph` by `build-graph-role-goldens.test.ts`,
  planned and driven on the engine with real cores by the native golden runner (in CI), and the equivalence rig plans
  its documents from it. Section 6's promise that the rig's documents be generated as `buildGraph` places them is
  therefore kept: it no longer spells any.
- **Host MIDI out by wiring came before the equivalence rig.** Section 7 had the rig third and `rp.host.midi-out`'s
  `systems` form fourth. The rig builds the per-role document, and without the systems form that document has no way
  to say whose host MIDI is whose: the prefixed form needs a 4-byte prefix that no protocol node writes. So the systems
  form landed first, beside the prefixed form, and the rig wired it. As built it takes inputs `in1` to `inN`, 1-based
  as `rp.core.merge`'s, at most 128 (`kMidiOutMaxInputs`, so a commit copies the ids into a fixed array without
  allocating); any 32-bit id, 0 and 4294967295 included, and one id may repeat. Its prepare is stateless: a settings
  change that keeps the inputs is a swap, one that moves them a rebuild.
- **An empty `systems` list means no inputs.** The flip placed `{"systems": []}` when nothing sends host MIDI, which
  until the deletion was the prefixed form, unwired. With the prefixed form gone, an empty list is a node with no
  inputs that sends nothing, and `buildGraph` still places it, so a document has its host outputs whatever its roles;
  no golden document changed. The prefix was the one way `rp.host.midi-out` dropped a message (one too short to carry
  it), so the engine's count of those went with it: `Engine::droppedMessages` is now the plan's queues alone. The host
  service kept its interface id, `rp.engine.host-io/1`, until the follow-up round: `EngineHostIo` had lost its drop
  counter at the deletion, and the follow-up round only moved the id to `rp.engine.host-io/2` to say so.
- **The host's MIDI out is frame-sorted across the node's inputs.** As first built the node handed its inputs' messages
  over input by input, so a later input's message at frame 3 followed an earlier one's at frame 10, and
  `Engine::midiOut` would have come out unsorted had a producer sent past frame 0, which the plugin passes to `writeMidiEvent` as it is and CLAP and LV2 require
  sorted. The kernel's one queue clamped each message to the latest frame already queued. The node keeps the last
  frame handed out in the block and sends `max(frame, last)`: bit-identical while every frame is 0, and
  `Engine::midiOut` is documented as frame-sorted. A settings change that keeps the input count keeps the node
  instance; one that changes it rebuilds.
- **Port names.** The N8 node's input is `midiIn`, where section 2 said `midi`: its output is named for the system
  input it feeds, `midi`, as every protocol node's is, and a port id names one port. `buildGraph` reads each type's
  MIDI input from the table. The routing node's outputs are `sys-<id>`, and its layout refuses two systems under one
  id, which the kernel's zod settings refused with a refinement the zod-free schema has no way to say.
- **The controller's app config is parsed by the publisher, not by `buildGraph`.** Section 3 had `buildGraph` parse
  it; `buildGraph` stays pure and registry-free instead, and the publisher parses it: `ProjectStore.controllerModel`
  for a project, `DirectGraph.setController` for the rest, each through `parseAppConfig(id, raw)` in
  [controller/registry.ts](../packages/retroplug/src/controller/registry.ts), which gives an id no app has an empty
  config (its node then runs nothing). The apps' runtime moved to a zod-free
  [controller/apps/index.ts](../packages/retroplug/src/controller/apps/index.ts), which the node imports; the registry
  pairs each runtime with its menu label and its zod schema, and refuses, when it registers, an app with no schema.
  The rest of what `kernelProjection` did lives in [graph/controller.ts](../packages/retroplug/src/graph/controller.ts):
  the target resolution, the MI.MAP override and the node's settings. The controller node also imports the playback
  predictor's leaf module rather than the `lsdj/playback` barrel, which would have pulled the WRAM reader and the sav
  codec into the bundle.
- **The `midi-routing` and `launchpad` roles are gone, and with them `RoleType`'s `dsp` and `scope` and
  `isSystemStage`.** Only the kernel read them as roles: the MIDI routing and the controller are project settings, which
  `kernelProjection` synthesized into those roles and `buildGraph` now places as nodes, so nothing persisted names
  them. [dspRoles.ts](../packages/retroplug/src/dspRoles.ts) keeps the five roles that have a protocol node, each its
  kind, its zod config schema and, for LSDj, the empty-sav seed hook; `registerDspRoles` keeps its name, since what it
  registers are still the roles whose work is the audio thread's.
- **`DirectGraph`'s `setProjectStages` became `setMidiRouting` and `setController`.** The flip mapped it onto the new
  model; the deletion removed it, since it spelled the routing and the controller as the two roles that went, and its
  calls moved: 18 of them in 14 files (13 under `test-native`, `lsdjPlaybackProbe.ts` among them, and
  `test/systems/store-graph.test.ts`). A `DirectGraph`'s controller now gets the target resolution and the MI.MAP override a
  project's does, since `buildGraph` applies both to any model.
- **The spans are named by node type.** The kernel traced each role's stage as a span inside its one node's
  `process` span, through span globals bound into the audio context. Each role is a node now, so the script host labels
  each script node's `process` span by its type: the bundle's types take labels from `DSP_SPAN_TYPE_BASE` (16) up, in
  registration order, each named by its id. By type rather than node id, since the kernel's role spans were per kind
  too, a type's label is fixed when the bundle loads (one per id would come and go with every document), and a node's
  `create` is never told its id; two systems' nodes of one type show as two spans a block, in plan order. `dsp-kernel`
  became `script-node` (`DSP_SPAN_SCRIPT_NODE`, label 0), now only the fallback for a type past the 1024-label bound.
  The span globals (`spanBegin`, `spanEnd`, `traceName`, `traceArmed`) and the floor and fence that kept JS's spans
  inside the host's went: the kernel's roles were their only user. The profile build's `[profile]` cases went from five
  to four, and a case pinning the embedded bundle's labels (16 to 22, in order) made them five again
  ([spec/08](../spec/08-profiling.md)).
- **The profile benches proved themselves live by allocating.** `dsp-bench` and `nes-bench` checked that their
  counters worked by an allocation count above zero, which the allocation-free nodes broke: `dsp-bench` failed under
  the profile host from the deletion until the bench commit (nothing in the default suites runs it), which checked the
  audio runtime's peak heap instead. That proves nothing about the window (the peak is never reset and the bundle's
  load fills it), so the benches now assert the window's 0 and prove it live with a second window around a block that
  allocates for a newly placed node, which must count. The allocation in the block is not the node's `createJs`
  (`GraphHost::publish` adopts the plan at once in the `renderAudio` pull path, so that runs inside `graph.add` /
  `setPipeline` / `loadRom`) but `fillMessages` making each fresh port's reused `Message[]` and pool the first time the
  node calls `messages()`; for the new `rp.mgb.midi` / `rp.n8.midi-to-fifo`, and for `rp.midi.route`, whose reshape
  freed its port arrays when its outputs grew. Making those arrays at create time would move it out of the block, and
  the assertion would need another trigger.
- **The settings schema, as built.** Each `s` builder parses and writes its JSON Schema as its zod twin does, held by
  `test/graph/settings-schema.test.ts` to zod's own output over 34 values, refusals in zod's own English words, and an
  object drops a key it does not name, as `z.object` does. An array's item needs a default of its own: it is written to
  the JSON Schema and taken for an undefined item, as in zod. The array's own default is normalized through its item
  schema when the schema is built, where zod returns a `.default` as given (a deliberate departure); an enum naming a value twice, and an int with a `NaN` or inverted bound, throw
  when the schema is built, where zod collapses the first and has no second; inherited `Object.prototype` names read
  as missing; and `s.json` takes its type as an explicit
  parameter, since a default such as `{}` says nothing about it. `defineNode` takes any schema with `safeParse` and
  `toJSONSchema`, so a zod schema still serves on the control plane (`define-node.test.ts` keeps one zod case on
  purpose). The script test bundle's toy nodes moved too, which took that bundle from 588942 bytes to 21537.
- **The bundle: 78152 bytes, 34 modules** (49908 bytes of bytecode), against 660567 before B3b (687646 at the flip,
  the seven node types beside the kernel). Getting zod out took two zod-free splits besides the controller apps':
  the LSDj link constants and `midiMapRow` into `lsdjSyncBytes.ts`, out of `dspRoles.ts` and its schemas, and
  `walkTicks` into `ticks.ts`, out of the kernel's module. `test/script/bundle-no-zod.test.ts` builds the bundle with
  the build's own options (`tools/build-session.js` exports them) and fails on any zod module in esbuild's metafile,
  and on the role and config modules (`configSchema`, `systemRoles`, `dspRoles`, `coreRoles`, `projectConfig`), and on
  a bundle over 200 KB, which catches zod arriving pre-inlined in another package's dist, as the path check cannot see.
  It is a `pnpm test` case, run with the runner's own node (`RP_NODE`), not a build gate. The
  bundle's load leaves no cycles in the audio runtime, where B3a's left about 144 KiB for the first quiescent reclaim
  to collect: the `[script][gc]` case that holds that collection now loads a chunk of its own that leaves one.
- **What the equivalence rig showed.** Every recorded digest held on the per-role graph, run beside the kernel node's
  graph from the rig's commit to the flip, and alone since. One case reads a per-node difference, the throw (section
  5): in the block where the second system's LSDj node throws, the third system's mGB node still forwards its MIDI,
  which the case checks, and that block's digest takes the third system as the kernel left it, with nothing. That
  check is true from the commit whose subject begins "the rig's feeds lead its document" on. Until then the rig (and the old
  hand-written one) ran that LSDj node after the third system's mGB, because the `serialOut` feeds stood where the
  systems stood, so the check could not tell a throw that stops only its node from one that stops every node planned
  after it; the feeds now lead the document, and a case holds every structure's executed script-node order to
  `buildGraph`'s document order. One case
  is new, recorded from the kernel node when the rig was built: a controller with its default target launching
  immediately, behind routed MIDI into an MI.MAP cart, then out of the host in blocks where two Game Boys send host
  MIDI too. Section 5's items 2, 3 and 6 were orders no earlier case sent a message through (the controller case
  staged no host MIDI, and its one launch to the host was cued past its last block), so without it the rig could not
  have caught a change in them. Before the rig, a temporary TS cross-check drove `DspKernel` and each node through the
  same blocks and compared every port; it caught 29 of 30 deliberate mutations of the nodes and went with the kernel.
  The thread sanitizer ran the rig's cases clean, and the restructuring one five times more.
- **The flip moved no audio.** The goldens were re-recorded by a one-off script, 840 cases and 238 documents, and the
  audio part of every document is as it was. Strict parity held bit for bit at every commit, nothing re-recorded.
- **Behaviour that differs from the kernel**, none of which a parity case sees:
  - A node's state is keyed by its role's kind (`sys-<id>/<kind>`), where the kernel keyed a stage's by its system and
    its place in the pipeline. So a role removed and added again on a surviving system starts afresh, and a role no
    longer inherits a neighbour's state when a role ahead of it is added or removed.
  - The controller's default target follows a change of first system: the resolved id is in the node's settings and
    in what rebuilds its session, where the kernel's session kept the id it resolved when it was built.
  - The controller rebuilds its session when its parsed app config changes, where the kernel compared the raw config,
    so an edit the app's schema reads as no change keeps the session.
  - Launches go only into a "system" target's `rp.lsdj.sync`; the kernel appended them to the target's inbox, which an
    mGB or a NES stage would have forwarded to the core.
  - `onSettings` runs only on a real change; the kernel re-applied the song table on every push, which nothing could
    observe.
  - A pipeline's configs must be what the store parsed: a node's settings fill their defaults (an `lsdj-sync` with no
    mode is `midiSync`), where the kernel's roles read a missing field as nothing. The stores hand parsed configs: an edit parses its role's,
    and a load does too (`adopt` parses each stored role that has a protocol node by its registered `RoleType` schema,
    before anything else reads it, so the parsed config is the system's everywhere: the node's settings, the serial-out
    capture, the menus and what a save writes). A stored config a schema refuses outright falls back to what this
    ROM's providers suggest for the role, and to the schema's defaults only when they suggest none (a Game Gear
    smsggdj cart is `gg`, not the schema's `sms`). The parse repairs fields (an unknown enum defaults, an integer
    clamps), and it also drops a key the schema does not read, as an edit already did, which the next save no longer
    carries; the repair line, logged once per load after the core has built, names the ROM, each repaired value and
    each dropped key. A `DirectGraph` caller passes its own configs as given.
  - Two roles whose nodes write one system input are not both placed. A project stored with `mgb` whose ROM path now
    holds LSDj gains `lsdj-sync`, and both write `serialIn`, which the planner refuses (`input-taken`) with the whole
    document. `protocolPipeline`, which every publisher builds with, keeps one writer per input and leaves out a kind
    already placed (a node's id is `sys-<id>/<kind>`, so a kind named twice would place two nodes under one id); the
    role stays on the system, so a save keeps it. Which of two is placed: `adopt` moves each role this ROM's providers
    suggest ahead of the earliest role they do not suggest whose node writes an input its own does, so a loaded system
    places the one its ROM calls for (today's LSDj, not the stale `mgb`), and with no collision, no registry or no
    suggestion the order is as stored and the first wins. A save writes the moved order. What is left out is logged
    once, after the core has built. `DirectGraph.setPipeline` sends the document without the later role and takes an
    optional callback that hears what was left out. The serial-out capture and the reported LSDj sync latency
    (`__rp_syncLatencyMs`) follow the `lsdj-sync` the pipeline places (`placedRole`), so a left-out one arms no capture
    and reports no latency. Known limitation: spec/07.
  - A throw stops only the node it is in (section 5).
  - The routing always runs before the controller; the kernel ran its project stages in the order given, which the
    stores always gave as routing first.
  - Where a system has no protocol node for an input, or no controller is enabled, the input or the host I/O node is
    left unwired, where the kernel node's outputs were wired and sent nothing.
- **The bench** ([ScriptBench.test.cpp](../packages/native/test/engine-graph/ScriptBench.test.cpp), hidden:
  `build/bin/retroplug-engine-graph-test "[bench]"`; Release, 44.1 kHz, a Ryzen 9 9950X, medians of 7 runs, three runs
  as ranges). A script node's call is as B3a measured it: 0.084 to 0.092 us a node with nothing to pass and 0.172 to
  0.199 us with one message in and out, flat in node count and block size, with no JS or native allocation; 64 nodes
  take 5.74 to 5.88 us, or 11.8 to 12.1 us with a message each. The protocol nodes are timed in their plan, the routing
  and a node a system, wired as `buildGraph` wires them, over B3a's structure (mGB, an LSDj and a NES on
  `nes-n8-midi`, send-to-all, playing at 120 bpm, a host message every 441 frames, the LSDj's serial out a byte every 8
  frames), against the same plan with no script nodes; no core runs:

  | Block | LSDj in MIDI sync | LSDj in MI.OUT, decoding | The plan alone | JS allocations a block (MIDI sync / MI.OUT) |
  |---|---|---|---|---|
  | 1 frame | 0.933 to 0.954 us | 0.835 to 0.853 us | 0.030 to 0.031 us | 0 / 0.23 |
  | 64 frames | 2.214 to 2.232 us | 3.883 to 3.899 us | 0.040 to 0.043 us | 0 / 15 |
  | 512 frames | 10.78 to 11.36 us | 23.24 to 23.87 us | 0.099 to 0.101 us | 0 / 120 |

  In MIDI sync the nodes allocate nothing, JS or native. The MI.OUT row feeds the LSDj a framed command every 8
  frames (clocks, a note on and off, a CC: five host MIDI messages in every 8 bytes) and checks that the host gets
  every one; its allocations are about 3 for each decoded message, the decoder's emitted arrays (section 8). Against B3a's last kernel-node figures (2.68 to
  2.74, 7.69 to 7.92 and 39.5 to 40.7 us, with 25, 81 and 431 JS allocations a block, measured on another tree and
  day), the per-role nodes in MIDI sync are about 2.9, 3.5 and 3.6 times faster. Part of that is B3a section 9's advice
  taken: only an LSDj's serial out is wired, into its own node, where the B3a bench also fed the kernel mGB's idle
  bytes. The rest is the kernel node's per-block `BlockInput` copy, which B3a's bench found to be its per-message
  cost, gone.
- **The profile benches** (`tools/run-profile.sh stats`, Profile A): `dsp-bench` makes 0 allocations a block (B3a:
  33.81), 0 in its worst block (62), with a peak heap of 307096 bytes, at 16.6 times real time; `nes-bench` 0 (20.14) and
  0 (44), peak 305064 bytes, at 10.8 times real time. The end-of-window collection frees nothing in either. A trace of
  `dsp-bench` (`tools/run-profile.sh trace`) has `rp.midi.route` at 1.37 us a block and `rp.mgb.midi` at 0.83
  (each the whole `process` span of its node, so the `js-call` and `marshal` spans nest inside those figures); the
  means over every `js-call` and `marshal` span in the trace, the two nodes' together, are about 0.95 and 0.11 us,
  against 1428 us of `apu-render` and 2.0 of `state-publish`; `nes-bench`'s has `rp.midi.route` at 1.09 us and
  `rp.n8.midi-to-fifo` at 0.53, against 2151 of `apu-render` and 3.7 of `state-publish` ([spec/08](../spec/08-profiling.md)).
