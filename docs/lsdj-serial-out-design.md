# LSDj's serial out as its own node: host MIDI at each byte's frame

**Date:** 2026-10-28 · **Branch:** `feature/modularize` · **Status:** built: section 10's five commits, then the
fixes their reviews found (section 12)

LSDj sends host MIDI two ways, both decoded from what it clocks out of its link port: MI.OUT (lsdj-sync mode
`midiOut`, Arduinoboy's Mode_LSDJ_Midiout: notes, CCs, program changes and realtime) and master sync (mode
`masterSync`: a clock a byte, a song-row NoteOn and 0xFA before a run's first, 0xFC after a 200 ms silence;
[lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md)). Both go out at frame 0 of the block after their
bytes. This change sends each message at its own byte's frame, in the block that byte was captured, by moving both
decoders out of `rp.lsdj.sync` into a node of their own that runs after the Game Boy. It is the first item of
[spec/07-remaining-work.md](../spec/07-remaining-work.md) section 5 after B3b
([modular-graph-phase-b3b-design.md](modular-graph-phase-b3b-design.md)), grown by the decisions in section 2.

## 1. What happens today

`rp.lsdj.sync` runs before its Game Boy, because in the MIDI-sync, MI.MAP, keyboard and passthrough modes it feeds
the system's `serialIn` and `buttons`. So it reads the system's `serialOut` over a feedback wire: last block's
captured bytes, each with its frame in that block ([lsdjSync.ts](../packages/retroplug/src/script/nodes/lsdjSync.ts)).
Its `emitMidiOut` sends every decoded message at frame 0, so a byte captured at frame `f` of block N goes out at
frame 0 of block N + 1. LSDj at tempo 128 sends about 51 clocks a second, one per 861 frames at 44.1 kHz; a
2048-frame block (46 ms) holds two or three, and they go out together, at frame 0, a block late. A case's last
block of bytes is never decoded at all: it waits for a block that does not come.

The host's list is built by `rp.host.midi-out`
([HostIoNodes.cpp](../packages/native/src/host/graph/HostIoNodes.cpp)), which hands its inputs out one after
another and sends each message at the later of its frame and the last frame handed out in the block (B3b's
follow-up C). With every frame 0 that is a plain concatenation; with real frames it raises a later input's earlier
frame to an earlier input's, so a second LSDj's clocks would bunch behind the first's.

## 2. Decisions taken

The user's, on 2026-10-28:

- **The decoder moves after the Game Boy** (rather than keeping `rp.lsdj.sync` and sending each message at its
  byte's frame in the block after, which stays a block late and has to clamp the frames that do not fit when the
  next block is shorter, as FL-style ragged splits make it).
- **MI.OUT moves with master sync.** One node owns the Game Boy-to-host direction; `rp.lsdj.sync` keeps the
  host-to-Game Boy one.
- **The decoders' emit becomes allocation-free in the same change**, since its signature changes anyway: it writes
  into a reused buffer instead of building an array per message. That closes spec/07's last per-block JS
  allocation on a production path.
- `rp.host.midi-out` becomes a merge by frame; the profile's `apu-render` span becomes the plan's whole run.

## 3. The two nodes

An `lsdj-sync` role on a Game Boy places:

- **`rp.lsdj.sync`**, node `sys-<id>/lsdj-sync`, before the systems as now: the routed MIDI and the controller's
  launches in, `serialIn` and `buttons` out. It loses its `serialOut` input and its `midiOut` output; in modes
  `midiOut` and `masterSync` it sends nothing.
- **`rp.lsdj.serial-out`**, node `sys-<id>/lsdj-sync/out`, after the systems: `serialOut` in, over a forward wire
  from `sys-<id>.serialOut`, and `midiOut` out, into the `rp.host.midi-out` input `rp.lsdj.sync` held, under the same
  system id, so the host's input order and its `systems` setting do not change.

Both take the role's config as their settings, through the one `lsdj-sync` schema, the MI.MAP controller's forced
mode included (`controllerSyncOverride`): a cart the controller forces to `midiMap` stays silent on host MIDI, as it
is today. The decoder reads only `mode`. It is placed whatever the mode, so a mode switch stays a change of
settings, as it is today, and a document's shape does not depend on a mode; in the other modes its capture is off,
its input empty, and it costs one script call a block that sends nothing. It is placed only on a Game Boy, as the
feedback wire was.

In [protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts) a spec names a node placed after its system
beside the one before it: `rp.lsdj.sync`'s spec drops `serialOut` and `midiOut` and names `rp.lsdj.serial-out`,
whose own spec says it reads its system's `serialOut` and sends host MIDI. `protocolPipeline`, `placedRole` and the
one-writer rule are unchanged: the decoder writes no system input.
[buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts) pushes the decoders after the systems in slot order,
before the cart mirror and the host outputs, and its comment that every script node goes before the systems becomes
the rule that a script node reads only earlier nodes (or a feedback wire). The role goldens' forward-wire check (B3b follow-up E) holds it. The new type registers last in
[bundle.ts](../packages/retroplug/src/script/bundle.ts), so the production span labels 16 to 22 keep their types and
it takes 23.

## 4. The decoder

`rp.lsdj.serial-out` reads its block's `serialOut` into its reused byte and frame arrays, as `rp.lsdj.sync` does
today, and runs the mode's decoder on them, with the frames now this block's.

**Master sync** (`arduinoboyMasterSyncBlock`). The rule is the master-sync fix's: every byte a clock, a run's first
byte its song row's NoteOn and 0xFA before its clock, and a stop when the silence since the run's newest byte passes
200 ms (`stop` frames at the block's rate). What changes is where each goes:

- A byte at frame `a` sends its messages at `a`.
- The stop goes at the first frame the silence passes 200 ms, `floor(lastAt + stop) + 1`: `lastAt + stop + 1`
  while `lastAt` is whole, as it is unless the host's rate changed during the run. Checked before each byte (a
  restart then goes out after its stop, at the byte's frame) and at the block's end: a mark inside the block sends
  0xFC there; one past it carries to the next block, with `lastAt` moved back by the block's length. While the host's
  rate holds, a mark is never behind the block it is sent in, since each block's end sends any mark inside it. A run
  carried into a block at another rate has its silence counted again in that rate's frames, and a mark that then
  falls before the block goes at its first frame (section 12).
- `MasterSyncState.lastFrames` goes: the bytes are this block's. `MasterSyncState.rate`, the rate `lastAt` is
  counted at, is added (section 12).

So what is sent, in order, is what is sent today: the same messages from the same bytes, only earlier, and at their
frames. The one addition is a case's last block, now decoded.

**MI.OUT** (`arduinoboyDecodeSerialOut`). The bit framing and the byte protocol are unchanged: a message goes out at
the frame of the captured byte whose bits complete it. A command whose bits straddle two blocks completes in the
later one, at its completing byte's frame there. Its known defect (what it decodes depends on the host's sample
rate, pinned by `lsdj-modes`' `midiOut` case, B0's seventh defect) stays as it is: this change moves MI.OUT's
frames, not its bytes.

**The emit.** Both decoders call `emit(frame, len, b0, b1, b2)` (a message is one to three bytes), and the node
fills one of three reused arrays, one per length, and sends it: `send` takes an array-like and sends all of it, so a
message needs an array of its own length. The script host copies a message's bytes into the port's queue as it is
sent, so the array is free again when `send` returns, and a warm block allocates nothing.

The scratch: MI.OUT's bit accumulator and pending command are kept on the decoder's instance across mode changes, as
`rp.lsdj.sync`'s scratch kept them; master sync's sub-object is dropped whenever the mode is not master sync, as now.
A throw stops only the decoder's block, as for any script node.

## 5. Host MIDI out

`rp.host.midi-out` merges its inputs by frame: each step hands out the head with the earliest frame, the lower input
on a tie. Each input arrives frame-sorted (a port's queue raises a frame sent behind a later one), and
`min(max(frame, last), frames - 1)` stays after the merge as a guard, so `Engine::midiOut` is frame-sorted and
inside the block whatever arrives, a document holding one `rp.host.midi-out` (`Engine::setGraph` refuses a second,
section 12). With every frame 0 the merge is today's order, input by input, so it moves nothing on its own. The
merge allocates nothing (one head index per input, `kMidiOutMaxInputs` of them) and costs messages times inputs a
block; the sink it hands each message to still allocates, `Engine::midiOut` taking a `std::vector` per message, as
spec/07's item from B3a records. Nothing it exposes changes, so the host-io service id stays `rp.engine.host-io/2`.

What a host does with a frame is unchanged: the plugin hands it to the DAW (`writeMidiEvent`), the RPC drains record
it, and the SDL standalone sends each message once its block is rendered, without it (its RtMidi out has no
timestamps; scheduling it is the standalone's own work).

## 6. The profile's `apu-render`

[Engine.cpp](../packages/native/src/host/engine/Engine.cpp) opens `apu-render` where the last script node's
`process` ended, which is right only while every script node runs before every system. It becomes the plan's whole
run, from where it starts to where it ends, with the script nodes' spans inside it (Chrome trace events nest), so it
is true wherever a script node sits, as it will need to be once users place nodes. Its figure then includes the
script nodes' few microseconds against about 1400 of emulation, and
[spec/08-profiling.md](../spec/08-profiling.md) says so. `ScriptHost::lastProcessEnd` goes if nothing else reads it.

## 7. What moves

The audio does not: nothing here changes what a core is fed or how it is clocked. Parity
([spec/06-build-test.md](../spec/06-build-test.md)) re-records the cases whose host MIDI out moves, all of them
fixture-only:

- `lsdj-modes`: `midiOut`, `masterSync-44100`, `masterSync-48000`.
- `blocks-tiny-lsdj-master`: all six.
- `blocks-tiny-lsdj`: the three `midiOut-*` cases.

Each re-recorded case keeps every segment's lanes, its SRAM and its work RAM byte for byte, and every other case in
those groups stays as it is; the baseline diff is the record. Its MIDI out keeps the same message sequence, only
earlier and at its frames, plus whatever its last block decodes to. `render-blocks`' MI.OUT case compares
`renderBlocks` with `renderAudio` and `drainMidiOut` on the same engine, not a baseline, so it holds unchanged.

Beyond parity: [ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp)'s MI.OUT and
master-sync digests, which B3b held to what the kernel sent, are re-recorded as the decoder's, and its header says
that those two now differ from the kernel by design. The role goldens
([build-graph-roles.json](../packages/retroplug/test/graph/golden/build-graph-roles.json)) re-record with the decoder
in every LSDj document; the B1 translator's goldens (build-graph.json) place no roles, so they hold unchanged.

## 8. Tests

- **The decoders**, in TS: each master-sync message at its byte's frame; the stop at its mark mid-block, at a
  block's first frame, and carried over a block with no bytes; a restart in the block of its stop; an MI.OUT command
  straddling two blocks at its completing byte's frame; and, run in a block's sequence, the same messages in the same
  order as the decoder before this change, which a test holds by sequence (frames aside).
- **The node**: `rp.lsdj.serial-out`'s own test file, which takes the MI.OUT and master-sync cases from
  `lsdj-sync.test.ts`; that file checks `rp.lsdj.sync` sends nothing in those modes. No allocation in a warm block,
  as the other protocol nodes' tests check.
- **The graph**: `pnpm test graph` holds the new spec and placement (ids, forward wire, the host-MIDI input taken
  over, the forced mode, GB only, document order); the role goldens re-record and `BuildGraphGoldens` drives them.
- **Host MIDI out**: `HostIo.test.cpp`'s case for a later input's earlier frame changes from clamped to merged, a
  case pins ties to input order, and one pins the guard.
- **Native**: [lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts) adds, at every
  split and both rates, that consecutive clocks in a run lie about a tempo period apart in absolute frames, where a
  long block today puts them at one frame. Its extra 1-frame render, there because bytes reached the decoder a block
  late, goes. [lsdj-midiout.test.ts](../packages/retroplug/test-native/lsdj-midiout.test.ts) checks MI.OUT's frames
  likewise.
- **Profile**: a `[profile]` case pins label 23 to `rp.lsdj.serial-out` and `apu-render` spanning the plan; the
  benches gain an LSDj in master sync, held to zero allocations a block.

The core's own byte timing moves by tens of frames with the block split
([lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts) says so), which predates this
change: the decoder adds no split dependence of its own, so the tests hold each message to its byte, not to an
absolute time shared by every split.

## 9. Docs

[spec/04-roles-protocol-nodes.md](../spec/04-roles-protocol-nodes.md) (the two nodes of an `lsdj-sync` role),
[spec/02-native-host.md](../spec/02-native-host.md) (the merge),
[spec/08-profiling.md](../spec/08-profiling.md) (`apu-render`, label 23, the allocation figures),
[spec/07-remaining-work.md](../spec/07-remaining-work.md) (this item and the decoders' allocation item close),
[docs/lsdj.md](lsdj.md) (MI.OUT and master sync timing), and AGENTS.md's engine-graph paragraph.

## 10. Landing

On `feature/modularize`, prototyped first on a local branch `proto/lsdj-serial-out`, then a plan generated from it,
as for B3b. The commits, each verifiable alone:

1. `rp.host.midi-out` merges by frame. Strict parity unchanged, nothing re-recorded.
2. `apu-render` spans the plan. A profile-build change; parity unchanged.
3. The decoders' allocation-free emit with frames, `rp.lsdj.sync` still sending at frame 0. Parity unchanged: it
   proves the new emit sends the same bytes.
4. `rp.lsdj.serial-out`: the split, the frames, the exact stop. Re-records the cases in section 7, the rig's
   digests and the goldens.
5. Docs.

Accepted on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`, plus the suites, TSan and the Reaper leg.

## 11. Left as it is

- **The core's byte timing moves with the split** (section 8): a SameBoy matter, its own change if wanted.
- **MI.OUT's rate-dependent decode** (section 4), still pinned.
- **The SDL standalone sends host MIDI at block time** (section 5).
- **The capture mode as a Game Boy node setting**, Phase C as spec/07 says: a mode switch still publishes the
  document and sets the capture through `ConfigField::SerialOutCapture`, so for a block the two can disagree, as
  the master-sync design's section 10 accepted.

## 12. Found while building

- **No node of RetroPlug's can hand the merge an unsorted input, so the guard needed one of its own.** Section 8 had
  a case pin the guard. An `rp_queue` is frame-ordered by contract (`rp_node.h`), and every node of RetroPlug's
  writes its queues through `rp::graph::reserveMessage` (`pushMessage` calls it, as a script node's `send` does),
  which raises a frame written behind a later one; so does the test feed: in the old case whose input went back in
  frame, its queue had raised that frame before `rp.host.midi-out` saw it.
  [HostIo.test.cpp](../packages/native/test/engine-graph/HostIo.test.cpp) pins the guard with a test-only node,
  `rp.test.raw-feed`, that writes its queue's items directly and out of order, as a node on the C ABI may; like
  `reserveMessage` it drops and counts a message that does not fit, and the case checks it dropped none.
- **`apu-render` is recorded after the spans it holds.** A span is stored as it closes, so a block's `apu-render`,
  opened at the plan's start, follows its script nodes' process spans in the trace. The trace-event JSON gives each
  span its start and length, so nothing that reads it depends on the order.
- **LSDj's first clock of a run comes early.** Section 8's spacing check has to allow it. In every split and at both
  rates a run's clock gaps follow LSDj's own repeating pattern of six, 846 to 874 frames at 44.1 kHz (921 to 951 at
  48), within 1.8% of the tempo's period, while its first tick after START comes at 0.78 of a period (673 frames at
  44.1 kHz). [lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts) holds the gaps
  within 3% of the period, which still refuses a clock moved to its block's start (a 1024-frame block is 19% past
  the period), and a run's first gap within 30%. The split moves the whole stream by up to 32 frames and its gaps
  not at all, as section 8 said of the core: the shift is the prototype's measurement, since the test logs each
  split's gaps and not where its stream lies.
- **No re-recorded case gained a message.** Section 7 expected each case's last block, decoded at last, to add what
  it held. In all twelve the new MIDI out is the old one's messages in the same order with nothing after them, each
  a block earlier than before, but for one stop in each 1-frame master-sync case, which stays in its block; a
  checker over the old and new baselines and their dumps found every lane, SRAM and work RAM of the three groups,
  and every other case's whole digest, unchanged. Every new message lies off frame 0 but in the 1-frame splits,
  whose blocks have no other frame, and for one clock of 85 in each master-sync `fl256` case, whose byte the core
  completed at its block's first frame.
- **A hand-edited `lsdj-sync` on another system loses its host MIDI input.** The decoder is placed only on a Game
  Boy, as the feedback wire was, but `rp.host.midi-out`'s input for the system had been `rp.lsdj.sync`'s, placed on
  any system. Such a system (no ROM provider attaches `lsdj-sync` to anything but a Game Boy) has its `rp.lsdj.sync`
  placed and no decoder, so `rp.host.midi-out` has no input for it, where it had one that sent nothing;
  [build-graph.test.ts](../packages/retroplug/test/graph/build-graph.test.ts) pins it, and no golden holds one.
- **The controller's order case checks the merge.**
  [ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp)'s case for a controller's
  launch beside two LSDjs' host MIDI held the system tags in input order; under the merge the host's list is ordered
  by frame and then input. The case checks the exact interleave each press sends, as frame:system,
  `0:1 7:2 7:3 23:2 307:3 607:3`: the launch first at frame 0, then Game Boy 2's clock and Game Boy 3's on the tie
  at 7, Game Boy 3's ahead of Game Boy 2's NoteOn at 23. That the list is sorted by (frame, system) would not tell
  the merge from the old concatenation, which raised each frame to the last handed out and so sent Game Boy 3's
  first clock at 23, behind that NoteOn (`0:1 7:2 23:2 23:3 307:3 607:3`), sorted all the same. Its first digest
  re-recorded with the LSDj modes' and the controller session's, every count unchanged.
- **The decoder reads nothing outside its two modes, though its input there is not always empty.** Section 3 said
  its input is empty there; it does not read it at all, a test handing it one that throws, so its call costs the
  mode check. In `ScriptBench`'s MIDI-sync structure, whose stand-in clocks the idle line into it, it reads none of
  it. In a project the input can hold bytes: the capture follows the role's stored mode (`serialOutCaptureFor`), so
  an LSDj stored in MI.OUT or master sync whose cart the MI.MAP controller forces (to `midiMap`, or to `off`) keeps
  capturing, and leaving those bytes unread is what keeps that cart as silent on host MIDI as section 3 wants
  (spec/07 records the capture following the stored mode).
- **A spec's `out` is a string.** [protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)'s `out` names
  a `PROTOCOL_NODES` key, but typing it as one would make the `as const` table refer to its own keys, so `outNodeOf`
  casts it and [protocol-nodes.test.ts](../packages/retroplug/test/graph/protocol-nodes.test.ts) holds every `out`
  to a key whose spec writes no system input. It holds the wiring too, which `buildGraph` alone had kept: a spec
  that reads a system's `serialOut` is some entry's `out`, and an `out` reads neither routed MIDI nor launches,
  since `buildGraph` wires `serialOut` only into a node it places after the systems and the other two into none.

The commits' reviews found more, fixed in the commits after them:

- **A drop in the host's rate during a run sent master sync's stop at a negative frame.** The node outlives a change
  of rate (`Engine::setSampleRate` re-activates the graph's instances rather than making new ones; the plugin's
  `sampleRateChanged` and the SDL standalone's settings call it), and so does its run. The run's newest byte was
  carried in frames at the old rate and its mark taken with the new block's `stop`: a run carried at 48 kHz to
  `lastAt` -9116, its mark 485 at that rate, marked frame -295 in a 44.1 kHz block, which `send` wrapped through
  ToUint32 to 4294967001, and `rp.host.midi-out` raised every later host message of that block to it. Any run whose
  silence had passed 200 ms times the new rate over the old at the drop (183.75 ms from 48 to 44.1 kHz) had its stop
  marked behind its block; a rise sent the stop late instead. `MasterSyncState.rate` now carries the rate `lastAt`
  is counted at, read off each block's transport, and a call at another rate counts the silence again in the new
  rate's frames: it is time, so 200 ms stays 200 ms. `lastAt` stays fractional and the mark floors once,
  `floor(lastAt + stop) + 1`: a floor at each rescale compounded over two changes in one silence and brought the
  mark a frame early (at 48, then 44.1, then 48 kHz, frame 393 for 394), and for a whole `lastAt` the mark is the
  old `lastAt + stop + 1`, so a constant rate, every parity case among them, sends what it sent. The mark is clamped
  at 0, which only a rise needs: it can put a mark less than an old-rate frame before the block, where the old rate
  had it at the block's first frame.
  [lsdj-arduinoboy.test.ts](../packages/retroplug/test/dsp/lsdj-arduinoboy.test.ts) holds a drop, a rise, the clamp
  and two changes in one silence, and
  [lsdj-serial-out.test.ts](../packages/retroplug/test/script/nodes/lsdj-serial-out.test.ts) a drop and a rise
  through the node's transport.
- **The rig's re-record re-blessed what reached the systems.** Its one digest hashed the system inputs and the host
  outputs together, so re-recording the three cases whose host MIDI out moved (the LSDj modes and the controller's
  two) took their system inputs into the new values unchecked: in `lsdj-modes`' nine modes, `rp.lsdj.sync`'s
  `serialIn` and `buttons`. Every case now records a second digest beside the first, over the system inputs alone.
  Its values were run on the rig at 0d781dd4, the commit before the decoder moved, where every first digest still
  held the kernel's value: every second digest was equal there, the three re-recorded cases' among them, so the move
  changed no system input.
- **`Engine::midiOut` is frame-sorted only within one `rp.host.midi-out`.** Section 5's promise held for the one
  `buildGraph` places, but a document holding two would append the second's merge behind the first's, and the
  plugin writes the list to the DAW as it is (CLAP and LV2 require the order). Nothing places two (every golden
  holds one), so `Engine::setGraph` refuses a second (`midi-out`), as it refuses a second `rp.host.midi-in`,
  `rp.host.controller-in` or cart mirror.
- **No document holds a feedback wire, and every golden is now held to that.** The role goldens' forward-wire check
  passed feedback wires over, and `build-graph.test.ts` looks for one in an LSDj's structure alone:
  [build-graph-role-goldens.test.ts](../packages/retroplug/test/graph/build-graph-role-goldens.test.ts) refuses a
  feedback wire in any golden as it refuses a backward wire, since one into a script node would hand it last block's
  values and nothing would show it. So the rule `buildGraph` keeps is that every node reads only nodes placed before
  it, a feedback wire the exception none of its documents takes.
- **The guard kept the host's list in order but not inside its block.** A script node's `send` carries a frame at or
  past the block's end as given, and wraps a negative one there (the rate drop's stop, above, went out at
  4294967001); the guard handed such a frame on to `Engine::midiOut` and the plugin's `writeMidiEvent`, and DPF
  passes each frame to CLAP, VST3 and LV2 unchecked. The guard now caps each frame at the block's last,
  `min(max(frame, last), frames - 1)`, so such a message goes out there, and so does every one after it in that
  block. [HostIo.test.cpp](../packages/native/test/engine-graph/HostIo.test.cpp) feeds it frames at and past the
  block's length and one wrapped from -1 through `rp.test.raw-feed`. No producer of RetroPlug's sends such a frame,
  so parity holds unchanged.
- **dsp-bench's LSDj windows needed a floor, and stay out of the valgrind modes.** MI.OUT's clocks come unevenly
  (at the default warmup none in a window's first 20 blocks, 8 in its first 100, 40 in 400), so a window of
  `RP_BENCH_BLOCKS` 5, 10 or 20 failed the playing check for want of one: a window runs at least 100 blocks, and the
  clock bound, like the zero, holds only at the default warmup. Their boots and warmups do not shrink with
  `RP_BENCH_BLOCKS`, so `tools/run-profile.sh`'s valgrind modes, which lower it, would have run several times longer
  with an LSDj's setup in the census of the mGB workload they attribute: `RP_BENCH_LSDJ=0` leaves the windows out,
  and those modes default to it.
- **dsp-bench's MI.OUT window decodes about half the NoteOns its song plays.** It sends 20 NoteOns in 400 blocks
  (9.3 s), where the phrase's eight notes at tempo 128 come to about 40. Likely MI.OUT's rate-dependent decode
  (section 4, B0's seventh defect); not looked into.
