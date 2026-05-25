# Release sanity check

Manual checklist to run before tagging a release. Unit tests + `make -C
build cli-smoke` + `make -C build validate` are assumed green already —
this list covers things only a human can confirm (audio sounds right, UI
feels right, DAW round-trips work).

Work top to bottom. Stop and investigate the first failure rather than
ploughing through.

## 0. Setup

- [ ] Clean `build/` and rebuild Release with `-j$(nproc)`. All six
      artifacts present in `build/bin/` (standalone, `.clap`,
      `-vst2.so`, `.vst3/`, `.lv2/`, `retroplug-cli`).
- [ ] `make -C build validate` passes (clap-validator + pluginval).
- [ ] `git status` clean — no stray `build/ui/bundle.js` or
      `PluginService.ts` staged.

## 1. Plugin formats load

In your DAW of choice, scan plugins and instantiate each format. For
each, load `roms/lsdj/lsdj9_4_2.gb`, hit play in the DAW, confirm audio.

- [ ] CLAP
- [ ] VST3
- [ ] VST2
- [ ] LV2
- [ ] Standalone (`build/bin/retroplug`, JACK target)

## 2. ROM kinds

In one instance, swap through each ROM kind via the menu. Each should
boot, render frames, and (where applicable) make sound.

- [ ] Generic GB ROM (any game)
- [ ] LSDJ stock (`roms/lsdj/lsdj9_4_2.gb`)
- [ ] LSDJ aboy (`roms/lsdj/lsdj9_3_3-arduinoboy.gb`)
- [ ] mGB
- [ ] GBA ROM
- [ ] NES ROM (Mesen)

After each ROM swap: video updates, audio switches cleanly with no
clicks/runaway DSP, menu title reflects the new system.

## 3. SameBoy options

On a Game Boy instance:

- [ ] Cycle Model (Auto / DMG-B / CGB-C / CGB-E / AGB). LSDJ boot
      colours / sound chip differ as expected.
- [ ] Toggle Fast boot on/off. Reset — boot ROM skips when on.
- [ ] Reset — same ROM reboots cleanly.

## 4. Input

- [ ] Keyboard drives the focused instance (arrows + A/B/Start/Select).
- [ ] Plug a controller — gamepad drives the focused instance.
- [ ] Settings → Keyboard profile: cycle profiles, mapping changes
      take effect immediately.
- [ ] Settings → Pad profile: same as above.

## 5. Save state / SRAM

In LSDJ:

- [ ] New SRAM — fresh save, no banks.
- [ ] Write a chain, Save SRAM. Reset. SRAM persists.
- [ ] Save State. Edit something. Load State. Edits reverted.
- [ ] Save SRAM As… / Save State As… — file pickers open, write to
      chosen path.
- [ ] Load State… — picks an external state file, loads correctly.
- [ ] Reload on ROM change toggles SRAM behaviour when the ROM file
      on disk is replaced.

## 6. Multi-instance

- [ ] Add instance — second tile appears, both render and produce
      audio simultaneously.
- [ ] Duplicate instance — clone matches source state.
- [ ] Tab cycles focus between tiles; menu title tracks focus.
- [ ] Remove instance — tile disappears, audio from that system stops.
- [ ] Project → Layout: Auto / Row / Column / Grid all lay out
      sensibly at 2, 3, and 4 instances.
- [ ] Project → Zoom 1x → 6x scales tiles correctly without cropping
      or stretching.

## 7. Link groups (LSDJ link cable)

Two LSDJ instances, both Link group: 1, both PROJECT → SYNC=LSDJ:

- [ ] Right margin shows `LEAD` on the started instance, `SYNC` on
      the other once START is pressed.
- [ ] Visual lockstep — cursors advance together.
- [ ] Audio stays in time across both tiles (no drift over ~30 s).
- [ ] Setting Link group: Off on one side breaks sync immediately.

## 8. MIDI input

Send MIDI from the DAW (or a hardware controller into standalone):

- [ ] Send to all — every instance receives notes.
- [ ] 4 ch / inst — instance N receives channels (4N…4N+3).
- [ ] 1 ch / inst — instance N receives channel N.
- [ ] ch → inst — channel determines the destination instance.

## 9. LSDJ sync modes

For each, set the mode via the menu and drive from the DAW. Confirm
LSDJ's PROJECT screen reflects the change and audio responds.

- [ ] Off — no transport response.
- [ ] MidiSync — DAW play starts LSDJ, tempo follows.
- [ ] Arduinoboy (aboy ROM only) — sync as MidiSync; verify on aboy
      build.
- [ ] MidiMap — DAW notes trigger LSDJ rows by note number.
- [ ] KeyboardMidi — DAW notes drive LSDJ as keyboard input.
- [ ] Passthrough — DAW MIDI passes through to LSDJ.
- [ ] MI.OUT (aboy ROM only) — LSDJ row notes emit MIDI from the
      plugin's MIDI out, captured by the DAW.

## 10. LSDJ kit editor

With an LSDJ instance focused:

- [ ] Menu → Kit Editor opens the editor overlay.
- [ ] Load a `.kit` file — samples appear and play.
- [ ] Save kit back to the ROM/SRAM — kit persists across reset.

## 11. Audio routing

Plugin declares 8 outputs. With a 2-instance project:

- [ ] Stereo — all systems mixed to outs 1/2.
- [ ] 2 ch / inst — instance 0 on outs 1/2, instance 1 on outs 3/4.
      Confirm by routing each pair to a separate DAW track.
- [ ] 1 ch / inst — each instance mono on its own out (1, 2, 3, …).

## 12. Project save / load

- [ ] Project → Save project — file picker writes a `.retroplug` (or
      whatever the extension is) project.
- [ ] Load it back in a fresh plugin instance — every system, ROM
      path, SRAM, LSDJ sync mode, link group, layout, zoom, audio
      routing, MIDI routing restored.
- [ ] DAW state recall: save the DAW project, close, reopen. Plugin
      restores to the same multi-instance state (this exercises DPF
      `setState` rather than the Save project file).

## 13. Recent files

- [ ] Recent menu lists last few loaded ROMs/projects, newest first.
- [ ] Selecting one reloads it. Kind icon (ROM vs project) matches.
- [ ] List persists across DAW restart.

## 14. Settings / paths

- [ ] Settings → Open settings folder opens the OS file browser at
      the config directory.
- [ ] About panel opens, version string looks right.

## 15. Stress / soak

- [ ] Run a 4-instance project with all instances producing audio for
      ~5 minutes. No xruns, no memory growth visible in `top` /
      Activity Monitor, no UI lag.
- [ ] Open/close the plugin window 10x in a row. No leaks, no crashes.
- [ ] Switch DAW sample rate (44.1 / 48 / 96 kHz) — audio continues
      cleanly after each change.

## 16. Cross-platform (if applicable to the release)

Repeat sections 1, 5, 12, 14 on every OS being shipped:

- [ ] Linux
- [ ] macOS
- [ ] Windows
