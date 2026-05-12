# Step 06 — MIDI input/output routing

**Status:** Done.

## Goal

Deliver host-supplied MIDI events to systems. Implement a routing policy
(broadcast / per-channel / per-instance) so a multi-instance project can be
played from one MIDI track. Make `SystemBase::onMidi` and per-system serial
buffers real for the first time.

## Depends on

- [Step 05](./05-multi-instance.md) (routing decisions only matter for N>1).

## Architecture introduced

- **`MidiEvent`** — passthrough of DPF's `MidiEvent` shape (frame-offset +
  data bytes). Already arrives as a `run()` argument; we just stop discarding
  it.
- **`MidiRouting`** — `enum class { SendToAll, FourChannelsPerInstance,
  OneChannelPerInstance, MidiChannelToInstance }`. Ported names from
  [old/src/core/ProjectState.h](../old/src/core/ProjectState.h).
- **`Project::dispatchMidi(MidiEvent*, count, routing)`** — DSP-side
  dispatcher: maps each event to the receiving system(s) per the routing rule,
  then calls `system->onMidi(...)` on each.
- **`SystemBase::onMidi`** — already declared (no-op default). Concrete
  override on `SameBoySystem` injects bytes into a per-system serial buffer
  that roles can consume; MGB/LSDJ roles attached to the system pick up the
  events from there.
- **No C++ system-level MIDI logic in step 06.** Each `SameBoySystem` just
  remembers the last block's events and exposes them via a small accessor that
  roles read. The actual interpretation (sync, passthrough, Arduinoboy) is the
  role's job — steps 07/08/09.

## Tasks

1. **Receive MIDI.** Wire `LVGLPluginDSP::run`'s `MidiEvent*` parameter into
   `project_.dispatchMidi(...)` after the command-queue drain.
2. **Implement `dispatchMidi`** for `SendToAll` first (broadcast). The
   per-channel/per-instance routing modes need an established system ordering;
   match the old project's iteration order.
3. **`SameBoySystem::onMidi`** stores the events in a per-block
   `std::vector<MidiEvent>` and forwards to each `RomRole::onMidi`. (No role
   in step 06 — list will be empty; events are discarded. Step 07 onward
   actually consumes them.)
4. **`ProjectConfig::midiRouting`** — plain enum, reflectcpp-serialized. UI
   menu lets the user pick.
5. **MIDI output.** Roles may want to *send* MIDI back (e.g. Arduinoboy
   master mode generating clock). Add a `SystemIo::midiOut` queue per system
   and gather them into DPF's `writeMidiEvent`. Defer the actual filling-in
   until step 09; just plumb the path.

## Verification

- Carla → MIDI input → plugin. With no roles attached, MIDI is received and
  silently dropped (no crash; verify via printf instrumentation).
- With multiple instances, switch routing modes via the menu and confirm the
  routing decision in a debug log.
- Connect a clock source on MIDI channel 1; once step 08 lands, LSDJ should
  sync to it.

## Risks / open questions

- **Sample-accurate MIDI.** DPF's `MidiEvent` carries a `frame` offset.
  Roles may need to apply events at the right sub-block sample (e.g. clock
  ticks at exactly the right tempo). The role's `onProcessBlock` already
  receives the same `AudioBlockInfo` SameBoy uses; keep events ordered by
  `frame` and let roles consume them with offsets.
- **Routing for 5+ instances.** `FourChannelsPerInstance` doesn't scale.
  Either error out for N>4, or wrap-around. Match old behaviour.
- **MIDI thru.** Some setups want MIDI passed through to the host's MIDI
  output as-is. Add a `passThrough` flag on `ProjectConfig`. Cheap.
