# The device check's plan (docs/modular-graph-phase-e3-design.md section 7): plan.json beside this file, and what it
# is exported for. Included by the image's CMakeLists.txt, which exports it at build time with retroplug-export-plan
# and embeds the blob, and by the desktop's tests (packages/native/cmake/tests.cmake), which export it the same way and
# run the same check over it, so the plan the image runs is the one the desktop holds.
#
# plan.json is a core and DSP plan with everything a plan can hold but a resource: rp.devtest.source (a stereo signal,
# messages and control changes) downmixed into a coupled pair (rp.devtest.lockstep `a` and `b`, linked); `a` through a
# gain and summed with `b` into a biquad whose Q a constant drives through a wire; the biquad through a delay back into
# `b`, a block late (the feedback wire), and into rp.devtest.digest, which also hears the source's messages and its
# control stream. The delay sets maxSamples (a tenth of a second), as a plan for the device must: its one-second
# default would take most of the arena. A graph document takes no comment, so the why is here.
#
# The rate and block are the box's: 48 kHz and 256 frames. The arena is the image's static arena, the one the per-type
# checks use (DeviceCheck.hpp's kArenaBytes, which the image and the test hold this to): 400 KiB.
#
# script-plan.json is the script plan (DeviceCheck.hpp's kScriptPlan*): the host's MIDI input (rp.device.midi-in, fed
# the check's stimulus) through two TypeScript nodes of the devtest script pack, rp.devtest.script-transpose (seven
# semitones up) and rp.devtest.script-nest (which passes its messages on from the engine's deepest C re-entry), into
# rp.devtest.digest, which hears messages alone. Exported as plan.json is, at the same rate and block, against the same
# arena.
include_guard(GLOBAL)
set(RP_DEVICE_CHECK_PLAN "${CMAKE_CURRENT_LIST_DIR}/plan.json")
set(RP_DEVICE_CHECK_SCRIPT_PLAN "${CMAKE_CURRENT_LIST_DIR}/script-plan.json")
set(RP_DEVICE_CHECK_PLAN_SAMPLE_RATE 48000)
set(RP_DEVICE_CHECK_PLAN_MAX_FRAMES 256)
set(RP_DEVICE_CHECK_ARENA_BYTES 409600)
