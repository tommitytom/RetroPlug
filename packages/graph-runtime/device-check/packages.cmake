# The packages the device check carries, in registration order: the one list both of its builds read. The image
# (CMakeLists.txt) registers their `device` entries as a device builds them, and the case generator (cases/) registers
# the same entries as the desktop builds them, to write the cases the image runs. The generated cases header carries
# the entries it was made for, and the image refuses, at compile time, a header made for another list.
#
# retroplug-device-mode-test holds the list to the manifests: every package with a `device` entry must be on it, so a
# package that becomes device-capable is device-checked from then on.
set(RP_DEVICE_CHECK_PACKAGES core dsp n8 device-io devtest)
#
# The context the image gives each script pack it registers (devtest's: rp.devtest.script-transpose and
# rp.devtest.script-nest), in place of its manifest's heapBytes, which is the desktop device-mode tests' figure behind
# the planner's 256-place queues. The image runs both of the pack's nodes at once behind the device MIDI input's
# 64-place queue (script-plan.json), for which the device-pack toolchain's 3/2 check passes the pack at 39792 bytes.
# The image registers the pack with it (cmake/packages.cmake's RP_SCRIPT_HEAP_BYTES_<package>),
# retroplug-device-mode-test runs the check's script plan and bench over a registration of the same, and the image
# prints what the RP2350 holds of it after its script plan's run.
set(RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES 40960)
