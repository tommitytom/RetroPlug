/* What a device image needs of MicroQuickJS's C runtime beyond the engine (graph-runtime's device engine:
 * packages/graph-runtime/native/CMakeLists.txt), in a Pico SDK image under the link trap
 * (cmake/device-image.cmake).
 *
 * abort. The engine calls it on a state it cannot go on from (a corrupt bytecode, an opcode it does not know). newlib's
 * abort raises SIGABRT through its signal table, which it sets up on the heap (_init_signal_r, _malloc_r), so linking
 * it would put the heap in the image and the trap refuses it; the engine library wraps the symbol instead
 * (--wrap=abort), and every call in the image, the engine's and any other's, reaches this: the SDK's panic, which
 * prints its message on the console and stops. Standalone, the box's watchdog then restarts it; with a debugger
 * attached it stays stopped (panic ends at a breakpoint, and the box's watchdog is paused on debug, box.cpp) until
 * someone resumes or resets it. This ties the device engine to the Pico SDK's panic, the one SDK every image that
 * links it (the box, the device check) builds on; a device on another SDK gives its own. setjmp and longjmp (the
 * engine's parser, which JSON.parse and the Function constructor reach) link as newlib has them: neither reaches the
 * heap. */
extern void panic(const char* fmt, ...) __attribute__((noreturn));

void __wrap_abort(void) { panic("MicroQuickJS: abort"); }
