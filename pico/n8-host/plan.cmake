# The box's built-in plan (docs/modular-graph-phase-e3-design.md sections 2 and 5): plan.json beside this file, and
# what it is exported for. Included by the firmware's CMakeLists.txt, which exports it at build time with
# retroplug-export-plan and embeds the blob, and by the desktop's tests (packages/native/cmake/tests.cmake), whose
# golden pins the blob these give, so a change to the format, a node's settings or one of these is a reviewed diff.
#
# plan.json is the bridge, MIDI in to the cart: rp.device.midi-in (the MIDI the box parses and stages, every message
# as it came), rp.bliptoaster.midi-policy, the cart's MIDI policy as a TypeScript node (packages/bliptoaster, a device
# script pack, docs/modular-graph-phase-e4-design.md section 6), dropping what BlipToaster's FIFO is not fed (poly
# aftertouch and channel pressure, high-rate streams a Launchpad floods and no ROM here reads, and system common and
# real time, which the bridge never forwarded), and rp.n8.nes, the cart. A graph document takes no comment, so the why
# is here.
#
# The packages are the box's product (the firmware's retroplug_product), which the export holds the plan's types to, as
# the loader holds them at boot, so a plan naming a type the box does not register fails the firmware's build.
#
# The rate and block are the design's: 48 kHz and 256 frames (5.3 ms). The arena is the image's static arena, which
# the plan must fit at export (and which the loader holds it to at boot), the export's figure for the plan with room
# over it (the box's boot lines say how much the plan takes).
include_guard(GLOBAL)
set(N8_HOST_PLAN "${CMAKE_CURRENT_LIST_DIR}/plan.json")
set(N8_HOST_PLAN_SAMPLE_RATE 48000)
set(N8_HOST_PLAN_MAX_FRAMES 256)
set(N8_HOST_ARENA_BYTES 65536)
set(N8_HOST_PACKAGES core device-io n8 bliptoaster)
