# Known bugs, batch 2: the three LSDj ones

Three entries from [spec/07](../spec/07-remaining-work.md)'s known bugs, all about LSDj carts:

| # | Bug | Where | Found |
|---|-----|-------|-------|
| 1 | "Is this LSDj" decided by a role's presence in the stored list, not by what the graph places | shell readers, cart views | B3b |
| 2 | MI.MAP's predicted playhead runs ahead of the cart at a tempo divisor of 2 or more | `launchpad`, `lsdj-launchpad` | F3 |
| 3 | MI.OUT decodes about half the NoteOns a song plays | `sameboy` (the synthetic Arduinoboy clock) | LSDj's serial-out node |

User decisions (2026-11-12): readers ask placement and stored roles are kept (section 1); the predictor follows the
divisor the cart is set to (section 2); the MI.OUT fix re-records the four parity cases it moves (section 3). The
process is batch 1's: this design, a plan whose code its author validates on the tree, one task per bug plus docs, each
implemented and given a spec review and an Opus code review, the kit as acceptance and an Opus final review.

## 1. Readers ask what is placed

A system's stored role list can keep a role the graph leaves out: after a ROM swap an mGB cart can still carry
`lsdj-sync`, an LSDj cart `mgb`. `protocolPipeline(roles)` (`packages/retroplug/src/graph/protocolNodes.ts`) decides
what is placed (the stored order, which a load sorts so the ROM's suggested role comes first; one writer per system
input; a kind once), and `syncLatency.ts` and `serialCapture.ts` already read it. Other readers decide by presence:

- `resolveTracker(roles)` (`src/trackerRegistry.ts`, by `markerRole` presence; spec/07 calls it
  `trackerIntegrationFor`), behind the tracker menus, Songs and Assets, song import, the render's tracker label, unsaved
  changes, the live SAV and `projectStore`'s tracker decisions (spec/07's `roles.some((r) => r.kind === "lsdj-sync")`
  there is gone; its tracker questions now go through `resolveTracker`);
- the config rows (`ui/screens/menu/configRows.ts`, `hasRole(sys.roles, row.role)`), which show LSDj's sync cyclers and
  divisor row on an mGB cart that stores a stale `lsdj-sync`;
- `projectParameterMap` (`src/parameterMap.ts`), which takes the first role with a parameter table, so an LSDj cart with
  a stale `mgb` ahead exposes mGB's CC lanes to the DAW;
- the cart views behind the package boundary: `packages/lsdj/ts/ui/hdView.ts` and `overlayView.ts`
  (`hasRole(sys.roles, "lsdj-sync")`), and `packages/risa/ts/ui/overlayView.ts` (`hasRole(..., "risa")`).

**Fix.**
- `placedRole(roles, kind)` beside `protocolPipeline`: the role if the pipeline places it. A role whose kind has no
  protocol node (the core config role, `lsdj-assets`, BlipToaster's `chip`) is never left out by the pipeline, so it
  counts as placed when present; the answer differs from presence only for a role with a node that loses on one writer
  per input or a repeated kind.
- `resolveTracker` matches an integration only by a placed marker role; the config rows show a role's rows only when it
  is placed; `projectParameterMap` takes the first role with a parameter table among the placed ones (a role with no
  node keeps its table).
- The host's `SystemInfo` (`packages/core/ts/host.ts`) gains `placed: readonly string[]`, the kinds the pipeline places,
  filled by the shell's system view from `protocolPipeline`; `@retroplug/core/systemRoles` gains `isPlaced(sys, kind)`.
  The LSDj HD view, the LSDj overlay and the risa overlay use it in place of `hasRole`. Reads of a role's config where
  presence is right (`lsdj-assets`, the core config role, the role editor, persistence, migrations, clone) stay as they
  are.
- Stored roles are never removed: a ROM swapped back brings the role's settings back (E5's rule that what is not placed
  is kept).

**Tests.** An mGB cart storing a stale `lsdj-sync` resolves no tracker, shows no LSDj sync rows and no LSDj HD view or
overlay; an LSDj cart storing a stale `mgb` exposes LSDj's parameters, not mGB's CC lanes; a no-node role keeps its
parameter table; unit cases for `placedRole` and `isPlaced`, and for the system view's `placed`.

**Docs.** spec/04 (which names `placedRole` as if it existed) matches the helper; spec/07's entry is closed and its
stale names corrected.

## 2. The MI.MAP predictor follows the cart's divisor

The LSDj sync node clocks a cart in MI.MAP at `24 / (config.tempoDivisor || 1)` ticks a quarter
(`packages/lsdj/ts/lsdjSyncNode.ts`; the role's `tempoDivisor`, schema 1 to 8, the menu 1/2/4/8). The Launchpad
controller's session (`packages/launchpad/ts/controllerSession.ts`) walks `TICKS_PER_QUARTER = 24` for its predictor, so
at divisor 2 and above its playhead and quantise run that many times ahead. The `launchpad` pack may not import `lsdj`,
and the placer names no tracker field.

**Fix.**
- The controller app's sync rule (`protocol.kinds.controllerApps[].syncOverride`, today `{node, mode, listening, off}`)
  gains an optional `divisor`: the name of the sync node's setting that divides the 24-tick clock.
  `lsdj-launchpad`'s manifest declares `"divisor": "tempoDivisor"`. The kind check
  (`launchpad/ts/control/kind.ts`) accepts it as a non-empty string; `ControllerSyncRule` and `ruleOfData` carry it.
- The placer reads it from what it is already handed: `PlaceSystem.pipeline` carries each placed role's node and
  config. For the driven system it finds the role whose node is `rule.node`, reads `config[rule.divisor]`, and hands the
  session a `tickDivisor` setting: the value if it is a finite number of at least 1, else 1, matching the node's own
  `config.tempoDivisor || 1`.
- The session walks `TICKS_PER_QUARTER / tickDivisor`, the node's expression. `tickDivisor` is read live, not part of
  `sessionShape`, so a divisor changed while a song plays reaches the running session at its next block without a
  rebuild, its launch state kept. The comment that documents the bug (`controllerSession.ts`, near line 110) becomes the
  setting's reason.

**Tests.** `kind.test.ts` (the key accepted, a bad type refused); the placer (the divisor read from the pipeline's
config, 1 when the key or the role is absent); `session.test.ts` (half the ticks a block at `tickDivisor` 2, a live
change without a rebuild); `lsdjMidiMap.test.ts` (a launch released at its row at divisor 2); the real-core
`test-native/lsdj-launchpad` case at divisor 2 (the predicted row holds the cart's polled row over several bars). At
divisor 1 nothing changes.

**Goldens that move, structurally:** `golden/build-graph-roles.json` and the generator fixture `expected/protocol.ts`
(the session's settings gain `tickDivisor`, the manifest data `divisor`), and `build-graph.test.ts`'s exact rule.
Re-recorded with `RP_GOLDEN_UPDATE=1`, the diff checked to hold only those fields. No audio golden. The two scenario
transcripts must stay byte-identical between Node and MicroQuickJS; they are re-recorded only if the walk reaches their
output, which at divisor 1 it does not.

## 3. MI.OUT feeds the Game Boy 0, as the Arduinoboy does

Investigated 2026-11-12 (`.superpowers/kb2des/miout-report.md`, with the traces and the probe). In Drive capture
`SameBoySystem::writeAudioSample` clocks one bit a host audio frame while `SC == 0x80` and feeds **1** back on the Game
Boy's serial input. The Arduinoboy (trash80's `Mode_LSDJ_Midiout.ino`, `getIncommingSlaveByte`) drives it **0**
throughout. LSDj's sender (ROM bank 3, `$7BE4`) writes `SB = $80 | byte` (a start bit, then 7 bits) and its serial
interrupt (`$3617`) re-arms the port (`SC = $80` at `$363C`) after every transfer, whatever it has to send. The clocks
issued on that re-armed port read back the fed bits: 1s, which the decoder takes for start bits, so the stream is
re-framed into 7-bit windows across two wire bytes. How many stray bits there are depends on the host rate, hence B0's
seventh defect. Measured on the suite's MI.OUT song: LSDj writes the same 43 wire bytes at every rate (2 starts, a stop,
20 NoteOns); today's decode gets 12 NoteOns at 44.1 kHz, 14 at 48 (with program changes the song never sends), 5 at 96;
feeding 0, all 20 with the song's values at 8, 16, 22.05, 32, 44.1, 48, 96 and 192 kHz. LSDj's own writes are
bit-identical either way.

**Fix.** `GB_serial_set_data_bit(gb_, false)`, with a comment citing the firmware's `GB_SET(clock, 0, 0)` and LSDj's
re-arm and saying why a fed 1 reads as a start bit; the "idle-high" comment above it corrected. No clock, frame, CPU
write or audio sample changes.

**Proof** (fixture-gated: the Arduinoboy LSDj ROM). `lsdj-midiout.test.ts` runs the song at 44.1, 48 and 96 kHz (a file
per rate, as `lsdj-mastersync` does) and holds the exact sequence (`0xFA`, NoteOns 48 52 55 60 64 67 72 36 48 52,
`0xFC`, `0xFA`, the same ten), 20 NoteOns, 2 starts, 1 stop, every value one the song authors, no Program Change and no
clock byte (the song sends none); its in-block frame assertions stay; its "clocks > 4", met only by the defect, goes.

**Parity re-record** (approved): `lsdj-modes` midiOut and `blocks-tiny-lsdj` midiOut-1, -fl and -fl256, in the fix's
commit. Each case's MIDI-out hash and its work-RAM hash move (LSDj stores the received byte at `$CC20`: `$00` now, as on
hardware, where it was `$FF`); the plan's step checks the baseline diff touches only those two fields, no segment or
audio hash.

**Tests built on the defect, reworked:** `render-blocks`' `0xF8` check becomes a NoteOn check; dsp-bench's MI.OUT window
counts NoteOns, not clocks, and its "uneven clocks" comment goes; the two `HostIo.test.cpp` cases keep their structural
checks (the capture switched in the block its decoder's mode is, bytes reaching `rp.lsdj.serial-out`) and assert that
mGB's idle line, `$00` now, decodes to nothing. The fixture-free suites then prove the wiring and the idle line; the
real decode is proved by the fixture-gated cases and parity, and spec/07 records that CI sees no real-core MI.OUT
decode.

**The `0x7F` clock path.** No real-core case sends one. The plan's author checks LSDj's MI.OUT sender for a `$7F` send:
if a setting makes LSDj send clocks, that setting gets a case; if not, the decoder's unit test
(`packages/lsdj/ts/test/lsdjArduinoboy.test.ts`) is its coverage and spec/07 says so.

**Unchanged** (measured with the fix): master sync (its capture listens and feeds nothing), `lsdj-arduinoboy-slave`,
`lsdj-capture-settings`, link-cable capture, `retroplug-sameboy-test`, every audio lane, and every golden outside the
four cases.

**Docs.** `docs/lsdj.md` (the snippet, the `0x80+` row and the "7D 7F 70 69" record), `lsdjArduinoboy.ts`'s module
comment (the capture's 8-bit groups are not wire bytes; the decoder reads the bit stream), the B0 design's section 2
(the seventh defect, fixed), `docs/lsdj-serial-out-design.md`'s last bullet (and its "about 40 NoteOns" figure: LSDj
sends about 65 to 70 in that window), spec/07's entry closed.

## 4. Plan, execution and acceptance

**Tasks**, each test-first: 1 placement (section 1); 2 the divisor (section 2); 3 MI.OUT (section 3, its parity
re-record inside it); 4 docs (spec/04, spec/07, AGENTS.md if anything there says otherwise). Implementers on the pi
models, a spec review (tree identity against the plan's validated diffs) and an Opus code review a task, an Opus final
review; follow-ups as batch 1 ran them. Work files in `.superpowers/kb2des`, `kb2plan`, `kb2exec`.

**Acceptance**, on the tip: the serial kit (strict parity with fixtures and `RP_FAIL_ON_SKIP=1`: only the four approved
cases differ from the pre-batch baseline, and only in `midiOut` and `ram`; TSan and ASan; BlipToaster and NESVJ; the
guarded SDL legs, `sdl:controller` among them since the session changes); the fixture-gated MI.OUT and Launchpad
cases; the scenario transcripts byte-identical. No run opens an audio or MIDI device outside the guard and the fakes.

## 5. As built

The four tasks landed as planned, each tree identical to the one the plan's author validated, then three follow-up
passes took the reviews' findings. What differs from sections 1 to 4:

- **Section 1: the readers use `placedKinds`, and `placedRole` is gone.** The shell's readers compute the placed kinds
  once and test them; the per-kind helper had no caller but its test and was deleted. `placed` on `SystemInfo` lists
  the kinds the pipeline places AND every present kind with no protocol node (a core's config role, a node-less tracker
  marker such as `risa`), because `isPlaced` lives in core and cannot ask the shell's table; it answers false when a
  view carries no `placed`. An LSDj cart storing a stale `mgb` now exposes no DAW lanes (LSDj declares none). Left open
  in spec/07: node-less tracker markers are still decided by presence, so a NES project storing both `risa` and
  `bliptoaster` takes the first in product order.
- **Section 2: the walk and its tests are the session node's.** The divisor tests are in
  `test/script/nodes/controller-session.test.ts` and `lsdjMidiMap.test.ts` (the session over the real `rp.lsdj.sync`, at
  divisors 1, 2, 3, 5, 6, 7 and 8, the cued row arriving at clock 96 every time). The real-core case checks the step the
  cart leaves each row on, not "the predicted row holds the polled row over several bars". The placer passes a value
  only if it is an integer of at least 1 (the role schema has already rounded and clamped it to 1..8). A divisor changed
  mid-song gives one block of no ticks, as the cart's own sync node does. `bar` and `beat` quantise still count host
  bars (spec/07).
- **Section 2, found reviewing: the controller asks placement too.** `controllerSyncOverride` called a cart driven when
  its sync role was stored and not placed (and when the controller named no live system), so the menu said "MI.MAP -
  driving it" while launches reached no node. The sync rule now carries a required `role` (`lsdj-launchpad`: `lsdj-sync`)
  which the kind check holds to the rule's `node` through the product's `ROLE_NODES`; the placer asks the pipeline for
  the node and the menu the view's placed kinds for the role. A ROM that is no tracker's shows no cart-sync row (spec/07
  records the missing "nothing to drive" row).
- **Section 3: one file per rate, with a shared helper** (`lsdj-midiout.test.ts`, `-48k`, `-96k`, `lsdjMidiOut.ts`).
  LSDj sends no clock byte of its own in MI.OUT, but N, X and Y send their value unchecked, so an `N7F` puts `$7F` on
  the wire and the Arduinoboy turns it into `0xF8`, as hardware does; a real-core case pins it. dsp-bench's MI.OUT window
  decodes about 40 NoteOns, as the serial-out design said: section 3's "about 65 to 70" was wrong (the phrase has eight
  empty rows; its period is 1.87 s, not 1.06). A fixture-free `HostIo` case puts back a real core's capture reaching the
  decoder in CI: mGB in Drive capture under a master-sync decoder, one `0xF8` at each captured byte's frame. mGB's first
  captured byte, `0x66`, is a ROM constant (its boot loads `SB`), not power-on randomness.
- **Docs beyond section 4's list:** spec/06 (the sync rule's keys, one pinned parity defect left), AGENTS.md (the same
  count; tracker integrations' order is behaviour for two node-less markers), the B0 design's sections 2 and 9, the
  serial-out design's sections 4 and 11, and the B3b design's `placedKinds`.

## 6. Execution

- Design 6690c8bc3; plan 6d4251eac (written and validated by an Opus agent against the tree, replayed from its text to
  the identical tree; the user moved the batch to Anthropic models after the design).
- Tasks, by Sonnet, each tree checked identical to the validated one and Opus-reviewed: 63cabe651 (placement), 098dd2c11
  (the divisor), 1e3c96b73 (MI.OUT, with the four-case parity re-record: `midiOut` and `ram` only, no audio hash),
  4f6f21db0 (docs).
- Follow-ups (Sonnet): 95da94dce, 9ef3f63bf, 106927c4a, c70ba76c7 (the per-task findings); Opus final review READY;
  dabc7648f, f19d947c2 (its minors, B6 the controller's placement); an Opus review of those found the optional `role`
  unchecked: 34d28cc01, 52e534693 (its refusal cases shown failing against the earlier check).
- Acceptance: the serial kit at c70ba76c7, 45 legs, all exit 0 (strict parity with fixtures and `RP_FAIL_ON_SKIP=1`, no
  skip; TSan and ASan; BlipToaster 57/57, NESVJ 19/19; `sdl:controller` OK; SDL PipeWire 27/0/0). The parallel kit at
  f19d947c2: 48 legs exit 0; at the tip, 52e534693: 48 legs exit 0 (BlipToaster 57/57, NESVJ 19/19, SDL PipeWire
  27/0/0).
