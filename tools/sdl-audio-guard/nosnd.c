// The LD_PRELOAD guard every hardware-capable harness runs under (docs/modular-graph-phase-f5b-design.md section 7):
// refuse the device nodes a test has no business opening, before the syscall, so a run on a desk full of gear can never
// reach it. What it covers:
//
//   - every ALSA node (/dev/snd/*) but the sequencer and the timer: PortAudio's ALSA backend probes every card at
//     Pa_Initialize, SDL's PipeWire driver reaches a card through its ALSA monitor, and a default stream plays to
//     whatever the machine's default card is;
//   - a video capture card (/dev/video*, /dev/media*, /dev/v4l-subdev*), which a session manager's v4l2 or libcamera
//     monitor enumerates and opens;
//   - a serial device (/dev/ttyACM*, /dev/ttyUSB*, /dev/ttyDbgProbe*, /dev/serial/*): a Pico box, or a debug probe;
//   - a HID device (/dev/hidraw*) and raw USB (/dev/bus/usb/*).
//
// Built by tools/sdl-audio-guard/build-guard.sh and prepended to LD_PRELOAD by guard-env.sh, which every harness that
// could reach one of them sources: tools/run-sdl-smoke.sh, tools/run-sdl-controller.sh, tools/run-sdl-pipewire.sh and
// tools/reaper-env.sh (so every reaper:* leg is guarded, not only a kit's). The `nosnd` name is the shim's first, ALSA
// only, scope; the kit's legs and variables keep it so a rename moves nothing.
//
// A refusal happens HERE, before the syscall, which is why the log is the proof: strace cannot see a call that was never
// made. Both the refusal and a look at the subsystems being touched are therefore the same compile, and this header is
// the whole of it.
//
// NOSND_LOG=<file>: append one line per refused path and per dlopen of an audio client library that was NOT already
// loaded, so a run can prove what it reached. A library's own dlopen is not a new audio dependency coming up: libasound
// is linked by rtmidi and SDL, and its config loader dlopens itself on every snd_seq_open, which made a null-pinned
// run's log non-empty for the MIDI client it is entitled to. Only a library that was NOT already loaded is logged, and
// that is the rule the counts in a harness's summary mean.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// NOSND_LOG=<file>: append one line per refused path and per audio-library dlopen. O_APPEND on every line, so threads
// that refuse at the same moment cannot interleave inside one line.
static void note(const char* what, const char* p) {
    const char* log = getenv("NOSND_LOG");
    if (!log || !*log) return;
    FILE* f = fopen(log, "a");
    if (!f) return;
    fprintf(f, "%d %s %s\n", (int)getpid(), what, p ? p : "(null)");
    fclose(f);
}

// Every other node the guard refuses, by prefix. The guard denies rather than allows, so a prefix that also matches a
// name the kernel invents later (a second capture card, another ttyACM) is the safe direction.
static const char* const kRefusedPrefixes[] = {
    "/dev/video", "/dev/media", "/dev/v4l-subdev", "/dev/ttyACM", "/dev/ttyUSB", "/dev/ttyDbgProbe",
    "/dev/hidraw", "/dev/bus/usb/", "/dev/serial/",
};

/** `p` with the path forms a caller can spell and still reach the same node folded away: runs of `/` and any `/./`
 *  segment (`/dev//snd/x`, `/dev/./snd/x`). `/../` is left alone - resolving it needs the working directory, and a
 *  library that reaches for a device through `/..` is not a shape any of them uses. Written into `out`, always
 *  terminated, and the result is only used for matching (the log carries the caller's own spelling). */
static const char* canonical(const char* p, char* out, size_t size) {
    size_t w = 0;
    for (const char* r = p; *r && w + 1 < size;) {
        if (*r == '/') {
            while (r[1] == '/') ++r;  // collapse a run
            if (r[1] == '.' && r[2] == '/') {  // a `/./` segment is no segment
                r += 2;
                continue;
            }
        }
        out[w++] = *r++;
    }
    out[w] = 0;
    return out;
}

static int refused(const char* p) {
    if (!p) return 0;
    char folded[4096];
    const char* const c = canonical(p, folded, sizeof folded);
    if (strncmp(c, "/dev/snd/", 9) == 0) {
        if (strcmp(c + 9, "seq") == 0 || strcmp(c + 9, "timer") == 0) return 0;
        note("refused", p);
        return 1;
    }
    for (size_t i = 0; i < sizeof(kRefusedPrefixes) / sizeof(kRefusedPrefixes[0]); i++) {
        if (strncmp(c, kRefusedPrefixes[i], strlen(kRefusedPrefixes[i])) == 0) {
            note("refused", p);
            return 1;
        }
    }
    return 0;
}

// Whether a library is ALREADY loaded when the dlopen runs (DT_NEEDED, or an earlier dlopen of it): then this is not a
// new audio dependency coming up. Searched per call, through dl_iterate_phdr's own `data` - the callback runs inside that
// call on the caller's thread, so nothing here is shared between two threads' dlopens.
struct Lookup {
    const char* want;  // the basename being looked for
    int         found;
};

static int phdr_cb(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    struct Lookup* look = data;
    if (!info->dlpi_name || !*info->dlpi_name) return 0;
    const char* base = strrchr(info->dlpi_name, '/');
    base = base ? base + 1 : info->dlpi_name;
    if (strcmp(base, look->want) == 0) {
        look->found = 1;
        return 1;  // stop walking
    }
    return 0;
}

static int already_loaded(const char* name) {
    if (!name) return 0;
    const char* base = strrchr(name, '/');
    struct Lookup look = {base ? base + 1 : name, 0};
    dl_iterate_phdr(phdr_cb, &look);
    return look.found;
}

void* dlopen(const char* name, int flags) {
    static void* (*real)(const char*, int);
    if (!real) real = (void* (*)(const char*, int))dlsym(RTLD_NEXT, "dlopen");
    if (name && (strstr(name, "jack") || strstr(name, "pipewire") || strstr(name, "pulse") || strstr(name, "asound") ||
                 strstr(name, "portaudio")) &&
        !already_loaded(name))
        note("dlopen", name);
    return real(name, flags);
}

// glibc's own rule for whether an open call carries a mode argument: O_CREAT, or BOTH bits of O_TMPFILE. Testing
// `f & O_TMPFILE` alone is true for any O_DIRECTORY open, because O_TMPFILE includes O_DIRECTORY - and then va_arg would
// read an argument the caller never passed.
#if defined(O_TMPFILE)
#define NEEDS_MODE(f) (((f) & O_CREAT) != 0 || ((f) & O_TMPFILE) == O_TMPFILE)
#else
#define NEEDS_MODE(f) (((f) & O_CREAT) != 0)
#endif

#define WRAP(name, ...)                                                                              \
    int name(const char* p, int f, ...) {                                                            \
        static int (*real)(const char*, int, ...);                                                   \
        if (!real) real = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, #name);                   \
        if (refused(p)) { errno = ENOENT; return -1; }                                               \
        mode_t m = 0;                                                                                \
        if (NEEDS_MODE(f)) { va_list a; va_start(a, f); m = va_arg(a, mode_t); va_end(a); }                \
        return real(p, f, m);                                                                        \
    }
WRAP(open)
WRAP(open64)

#define WRAPAT(name)                                                                                 \
    int name(int d, const char* p, int f, ...) {                                                     \
        static int (*real)(int, const char*, int, ...);                                              \
        if (!real) real = (int (*)(int, const char*, int, ...))dlsym(RTLD_NEXT, #name);              \
        if (refused(p)) { errno = ENOENT; return -1; }                                               \
        mode_t m = 0;                                                                                \
        if (NEEDS_MODE(f)) { va_list a; va_start(a, f); m = va_arg(a, mode_t); va_end(a); }                \
        return real(d, p, f, m);                                                                     \
    }
WRAPAT(openat)
WRAPAT(openat64)

// The _FORTIFY_SOURCE entry points a caller compiled with fortification reaches instead of open/openat.
int __open_2(const char* p, int f) { return open(p, f); }
int __open64_2(const char* p, int f) { return open64(p, f); }
int __openat_2(int d, const char* p, int f) { return openat(d, p, f); }
int __openat64_2(int d, const char* p, int f) { return openat64(d, p, f); }
