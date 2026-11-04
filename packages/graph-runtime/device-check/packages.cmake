# The packages the device check carries, in registration order: the one list both of its builds read. The image
# (CMakeLists.txt) registers their `device` entries as a device builds them, and the case generator (cases/) registers
# the same entries as the desktop builds them, to write the cases the image runs. The generated cases header carries
# the entries it was made for, and the image refuses, at compile time, a header made for another list.
#
# retroplug-device-mode-test holds the list to the manifests: every package with a `device` entry must be on it, so a
# package that becomes device-capable is device-checked from then on.
set(RP_DEVICE_CHECK_PACKAGES core dsp n8)
