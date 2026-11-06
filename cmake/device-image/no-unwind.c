/* What every RP2350 image links in place of libgcc's ARM unwinder (cmake/device-image.cmake's rp_device_executable).
 *
 * newlib's setjmp.o, which MicroQuickJS's engine links (its exceptions are setjmp and longjmp), carries .ARM.exidx
 * entries whose personality routine is __aeabi_unwind_cpp_pr0, and that one reference pulls libgcc's whole unwinder
 * into the image (unwind-arm.o, libunwind.o and pr-support.o, about 4 KB, which the box runs from RAM). Nothing in an
 * image unwinds: it builds with -fno-exceptions, and the link trap refuses every way to throw. So the routine is
 * defined here, strong, as a trap of its own: a call to it stops the image (the SDK's panic, which prints and halts).
 * Were something to need the real unwinder, it would pull unwind-arm.o, which defines the routine again, and the link
 * would fail on the two definitions, loudly. It is not one of the trap's wrapped symbols (device-image.cmake's list):
 * those are calls the image must not make, and this one only an index entry names, which no code here ever follows. */
#include "pico.h"

void __aeabi_unwind_cpp_pr0(void) { panic("__aeabi_unwind_cpp_pr0: nothing in this image unwinds"); }
