#include "host/script/ScriptHost.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32) || defined(__linux__) || defined(__GLIBC__)
#include <malloc.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

extern "C" {
#include "quickjs.h"
}

#include "graph/CanonicalJson.hpp"
#include "graph/Descriptor.hpp"
#include "graph/Queue.hpp"

// The production script-node bundle: src/script/bundle.ts, esbuilt to a global-code IIFE and tjsc'd to bytecode as a
// C array (packages/native/cmake/bundles.cmake, retroplug-script-bundle).
extern "C" {
extern const std::uint8_t  rp_script_bundle[];
extern const std::uint32_t rp_script_bundle_size;
}

namespace rp::host {

namespace {

using graph::PortKind;

constexpr const char* kNodesGlobal = "__rp_scriptNodes";  // packages/retroplug/src/script/register.ts
constexpr const char* kBuiltInPack = "rp.script";          // the built-in pack's id, and its package's

// The property names the block thread reads and writes, as atoms made once, so naming one allocates nothing.
enum Atom : std::size_t {
    kFrames, kTransport, kSampleRate, kTempo, kPpq, kPlaying, kFrame, kData, kFlags, kStart, kChanges, kValue,
    kLength, kFault, kProcess, kOnSettings, kPortsChanged, kActivate, kAtomCount
};
constexpr const char* kAtomNames[kAtomCount] = {"frames", "transport", "sampleRate", "tempo",      "ppq",          "playing",
                                                "frame",  "data",      "flags",      "start",      "changes",      "value",
                                                "length", "fault",     "process",    "onSettings", "portsChanged", "activate"};

// How the host makes a property on an object it made, or a slot of an array JS holds that is not the array's own: an
// own data property, writable, enumerable and configurable as a plain assignment would make it, a failure thrown. A
// define never walks the prototype chain, so in the realm every script node of a pack shares, an accessor one node (or
// the bundle) hung on Object.prototype or Array.prototype never runs for another's; and a JS_SetProperty onto a
// property that is the object's own finds it there and walks no further either. A node takes slots back as a matter of
// course (a shift, a splice, a shorter length), so whether a slot is the array's own is asked at each write
// (Glue::putSlot). A field the host fills stays its object's own unless the node deletes it, which nothing a node
// should do needs, so those writes are plain sets, and one onto a field the node deleted walks the prototype chain:
// asking at every field write as well would cost each script node's call about 12 ns a block (5 fields; 14% of an empty
// pass-through's), and about 4 ns more a message.
constexpr int kDefine = JS_PROP_C_W_E | JS_PROP_THROW;

// `text` into out[0..capacity), NUL-terminated, cut where it does not fit at the end of a character: a cut by bytes
// alone can land inside a UTF-8 sequence, and leave text a reader refuses. The bytes written, the NUL not counted.
std::size_t copyText(char* out, std::size_t capacity, std::string_view text) {
    if (capacity == 0) return 0;
    std::size_t n = std::min(text.size(), capacity - 1);
    if (n < text.size())
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;  // text[n], the first byte left out
    std::memcpy(out, text.data(), n);
    out[n] = '\0';
    return n;
}

// Text written into a fixed buffer piece by piece, each piece cut at a character where it does not fit (copyText).
struct TextOut {
    char*       out;
    std::size_t capacity;
    std::size_t n = 0;
    void add(std::string_view text) { n += copyText(out + n, capacity - n, text); }
    void number(long long value) {
        char digits[24];
        std::snprintf(digits, sizeof digits, "%lld", value);
        add(digits);
    }
    void size(std::size_t value) {
        char digits[24];
        std::snprintf(digits, sizeof digits, "%zu", value);
        add(digits);
    }
};

// A pack's memory limit as every refusal at it says it: "64 MiB memory limit" when it is a whole number of
// mebibytes, else "65536-byte memory limit". Into `t`, so the block thread says it allocating nothing.
void addLimit(TextOut& t, std::size_t limit) {
    constexpr std::size_t kMiB = std::size_t{1} << 20;
    if (limit % kMiB == 0) {
        t.size(limit / kMiB);
        t.add(" MiB");
    } else {
        t.size(limit);
        t.add("-byte");
    }
    t.add(" memory limit");
}

// The same, as a string (control thread).
std::string limitText(std::size_t limit) {
    char text[48];
    TextOut t{text, sizeof text};
    addLimit(t, limit);
    return std::string(text, t.n);
}

void copyError(rp_error* err, std::string_view what) {
    if (err) copyText(err->message, sizeof err->message, what);
}

void clearException(JSContext* ctx) { JS_FreeValue(ctx, JS_GetException(ctx)); }

// A thrown value as text, into a fixed buffer (the block thread allocates nothing native): an Error's message alone
// when `messageOnly` (a settings refusal is the definition's own words), else "Name: message".
void describe(JSContext* ctx, JSValueConst thrown, bool messageOnly, char* out, std::size_t capacity) {
    JSValue message = messageOnly && JS_IsError(thrown) ? JS_GetPropertyStr(ctx, thrown, "message") : JS_UNDEFINED;
    std::size_t length = 0;
    const char* text = JS_ToCStringLen(ctx, &length, JS_IsString(message) ? message : thrown);
    if (!text) clearException(ctx);
    copyText(out, capacity, text ? std::string_view(text, length) : std::string_view("a value with no text"));
    if (text) JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, message);
}

// The pending exception, described (control thread: it may allocate).
std::string exceptionText(JSContext* ctx, bool messageOnly) {
    JSValue thrown = JS_GetException(ctx);
    char text[sizeof(rp_error::message)];
    describe(ctx, thrown, messageOnly, text, sizeof text);
    JS_FreeValue(ctx, thrown);
    return text;
}

// obj[name](...args), with obj as this.
JSValue callMethod(JSContext* ctx, JSValueConst obj, const char* name, int argc, JSValueConst* argv) {
    JSValue fn = JS_GetPropertyStr(ctx, obj, name);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return JS_ThrowTypeError(ctx, "the definition has no %s", name);
    }
    JSValue result = JS_Call(ctx, fn, obj, argc, argv);
    JS_FreeValue(ctx, fn);
    return result;
}

// JSON.stringify(value) into out; false with the exception pending when it has no JSON form.
bool stringify(JSContext* ctx, JSValueConst value, std::string& out) {
    JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) return false;
    std::size_t n = 0;
    const char* text = JS_IsString(json) ? JS_ToCStringLen(ctx, &n, json) : nullptr;
    if (text) {
        out.assign(text, n);
        JS_FreeCString(ctx, text);
    }
    JS_FreeValue(ctx, json);
    if (!text) JS_ThrowTypeError(ctx, "the value has no JSON form");
    return text != nullptr;
}

// The promise jobs pending in rt, at most `bound` of them, `onThrow` called for each that throws (with its exception
// pending, for it to take). True when none is left.
template <class OnThrow>
bool runJobs(JSRuntime* rt, int bound, OnThrow&& onThrow) {
    for (int n = 0; n < bound; ++n) {
        JSContext* ctx = nullptr;
        const int r = JS_ExecutePendingJob(rt, &ctx);
        if (r == 0) return true;
        if (r < 0) onThrow(ctx);
    }
    return !JS_IsJobPending(rt);
}

// Message i taken back out of q with its payload, as if it had never been pushed: the messages after it, and their
// payloads, move down. A queue's payloads lie in push order, so theirs follow its.
void eraseMessage(rp_queue& q, std::uint32_t i) {
    const rp_message gone = q.items[i];
    const std::uint32_t after = gone.offset + gone.size;
    std::memmove(q.bytes + gone.offset, q.bytes + after, q.bytes_used - after);
    for (std::uint32_t k = i + 1; k < q.count; ++k) {
        q.items[k - 1] = q.items[k];
        q.items[k - 1].offset -= gone.size;
    }
    --q.count;
    q.bytes_used -= gone.size;
}

// The calling thread's CPU time, in nanoseconds (0 where it cannot be read): what the call deadline measures, so a
// stall that is not the node's own (the thread preempted, the process stopped, a debugger or a VM pausing it, the
// machine asleep) costs it nothing. Windows counts it in clock ticks (about 16 ms), far finer than a deadline.
std::int64_t threadCpuNs() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetThreadTimes(::GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
    const auto ns = [](const FILETIME& f) {
        return static_cast<std::int64_t>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) * 100;
    };
    return ns(kernel) + ns(user);
#else
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
    return static_cast<std::int64_t>(t.tv_sec) * 1000000000 + static_cast<std::int64_t>(t.tv_nsec);
#endif
}

bool sameObject(JSValueConst a, JSValueConst b) {
    return JS_IsObject(a) && JS_IsObject(b) && JS_VALUE_GET_PTR(a) == JS_VALUE_GET_PTR(b);
}

// The bytes the platform's allocator gave `p`: what a pack's runtimes count against its memory limit, so a platform
// that cannot say is one the limit cannot hold, and is refused here rather than built with the limit silently off.
std::size_t usableSize(const void* p) {
#if defined(__APPLE__)
    return malloc_size(p);
#elif defined(_WIN32)
    return _msize(const_cast<void*>(p));
#elif defined(__linux__) || defined(__GLIBC__)
    return malloc_usable_size(const_cast<void*>(p));
#else
#error "the script host's memory limit needs the allocator's usable size of a block on this platform"
#endif
}

// One writer (the thread running blocks, or the control thread while quiescent), so a plain increment, kept atomic
// for the readers on other threads.
void bump(std::atomic<std::uint64_t>& c, std::uint64_t by = 1) {
    c.store(c.load(std::memory_order_relaxed) + by, std::memory_order_relaxed);
}

#ifdef RETROPLUG_PROFILE
// The same for a running maximum: c raised to `to` when it is below.
void bumpMax(std::atomic<std::uint64_t>& c, std::uint64_t to) {
    if (to > c.load(std::memory_order_relaxed)) c.store(to, std::memory_order_relaxed);
}
#endif

// The trace window's spans on the block path (spec/08-profiling.md): compiled out of every build but a profile one.
inline void traceBegin([[maybe_unused]] ScriptHost& h, [[maybe_unused]] std::uint32_t label) {
#ifdef RETROPLUG_PROFILE
    h.spanBegin(label);
#endif
}
inline void traceEnd([[maybe_unused]] ScriptHost& h) {
#ifdef RETROPLUG_PROFILE
    h.spanEnd();
#endif
}

enum class Side : std::uint8_t { Input, Output, Param };

// One port or param of a layout, by the index of its view on its side of rp_process (inputs, outputs or params).
struct Port {
    std::string   id;
    PortKind      kind = PortKind::Events;
    Side          side = Side::Input;
    std::uint32_t index = 0;
};

// One port's JS state while its layout is the instance's: made on the block thread the first time it is needed,
// refilled at most once a block, freed there with the layout. Its native parts are sized on the control thread, so
// the block thread only ever writes into them.
struct PortJs {
    JSAtom        atom = JS_ATOM_NULL;     // the id, so a lookup allocates nothing
    JSValue       value = JS_UNDEFINED;    // what JS is handed: a Message[], a ControlStream or a Float32Array[]
    JSValue       pool = JS_UNDEFINED;     // messages and changes: every element made, so a shorter block frees none
    JSValue       items = JS_UNDEFINED;    // control: the stream's changes array
    JSValue       payload = JS_UNDEFINED;  // messages: the JS-owned ArrayBuffer the block's payloads are copied into
    std::uint64_t stamp = 0;               // the block it was filled for
    // messages and changes: the elements in pool (JS never sees the pool, so its first `pooled` slots are its own and
    // the rest are read nowhere).
    std::uint32_t pooled = 0;
    // audio: the instance's own copy of each channel, a JS-owned ArrayBuffer of bufferFrames floats (undefined until
    // the port first runs with that channel), which a block's samples are copied into before process (an input) or out
    // of after it (an output); value holds a view of the first viewFrames of each of the first viewCount. Never read
    // back from value, which JS may change. kMaxChannels long.
    std::vector<JSValue> buffers;
    std::uint32_t bufferFrames = 0;
    std::uint32_t viewFrames = 0;
    std::uint32_t viewCount = 0;
};

// The layout an instance runs with: its ports by id, and their JS state.
struct Layout {
    std::string         json;
    std::vector<Port>   ports;  // inputs and outputs in layout order, then the params
    std::vector<PortJs> js;     // one per port
    bool                bound = false;  // its atoms made (block thread)
};

// What prepare_settings prepares and commit_settings swaps: the settings the definition accepted, as JSON, and, from a
// type that reshapes, the layout they give. An instance's current settings never hold a layout: a commit moves it
// into the instance, or hands it back unused, with the replaced settings, to the control thread.
struct Settings {
    std::string             json;
    std::unique_ptr<Layout> layout;
};

std::unique_ptr<Layout> buildLayout(const std::string& json, std::string& why) {
    auto parsed = graph::parseLayout(json);
    if (!parsed) {
        why = std::string("the definition's layout does not parse: ") + parsed.error().what();
        return nullptr;
    }
    auto l = std::make_unique<Layout>();
    l->json = json;
    std::uint32_t in = 0, out = 0;
    for (const auto& p : parsed->ports) {
        const bool output = p.dir == graph::PortDir::Out;
        l->ports.push_back({p.id, p.kind, output ? Side::Output : Side::Input, output ? out++ : in++});
    }
    for (std::size_t k = 0; k < parsed->params.size(); ++k)
        l->ports.push_back({parsed->params[k].id, PortKind::Control, Side::Param, static_cast<std::uint32_t>(k)});
    l->js.resize(l->ports.size());
    for (std::size_t i = 0; i < l->ports.size(); ++i)
        if (l->ports[i].kind == PortKind::Audio) l->js[i].buffers.assign(graph::kMaxChannels, JS_UNDEFINED);
    return l;
}

}  // namespace

struct ScriptHost::Type {
    rp_node_type  abi{};
    ScriptHost*   host = nullptr;
    Pack*         pack = nullptr;  // whose runtimes its definitions, and so its instances, live in
    std::string   id;
    std::string   descriptor;
    bool          reshapes = false;
    bool          faultOnThrow = false;
    std::uint32_t spanLabel = DSP_SPAN_SCRIPT_NODE;  // its nodes' process spans' label (DspSpanLabel), set by the load
    JSValue       control = JS_UNDEFINED;  // the definition, in its pack's control context
    JSValue       audio = JS_UNDEFINED;    // and in its audio context
};

// A pack (docs/modular-graph-phase-c1-design.md section 4): an id, its two runtimes and their contexts, loaded with its
// bundle, and the types that bundle registered, in a package of its own. Everything the block thread holds per audio
// runtime is the pack's, made with it: the Block class and its frozen prototype, the atoms it names properties by, the
// address of Object.prototype. Made and loaded on the control thread; the block thread reaches it only through an
// instance of one of its types.
struct ScriptHost::Pack {
    // The runtimes are raw pointers, freed by ScriptHost::freeRuntimes (which nulls them) before a pack goes.
    ~Pack() { assert(!audioRt && !controlRt && !audio && !control && "a pack is freed through freeRuntimes"); }

    // A runtime's allocator opaque: the counters it adds to, and its own live heap and the limit an allocation may not
    // take it past (design section 6). Written by whichever thread runs the runtime (for the audio one, the block
    // thread, or the control thread while it loads or no audio thread runs).
    struct Heap {
        Counters*   counters = nullptr;
        std::size_t live = 0;      // usable bytes
        std::size_t limit = kDefaultMemoryLimit;
        bool        refused = false;  // an allocation was refused since this was last read (Glue::refusedSince)
    };

    ScriptHost*   host = nullptr;   // the host holding it, whose trace a Block method's marshal spans go to
    std::string   id;
    std::uint32_t generation = 1;   // which of its id's loads made it (generations_)
    std::string   manifest;         // its package's: {"id":"<id>"}
    JSRuntime*    controlRt = nullptr;
    JSContext*    control = nullptr;
    JSRuntime*    audioRt = nullptr;
    JSContext*    audio = nullptr;
    JSClassID     blockClass = 0;   // the Block object's class, in the audio runtime
    std::array<JSAtom, kAtomCount> atoms{};  // the names the block thread reads and writes, in the audio runtime
    const void*   objectProto = nullptr;     // the audio context's Object.prototype, by address
    std::vector<std::unique_ptr<Type>> types;
    std::vector<const rp_node_type*>   typeList;
    rp_package    package{};
    // Its load, or a release of one of its instances, since its last collect (whoever released it).
    bool          garbage = false;
    // Its audio runtime's heap, adding to the host's counters once it joins (to a load's own before), and its control
    // runtime's, held to the same limit with a refusal noted the same way, so a load or a settings check it fails says
    // so, adding to controlCounts, which nothing reads: only the audio side's counts are the host's to give.
    Heap          heap;
    Heap          controlHeap;
    Counters      controlCounts;
    // Whose promise jobs are pending while a checkpoint has left some (the block thread's): the instance they are
    // charged to until the queue empties, or, `jobsOrphaned`, no node, the one they were charged to having been
    // released (Glue::chargeJobs).
    Instance*     jobsOwner = nullptr;
    bool          jobsOrphaned = false;
    // The control thread's. The records of its types' instances alive (made by create, deleted by deleteRecord), and
    // how it stands (PackStanding): a generation a reload replaced is retired, one that never stood is discarded, and
    // either is freed once its records are 0 (freeIdle).
    std::uint32_t records = 0;
    PackStanding  standing = PackStanding::Current;
    // Its live heap as it joined, which the shared counters never had (a loaded pack's load is counted apart; the
    // built-in pack's is not, but it is freed only with the host): what they have of its live heap is the rest, which a
    // profile build takes off them once it is freed (settleFreedLive).
    std::size_t   joinedLive = 0;
};

// One script node instance, behind its shim. Allocated by create and deleted on the control thread; its JS state is
// made and freed on the block thread.
struct ScriptHost::Instance {
    enum class State : std::uint8_t { Pending, Live, Failed, Released };

    Type*                   type = nullptr;
    std::string             createdLayout;  // the layout it was created with, which prepare compares against
    // Written by create and activate on the control thread, before a plan holding the instance is published or while
    // quiescent, and read by the block thread after.
    double                  sampleRate = 48000.0;
    std::uint32_t           maxFrames = 1024;
    Instance*               backlogNext = nullptr;  // the control thread's: the next record in the release backlog
    // The block thread's.
    State                   state = State::Pending;
    bool                    faultNext = false;  // a hook threw under faultOnThrow, or expired twice in a row: fault next block
    std::uint8_t            expiries = 0;       // its calls the deadline stopped in a row (expiryFaults)
    std::uint8_t            cutShorts = 0;      // checkpoints in a row that cut its promise jobs short (chargeJobs)
    Settings*               settings = nullptr;
    std::unique_ptr<Layout> layout;
    JSValue                 object = JS_UNDEFINED;  // the NodeInstance its definition's create returned
    JSValue                 process = JS_UNDEFINED, onSettings = JS_UNDEFINED, portsChanged = JS_UNDEFINED,
                            activate = JS_UNDEFINED;
    JSValue                 block = JS_UNDEFINED;   // the one Block object it is handed, every block
    JSValue                 transport = JS_UNDEFINED;
    const rp_process*       running = nullptr;      // during its process call
    std::uint64_t           blockStamp = 0;

    ~Instance() { delete settings; }
};

// Everything the ABI tables, the Block object's methods and the runtimes' allocator reach the host through. What an
// instance's JS lives in is reached through its type's pack, and what a Block method runs in through its context's.
struct ScriptHost::Glue {
    // The rp_node a script node is to the planner: a pointer to its record, which outlives it until its release.
    struct Shim : rp_node {
        Instance* in = nullptr;
    };

    static Type&       typeOf(const rp_node_type* t) { return *static_cast<Type*>(t->ctx); }
    static Instance&   instanceOf(rp_node* n) { return *static_cast<Shim*>(n)->in; }
    static Pack&       packOf(Instance& in) { return *in.type->pack; }
    static Pack&       packOf(JSContext* audio) { return *static_cast<Pack*>(JS_GetContextOpaque(audio)); }
    static JSAtom      atom(const Pack& p, Atom a) { return p.atoms[a]; }

    // ---- the call deadline ---------------------------------------------------------------------------------------
    // While one lives, what the block thread runs in an audio runtime may use the deadline's worth of the thread's CPU
    // time, and the interrupt handler stops it after: QuickJS throws an uncatchable error, which the call returns as a
    // throw. Arming one reads no clock: the handler takes the baseline at its first poll inside the call and compares at
    // the later ones, so a call too short to reach a poll costs nothing (and one that does goes uncounted for up to
    // 10000 polls, far under any deadline). One made inside another's call is part of that call, and its budget.
    // Arming one is where a call into `pack` begins, so it clears the pack's refusal note too: a call is charged only
    // what its pack refuses from then on, never one made where no checkpoint looks (a release freeing a node's JS, a
    // quiescent collection, the text the host makes of a checkpoint's last job's throw).
    struct Deadline {
        ScriptHost& h;
        bool        nested;
        Deadline(ScriptHost& host, Pack& pack) : h(host), nested(host.armed_) {
            if (nested) return;
            h.armed_ = true;
            h.cpuStart_ = -1;
            h.interrupted_ = false;
            pack.heap.refused = false;
        }
        ~Deadline() {
            if (!nested) h.armed_ = false;
        }
        Deadline(const Deadline&) = delete;
        Deadline& operator=(const Deadline&) = delete;
    };

    // QuickJS asks once every 10000 of its polls (at each call and each jump back in a loop) whether to stop: the
    // block thread's clock in an audio runtime that has joined, the control thread's everywhere else
    // (controlInterrupt).
    static int interrupt(JSRuntime*, void* opaque) {
        auto& h = *static_cast<ScriptHost*>(opaque);
        return expired(h, h.armed_, h.cpuStart_, h.interrupted_) ? 1 : 0;
    }
    // Whether the armed call one thread's clock times has used up the deadline: the baseline is taken at the call's
    // first poll, and `interrupted` set once it has.
    static bool expired(const ScriptHost& h, bool armed, std::int64_t& cpuStart, bool& interrupted) {
        if (!armed) return false;
        const std::int64_t now = threadCpuNs();
        if (cpuStart < 0) {
            cpuStart = now;
            return false;
        }
        if (now - cpuStart < h.callDeadlineNs_.load(std::memory_order_relaxed)) return false;
        interrupted = true;
        return true;
    }

    // ---- the control thread's deadline ---------------------------------------------------------------------------
    // The same length, of the control thread's own CPU time, on a clock of its own, held to every call it makes into a
    // pack's JS: a load (the chunks in both runtimes, the jobs they queue, the definitions read) and a settings check.
    // Never the block thread's clock: the block thread may be inside a call into another pack meanwhile.
    struct ControlDeadline {
        ScriptHost& h;
        bool        nested;
        explicit ControlDeadline(ScriptHost& host) : h(host), nested(host.controlArmed_) {
            if (nested) return;
            h.controlArmed_ = true;
            h.controlCpuStart_ = -1;
            h.controlInterrupted_ = false;
        }
        ~ControlDeadline() {
            if (!nested) h.controlArmed_ = false;
        }
        ControlDeadline(const ControlDeadline&) = delete;
        ControlDeadline& operator=(const ControlDeadline&) = delete;
    };
    // A pack's control runtime's handler for its life, and its audio runtime's until join.
    static int controlInterrupt(JSRuntime*, void* opaque) {
        auto& h = *static_cast<ScriptHost*>(opaque);
        return expired(h, h.controlArmed_, h.controlCpuStart_, h.controlInterrupted_) ? 1 : 0;
    }
    // What a call the control thread's deadline stopped says, `what` the call.
    static std::string stopped(const ScriptHost& h, const std::string& what) {
        return what + " ran past the " + std::to_string(h.callDeadlineNs_.load(std::memory_order_relaxed) / 1000000) +
               " ms call deadline (CPU time), and was stopped";
    }

    // After one of a node's calls: whether the deadline stopping it (`stopped`: the call, or a promise job charged to
    // the node at its checkpoint) faults the node. Under faultOnThrow the first expiry does; without, only a second in
    // a row (design section 4.5: one throw and the next block runs again), and a call that ran to its end, its jobs
    // with it, clears the count. So a node that hangs, or whose reaction does, still faults within two deadlines, and
    // one long call costs a block rather than the session, which a fault would: it outlives a reshape.
    static bool expiryFaults(Instance& in, bool stopped) {
        if (!stopped) {
            in.expiries = 0;
            return false;
        }
        if (in.type->faultOnThrow) return true;
        if (in.expiries < 2) ++in.expiries;
        return in.expiries >= 2;
    }

    // ---- the runtimes' allocator: the platform's, counted and held to each pack's limit -------------------------
    // Each of a pack's two runtimes has its own opaque (Pack::Heap), which keeps its live heap in usable bytes, as
    // QuickJS's own accounting (jsUsable) reads it, in every build: an allocation that would take it past the pack's
    // limit is refused (null, which QuickJS throws as out of memory in the call that asked) and the refusal noted, for
    // the call's checkpoint to fault its node (the audio runtime), or for the load or the settings check it failed to
    // say so (the control one). The limit is the host's to enforce rather than QuickJS's (JS_SetMemoryLimit), which
    // checks its own before calling the allocator and so would refuse without the host ever seeing it.
#ifdef RETROPLUG_PROFILE
    // A profile build keeps the live heap of what a heap counts into summed, and its peak: every pack's audio runtime
    // in the host's counters.
    static void grew(Counters& c, std::int64_t by) {
        const std::int64_t live = c.liveBytes.load(std::memory_order_relaxed) + by;
        c.liveBytes.store(live, std::memory_order_relaxed);
        if (live > static_cast<std::int64_t>(c.peakBytes.load(std::memory_order_relaxed)))
            c.peakBytes.store(static_cast<std::uint64_t>(live), std::memory_order_relaxed);
    }
#endif
    // Whether `heap` has room for `more` bytes; when not, the refusal is noted.
    static bool admits(Pack::Heap& heap, std::size_t more) {
        if (more <= heap.limit && heap.live <= heap.limit - more) return true;
        heap.refused = true;
        return false;
    }
    static void allocated(Pack::Heap& heap, void* p, std::size_t size) {
        const std::size_t usable = usableSize(p);
        heap.live += usable;
        bump(heap.counters->allocs);
        bump(heap.counters->bytes, size);
#ifdef RETROPLUG_PROFILE
        grew(*heap.counters, static_cast<std::int64_t>(usable));
#endif
    }
    static void* jsCalloc(void* opaque, std::size_t count, std::size_t size) {
        auto& heap = *static_cast<Pack::Heap*>(opaque);
        if (!admits(heap, count * size)) return nullptr;  // QuickJS has checked count * size for overflow
        void* p = std::calloc(count, size);
        if (p) allocated(heap, p, count * size);
        return p;
    }
    static void* jsMalloc(void* opaque, std::size_t size) {
        auto& heap = *static_cast<Pack::Heap*>(opaque);
        if (!admits(heap, size)) return nullptr;
        void* p = std::malloc(size);
        if (p) allocated(heap, p, size);
        return p;
    }
    static void jsFree(void* opaque, void* p) {
        if (p) {
            auto& heap = *static_cast<Pack::Heap*>(opaque);
            const std::size_t usable = usableSize(p);
            heap.live -= usable;
            bump(heap.counters->frees);
#ifdef RETROPLUG_PROFILE
            grew(*heap.counters, -static_cast<std::int64_t>(usable));
#endif
        }
        std::free(p);
    }
    static void* jsRealloc(void* opaque, void* p, std::size_t size) {
        auto& heap = *static_cast<Pack::Heap*>(opaque);
        const std::size_t before = p ? usableSize(p) : 0;
        // p is left as it was, as a failed realloc does.
        if (size > before && !admits(heap, size - before)) return nullptr;
        void* q = std::realloc(p, size);
        if (q || size == 0) {
            const std::size_t after = q ? usableSize(q) : 0;
            heap.live = heap.live - before + after;
            bump(heap.counters->reallocs);
            if (size > before) bump(heap.counters->bytes, size - before);
#ifdef RETROPLUG_PROFILE
            grew(*heap.counters, static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before));
#endif
        }
        return q;
    }
    static std::size_t jsUsable(const void* p) { return usableSize(p); }

    // ---- the control side: what a definition says of a settings object --------------------------------------
    // The settings the definition's parseSettings accepted and the layout they give, both as JSON; or false, with
    // its reason in err.
    static bool check(Type& t, const rp_settings* s, std::string& settings, std::string& layout, rp_error* err) {
        if (s && s->encoding != RP_SETTINGS_JSON) {
            copyError(err, "settings encoding " + std::to_string(s->encoding) + " is not supported; a script node reads JSON");
            return false;
        }
        Pack& pack = *t.pack;
        ScriptHost& h = *t.host;
        JSContext* ctx = pack.control;
        JS_UpdateStackTop(pack.controlRt);  // QuickJS guards the stack from where it was last entered: re-anchor here
        // Its parseSettings, its layout and the jobs they queue, as one call: one the deadline stops, or that fails on
        // an allocation the pack's memory limit refused, refuses the settings, saying so, so the planner refuses the
        // document and the running plan stands; and so does one whose jobs do not settle in kJobsPerCheckpoint.
        const ControlDeadline call(h);
        pack.controlHeap.refused = false;  // what this call is refused, from here on
        const std::string text = s && s->data && s->size ? std::string(s->data, s->size) : std::string("{}");
        JSValue raw = JS_ParseJSON(ctx, text.c_str(), text.size(), "<settings>");
        JSValue parsed = JS_IsException(raw) ? JS_EXCEPTION : callMethod(ctx, t.control, "parseSettings", 1, &raw);
        JSValue lay = JS_IsException(parsed) ? JS_EXCEPTION : callMethod(ctx, t.control, "layout", 1, &parsed);
        bool ok = !JS_IsException(lay) && stringify(ctx, parsed, settings) && stringify(ctx, lay, layout);
        const char* where = JS_IsException(parsed) ? "parseSettings"
                            : JS_IsException(lay)    ? "layout"
                                                     : "settings check";
        if (!ok && h.controlInterrupted_) {
            clearException(ctx);
            copyError(err, stopped(h, t.id + "'s " + where));
        } else if (!ok && pack.controlHeap.refused) {
            // The error is QuickJS's out of memory (null when it could not make even that), or the definition's own
            // after it caught that one: the limit is said either way.
            copyError(err, t.id + "'s " + where + " reached its pack's " + limitText(pack.controlHeap.limit) + ": " +
                               exceptionText(ctx, true));
        } else if (!ok) {
            copyError(err, exceptionText(ctx, true));
        }
        JS_FreeValue(ctx, lay);
        JS_FreeValue(ctx, parsed);
        JS_FreeValue(ctx, raw);
        // QuickJS cannot drop a pending job, so what does not settle stays queued in the pack's control runtime, where
        // the pack's next settings check runs it with its own: a chain that never settles refuses every check of the
        // pack, until a reload frees that runtime.
        const bool settled = drainControlJobs(pack);
        if (ok && h.controlInterrupted_) {
            ok = false;
            copyError(err, stopped(h, t.id + "'s promise jobs"));
        } else if (ok && !settled) {
            ok = false;
            copyError(err, t.id + "'s settings check: its pack's promise jobs did not settle in the " +
                               std::to_string(kJobsPerCheckpoint) + " a settings check runs");
        }
        return ok;
    }

    // ---- the type ----------------------------------------------------------------------------------------------
    static const char* descriptor(const rp_node_type* t) { return typeOf(t).descriptor.c_str(); }

    static std::size_t layoutFor(const rp_node_type* t, const rp_settings* s, char* out, std::size_t capacity,
                                 rp_error* err) {
        std::string settings, layout;
        if (!check(typeOf(t), s, settings, layout, err)) return 0;
        if (out && layout.size() < capacity) {
            std::memcpy(out, layout.data(), layout.size());
            out[layout.size()] = '\0';
        }
        return layout.size();
    }

    static bool queryMemory(const rp_node_type*, const rp_settings*, rp_memory_req* out, rp_error*) {
        out->size = sizeof(Shim);
        out->align = alignof(Shim);
        return true;
    }

    // Only the shim and its record: the JS instance waits for the block thread.
    static rp_node* create(const rp_node_type* t, const rp_host*, void* memory, const rp_settings* s, rp_error* err) {
        Type& type = typeOf(t);
        std::string settings, layout, why;
        if (!check(type, s, settings, layout, err)) return nullptr;
        auto in = std::make_unique<Instance>();
        in->layout = buildLayout(layout, why);
        if (!in->layout) {
            copyError(err, why);
            return nullptr;
        }
        in->type = &type;
        in->createdLayout = layout;
        in->settings = new Settings{std::move(settings), nullptr};
        auto* shim = new (memory) Shim{};
        shim->vt = &kVtable;
        shim->in = in.release();
        ++type.pack->records;  // the pack is freed only once every record of its types is back (freeIdle)
        return shim;
    }

    static void destroy(rp_node* n) {
        auto* shim = static_cast<Shim*>(n);
        shim->in->type->host->retire(shim->in);
        shim->~Shim();
    }

    // ---- the instance --------------------------------------------------------------------------------------------
    // The control thread, on a fresh shim (whose JS instance is made later, with these values, before a plan holding
    // it is published), or on a live one while quiescent (GraphHost::reactivate, which refuses while an audio thread
    // runs blocks), when its activate runs at once: this thread is the block thread then.
    static bool activate(rp_node* n, double sampleRate, std::uint32_t maxFrames, rp_error* err) {
        Instance& in = instanceOf(n);
        in.sampleRate = sampleRate;
        in.maxFrames = maxFrames;
        if (in.state != Instance::State::Live) return true;
        ScriptHost& h = *in.type->host;
        assert(!h.audioOwns_.load(std::memory_order_acquire) && "a live script node is re-activated only while quiescent");
        if (callActivate(h, in)) return true;
        copyError(err, "activate refused these settings");
        return false;
    }

    static void deactivate(rp_node*) {}

    static void portsChanged(rp_node* n, const std::uint8_t* connected, std::uint32_t count) {
        Instance& in = instanceOf(n);
        ScriptHost& h = *in.type->host;
        // The create a fresh instance makes first, the hook, and the jobs they queue.
        const Deadline call(h, packOf(in));
        const bool fresh = in.state == Instance::State::Pending;
        if (!ensureLive(h, in)) return;
        JSContext* ctx = packOf(in).audio;
        if (!JS_IsFunction(ctx, in.portsChanged)) {
            if (fresh) hookDone(h, in, false, "create");  // what its create queued, at a checkpoint of its own
            return;
        }
        JSValue mask = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < count && !JS_IsException(mask); ++i)
            if (JS_DefinePropertyValueUint32(ctx, mask, i, JS_NewBool(ctx, connected[i] != 0), kDefine) < 0) {
                JS_FreeValue(ctx, mask);  // out of memory: a throw in portsChanged
                mask = JS_EXCEPTION;
            }
        JSValue result = JS_IsException(mask) ? JS_EXCEPTION : JS_Call(ctx, in.portsChanged, in.object, 1, &mask);
        hookDone(h, in, JS_IsException(result), "portsChanged");
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, mask);
    }

    static rp_status process(rp_node* n, const rp_process* block) {
        Instance& in = instanceOf(n);
        ScriptHost& h = *in.type->host;
        // The whole block, not just the call: the create a fresh instance makes first, the block path (a node may hang
        // a setter on what it fills in), the call, and the jobs they queue.
        const Deadline call(h, packOf(in));
        if (!ensureLive(h, in) || in.faultNext) return RP_FAULT;
        const Pack& pack = packOf(in);
        JSContext* ctx = pack.audio;
        traceBegin(h, in.type->spanLabel);
        traceBegin(h, DSP_SPAN_MARSHAL);
        ++in.blockStamp;
        // Every audio port's copies first, the inputs' samples in them: a node may read views it kept from a block
        // before, so the copy cannot wait for it to ask.
        if (!readyAudio(pack, ctx, in, *block)) return cannotRun(h, in, *block);
        in.running = block;
        if (!fillBlock(pack, ctx, in, *block)) {
            in.running = nullptr;
            return cannotRun(h, in, *block);
        }
        traceEnd(h);
        jsCallBegin(h);
        JSValue result = JS_Call(ctx, in.process, in.object, 1, &in.block);
        jsCallEnd(h);
        in.running = nullptr;
        const bool threw = JS_IsException(result), stopped = h.interrupted_;
        if (threw) recordThrow(h, in, "process");  // first: the copy out may meet a detached buffer, which throws too
        copyAudioOut(ctx, in, *block);             // what it wrote up to a throw stands, as its sends do
        bool said = false;                         // it answered "fault"
        if (!threw && JS_IsString(result)) {
            const JSAtom answer = JS_ValueToAtom(ctx, result);
            said = answer == atom(pack, kFault);
            JS_FreeAtom(ctx, answer);
        }
        JS_FreeValue(ctx, result);
        // Its jobs after the copy out, so the block's audio is what process wrote, inside its process span (a profile
        // build), after its js-call span.
        const Settled jobs = checkpoint(h, in, threw, "process");
        const bool expired = expiryFaults(in, stopped || jobs.stopped);
        processSpanEnd(h);
        return (threw && in.type->faultOnThrow) || expired || said || jobs.fault ? RP_FAULT : RP_OK;
    }

    // A block whose marshalling failed before its process could be called, with the exception pending: JS out of
    // memory, a write onto what the node holds refused (readyAudio's onto its views, fillBlock's onto the Block and its
    // transport), or the deadline stopping a setter it hung there, whose uncatchable error is the throw kept. Counted as
    // a throw in process, its audio outputs silent, and faulted as such a throw would fault it. What the setter queued
    // runs at the checkpoint after it.
    static rp_status cannotRun(ScriptHost& h, Instance& in, const rp_process& block) {
        const bool stopped = h.interrupted_;
        recordThrow(h, in, "process");
        silenceAudio(in, block);
        traceEnd(h);  // marshal
        const Settled jobs = checkpoint(h, in, true, "process");
        const bool expired = expiryFaults(in, stopped || jobs.stopped);
        processSpanEnd(h);
        return in.type->faultOnThrow || expired || jobs.fault ? RP_FAULT : RP_OK;
    }

    // The block's length and transport, onto the Block object and its transport, whose fields createJs defined, so each
    // write finds its own property (one the node deleted is the exception, kDefine). False, with the exception pending,
    // when a write fails: a setter the node hung on one threw, or it froze one.
    static bool fillBlock(const Pack& p, JSContext* ctx, Instance& in, const rp_process& block) {
        const rp_transport& t = *block.transport;
        const bool playing = (t.flags & RP_TRANSPORT_PLAYING) != 0;
        return JS_SetProperty(ctx, in.block, atom(p, kFrames), JS_NewUint32(ctx, block.frames)) >= 0 &&
               JS_SetProperty(ctx, in.transport, atom(p, kSampleRate), JS_NewFloat64(ctx, t.sample_rate)) >= 0 &&
               JS_SetProperty(ctx, in.transport, atom(p, kTempo), JS_NewFloat64(ctx, t.tempo)) >= 0 &&
               JS_SetProperty(ctx, in.transport, atom(p, kPpq), JS_NewFloat64(ctx, t.ppq)) >= 0 &&
               JS_SetProperty(ctx, in.transport, atom(p, kPlaying), JS_NewBool(ctx, playing)) >= 0;
    }

    // A process call's js-call span (a profile build), closed as the call returns whatever happened in JS. Every span
    // the host opens inside it (an input's marshal, as JS reads one) closes before the host returns to JS, and JS opens
    // none, so js-call is the innermost open span here.
    static void jsCallBegin(ScriptHost& h) { traceBegin(h, DSP_SPAN_JSCALL); }
    static void jsCallEnd(ScriptHost& h) { traceEnd(h); }

    // The end of a process call's span, its type's (a profile build).
    static void processSpanEnd(ScriptHost& h) { traceEnd(h); }

    static rp_prepare_result prepare(rp_node* n, const rp_settings* s, void** prepared, rp_error* err) {
        Instance& in = instanceOf(n);
        Type& t = *in.type;
        std::string settings, layout, why;
        if (!check(t, s, settings, layout, err)) return RP_PREPARE_INVALID;
        if (!t.reshapes && layout != in.createdLayout) return RP_PREPARE_REBUILD;
        auto next = std::make_unique<Settings>();
        next->json = std::move(settings);
        if (t.reshapes) {
            next->layout = buildLayout(layout, why);
            if (!next->layout) {
                copyError(err, why);
                return RP_PREPARE_INVALID;
            }
        }
        *prepared = next.release();
        return t.reshapes ? RP_PREPARE_RESHAPE : RP_PREPARE_SWAP;
    }

    // The block thread, at the swap. A reshape's layout replaces the instance's when it differs (and is handed back
    // unused when it does not); either way what goes back in *old is the replaced settings, holding the layout that
    // is no longer the instance's, its JS state freed.
    static void commit(rp_node* n, void* prepared, void** old) {
        Instance& in = instanceOf(n);
        ScriptHost& h = *in.type->host;
        auto* next = static_cast<Settings*>(prepared);
        Settings* prev = in.settings;
        // The create a fresh instance makes first, onSettings, and the jobs they queue.
        const Deadline call(h, packOf(in));
        const bool fresh = in.state == Instance::State::Pending;
        const bool live = ensureLive(h, in);  // a fresh instance is made from the settings it was created with first
        JSContext* ctx = packOf(in).audio;
        bool reshaped = false;
        if (next->layout) {
            if (next->layout->json != in.layout->json) {
                freeLayoutJs(ctx, *in.layout);
                prev->layout = std::move(in.layout);
                in.layout = std::move(next->layout);
                bindLayout(ctx, *in.layout);
                reshaped = true;
            } else {
                prev->layout = std::move(next->layout);
            }
        }
        in.settings = next;
        *old = prev;
        if (!live) return;
        if (!JS_IsFunction(ctx, in.onSettings)) {
            // What its create queued, or what its new layout's atoms were refused and freeing the old one's JS queued
            // (a FinalizationRegistry callback whose target that was), at a checkpoint of its own: the commit's,
            // charged to it.
            if (fresh || reshaped) hookDone(h, in, false, fresh ? "create" : "a reshape");
            return;
        }
        JSValue settings = JS_ParseJSON(ctx, next->json.c_str(), next->json.size(), "<settings>");
        JSValue result = JS_IsException(settings) ? JS_EXCEPTION : JS_Call(ctx, in.onSettings, in.object, 1, &settings);
        hookDone(h, in, JS_IsException(result), "onSettings");
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, settings);
    }

    // The control thread: what a commit handed back, or a prepared object never committed. No JS state in either.
    static void release(rp_node*, void* old) { delete static_cast<Settings*>(old); }

    static const void* getInterface(rp_node*, const char*) { return nullptr; }

    static constexpr rp_node_vtable kVtable{&activate, &deactivate, &portsChanged, &process,
                                            &prepare,  &commit,     &release,      &getInterface};

    // ---- the block thread: an instance's JS ------------------------------------------------------------------------
    static bool ensureLive(ScriptHost& h, Instance& in) {
        if (in.state == Instance::State::Pending) createJs(h, in);
        else JS_UpdateStackTop(packOf(in).audioRt);
        return in.state == Instance::State::Live;
    }

    // The definition's create, with the instance's current settings: it fails (and the node faults) when create
    // throws or returns no process, when reading its process or a hook throws (a getter or a Proxy trap the node
    // defined for one, or a prototype chain too long or cycling through a Proxy, as method says), or when JS runs out
    // of memory making what the host hands it. What the host makes here holds every field it will write as its own
    // (kDefine). It runs inside the deadline of the call that reached the pending instance (ensureLive's caller), whose
    // checkpoint runs the jobs it queues.
    static void createJs(ScriptHost& h, Instance& in) {
        const Pack& pack = packOf(in);
        JSContext* ctx = pack.audio;
        JS_UpdateStackTop(pack.audioRt);
        bindLayout(ctx, *in.layout);
        // The settings as the control side's parseSettings returned them, in their JSON form (the class comment).
        JSValue settings = JS_ParseJSON(ctx, in.settings->json.c_str(), in.settings->json.size(), "<settings>");
        JSValue cc = JS_IsException(settings) ? JS_EXCEPTION : JS_NewObject(ctx);
        if (JS_IsException(cc)) {
            JS_FreeValue(ctx, settings);
        } else if (JS_DefinePropertyValueStr(ctx, cc, "settings", settings, kDefine) < 0 ||  // each takes its value
                   JS_DefinePropertyValueStr(ctx, cc, "sampleRate", JS_NewFloat64(ctx, in.sampleRate), kDefine) < 0 ||
                   JS_DefinePropertyValueStr(ctx, cc, "maxFrames", JS_NewUint32(ctx, in.maxFrames), kDefine) < 0) {
            JS_FreeValue(ctx, cc);
            cc = JS_EXCEPTION;
        }
        JSValue object = JS_IsException(cc) ? JS_EXCEPTION : callMethod(ctx, in.type->audio, "create", 1, &cc);
        JS_FreeValue(ctx, cc);
        if (JS_IsException(object)) return failCreate(h, in);
        JSValue process = method(pack, ctx, object, kProcess);
        if (!JS_IsFunction(ctx, process)) {
            if (!JS_IsException(process)) JS_ThrowTypeError(ctx, "create returned no process function");
            JS_FreeValue(ctx, process);
            JS_FreeValue(ctx, object);
            return failCreate(h, in);
        }
        // The hooks, read as process is: one absent is undefined.
        const Atom names[3] = {kOnSettings, kPortsChanged, kActivate};
        JSValue hooks[3] = {JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            JSValue fn = method(pack, ctx, object, names[k]);
            ok = !JS_IsException(fn);
            if (JS_IsFunction(ctx, fn)) hooks[k] = fn;
            else JS_FreeValue(ctx, fn);
        }
        // The Block object and its transport, with every field the block path fills (fillBlock), so no write there
        // reaches the Block class's prototype or Object.prototype unless the node deletes the field (kDefine).
        JSValue block = ok ? JS_NewObjectClass(ctx, pack.blockClass) : JS_EXCEPTION;
        JSValue transport = JS_IsException(block) ? JS_EXCEPTION : JS_NewObject(ctx);
        ok = !JS_IsException(transport) && defineFields(pack, ctx, transport, {kSampleRate, kTempo, kPpq, kPlaying}) &&
             JS_DefinePropertyValue(ctx, block, atom(pack, kTransport), JS_DupValue(ctx, transport), kDefine) >= 0 &&
             defineFields(pack, ctx, block, {kFrames});
        if (!ok) {
            for (JSValue v : {transport, block, hooks[0], hooks[1], hooks[2], process, object}) JS_FreeValue(ctx, v);
            return failCreate(h, in);
        }
        JS_SetOpaque(block, &in);
        in.object = object;
        in.process = process;
        in.onSettings = hooks[0];
        in.portsChanged = hooks[1];
        in.activate = hooks[2];
        in.block = block;
        in.transport = transport;
        in.state = Instance::State::Live;
        h.live_.fetch_add(1, std::memory_order_relaxed);
    }

    // A create that failed, with its exception pending: counted, what it queued run at a checkpoint (charged to it, as
    // nothing else is to be), and the node faults at its first block.
    static void failCreate(ScriptHost& h, Instance& in) {
        recordThrow(h, in, "create");
        in.state = Instance::State::Failed;
        checkpoint(h, in, true, "create");
    }

    // Each of `fields` onto `obj`, an object the host made and JS has not yet seen, as its own data property, undefined
    // until the block path fills it. False, with the exception pending, when JS is out of memory.
    static bool defineFields(const Pack& p, JSContext* ctx, JSValueConst obj, std::initializer_list<Atom> fields) {
        for (const Atom a : fields)
            if (JS_DefinePropertyValue(ctx, obj, atom(p, a), JS_UNDEFINED, kDefine) < 0) return false;
        return true;
    }

    // A method of the object create returned, as JS would find it there or on a prototype it inherits from (a class
    // keeps its methods on its prototype), short of Object.prototype: what only Object.prototype holds is no method of
    // the node's, and reading it there would run, or adopt as the node's, whatever another node (or the bundle) hung on
    // the realm's one. A getter found on the way is the node's own, and runs with the object as `this`. A Proxy on the
    // way answers for itself and all past it, as JS reads one: the read there is a plain get of the object, which finds
    // nothing on the plain objects before the Proxy (the walk asked each) and hands it the object as the receiver, so
    // its get trap answers, or with none its target and the target's prototypes, up to Object.prototype. A wrapper or a
    // test double whose get trap answers the methods works, and what a Proxy the node made answers is the node's. The
    // walk and that get run no JS of their own, so the call deadline cannot stop them, and each is bounded otherwise:
    // the get by QuickJS's stack check (a chain that cycles back through a Proxy, which setPrototypeOf's cycle check
    // cannot see past, recurses into it until that throws a RangeError), and the walk by kMethodHops (plain objects
    // never form a cycle, and a chain of more than that is refused, with a RangeError). Undefined when nothing on the
    // way has it; JS_EXCEPTION, with it pending, when a getter or a Proxy's trap throws or the read is refused.
    static JSValue method(const Pack& p, JSContext* ctx, JSValueConst object, Atom name) {
        if (!JS_IsObject(object)) return JS_UNDEFINED;
        JSValue at = JS_DupValue(ctx, object);
        for (int hops = 0; JS_IsObject(at) && JS_VALUE_GET_PTR(at) != p.objectProto; ++hops) {
            if (hops == kMethodHops || JS_IsProxy(at)) {
                JS_FreeValue(ctx, at);
                if (hops < kMethodHops) return JS_GetProperty(ctx, object, atom(p, name));
                return JS_ThrowRangeError(ctx, "the prototype chain is longer than %d objects", kMethodHops);
            }
            JSPropertyDescriptor d;
            const int own = JS_GetOwnProperty(ctx, &d, at, atom(p, name));
            if (own != 0) {
                JS_FreeValue(ctx, at);
                if (own < 0) return JS_EXCEPTION;
                JSValue found = (d.flags & JS_PROP_GETSET) == 0 ? JS_DupValue(ctx, d.value)
                                : JS_IsFunction(ctx, d.getter) ? JS_Call(ctx, d.getter, object, 0, nullptr)
                                                               : JS_UNDEFINED;
                for (JSValue v : {d.value, d.getter, d.setter}) JS_FreeValue(ctx, v);
                return found;
            }
            JSValue next = JS_GetPrototype(ctx, at);
            JS_FreeValue(ctx, at);
            if (JS_IsException(next)) return JS_EXCEPTION;
            at = next;
        }
        JS_FreeValue(ctx, at);
        return JS_UNDEFINED;
    }

    // Slot i of `array`, an array the node holds, takes `v`: set when the slot is the array's own now (one the host made
    // there and the node kept, or one the node made, so a setter the node hung there runs and a freeze refuses), defined
    // when it is not (never made, or taken away by the node: a shift, a splice, a shorter length, a delete), so the write
    // never walks to an accessor on Array.prototype. The array is asked at the write, with no descriptor wanted, so no
    // getter runs; and an index atom is a tagged integer for every index a queue or a port has, so asking allocates
    // nothing and leaves nothing to free. Below zero, with the exception pending, when the write fails. (An array's
    // length is always its own, and cannot be deleted, so the host sets that straight.)
    static int putSlot(JSContext* ctx, JSValueConst array, std::uint32_t i, JSValue v) {
        assert(i < (1u << 31) && "an index atom past 2^31 - 1 would be a string, which this never frees");
        const int own = JS_GetOwnProperty(ctx, nullptr, array, JS_NewAtomUInt32(ctx, i));
        if (own > 0) return JS_SetPropertyUint32(ctx, array, i, v);
        if (own == 0) return JS_DefinePropertyValueUint32(ctx, array, i, v, kDefine);
        JS_FreeValue(ctx, v);
        return -1;
    }

    static bool callActivate(ScriptHost& h, Instance& in) {
        const Pack& pack = packOf(in);
        JSContext* ctx = pack.audio;
        if (!JS_IsFunction(ctx, in.activate)) return true;
        JS_UpdateStackTop(pack.audioRt);
        const Deadline call(h, packOf(in));
        JSValueConst args[2] = {JS_NewFloat64(ctx, in.sampleRate), JS_NewUint32(ctx, in.maxFrames)};
        JSValue result = JS_Call(ctx, in.activate, in.object, 2, args);
        // A throw is no refusal (the re-activation stands): it is a hook's throw, faulting the node as hookDone says.
        const bool threw = JS_IsException(result);
        hookDone(h, in, threw, "activate");
        const bool ok = threw || !(JS_IsBool(result) && !JS_ToBool(ctx, result));
        JS_FreeValue(ctx, result);
        return ok;
    }

    // A throw counted, and the first one's text kept for reclaim to log, written by `write` into a fixed buffer (the
    // block thread allocates nothing native there).
    template <class Write>
    static void countThrow(ScriptHost& h, Write&& write) {
        h.throws_.fetch_add(1, std::memory_order_relaxed);
        int unclaimed = 0;
        if (!h.firstThrowState_.compare_exchange_strong(unclaimed, 1, std::memory_order_acq_rel)) return;
        TextOut text{h.firstThrow_, sizeof h.firstThrow_};
        write(text);
        h.firstThrowState_.store(2, std::memory_order_release);
    }

    // Counted, and the first one kept for reclaim to log; the exception, pending in `ctx` (the context the call ran
    // in), is cleared. A call the deadline stopped counts as a throw, and says so.
    static void recordThrow(ScriptHost& h, Instance& in, const char* where) {
        recordThrow(h, packOf(in).audio, in.type->id.c_str(), where);
    }
    // `type` null: no one node's (a promise job run where no call ran and no owner is remembered, or one whose node is
    // gone).
    static void recordThrow(ScriptHost& h, JSContext* ctx, const char* type, const char* where) {
        JSValue thrown = JS_GetException(ctx);
        countThrow(h, [&](TextOut& t) {
            t.add(type ? type : where);
            if (h.interrupted_) {
                t.add(" ran past the ");
                t.number(static_cast<long long>(h.callDeadlineNs_.load(std::memory_order_relaxed) / 1000000));
                t.add(" ms call deadline");
                if (type) {
                    t.add(" in ");
                    t.add(where);
                }
                t.add(" (CPU time), and was stopped: ");
            } else if (type) {
                t.add(" threw in ");
                t.add(where);
                t.add(": ");
            } else {
                t.add(" threw: ");
            }
            describe(ctx, thrown, false, t.out + t.n, t.capacity - t.n);
        });
        JS_FreeValue(ctx, thrown);
    }

    // After a hook (onSettings and portsChanged at a plan swap, activate at a re-activation), a create no hook follows,
    // or a reshape no onSettings follows: a throw is counted, then its checkpoint runs, and the node faults at its next
    // block, since it may hold half of what the hook would have set, under faultOnThrow, when the deadline stopped it
    // or its jobs a second time in a row (expiryFaults), or when its checkpoint faults it.
    static void hookDone(ScriptHost& h, Instance& in, bool threw, const char* where) {
        const bool stopped = h.interrupted_;
        if (threw) recordThrow(h, in, where);
        const Settled jobs = checkpoint(h, in, threw, where);
        const bool expired = expiryFaults(in, stopped || jobs.stopped);
        if ((threw && in.type->faultOnThrow) || expired || jobs.fault) in.faultNext = true;
    }

    // ---- the checkpoint after a call: its pack's promise jobs, and its memory (design section 6) ----------------
    // What a checkpoint charged to a node.
    struct Settled {
        bool stopped = false;  // the deadline stopped one of its jobs: its call counts as stopped (expiryFaults)
        bool fault = false;    // it faults now: an allocation its pack refused in its call or its jobs, a job of its
                               //   that failed under faultOnThrow, or its jobs cut short (at once under faultOnThrow,
                               //   else a second time in a row)
    };

    // Whether `pack` refused an allocation since this was last asked; the note is cleared.
    static bool refusedSince(Pack& pack) { return std::exchange(pack.heap.refused, false); }

    // After a call the block thread made into `in` (create, onSettings, portsChanged, activate or process), or the
    // commit of a reshape no onSettings follows, inside the call's deadline, which spans the call and its jobs. An
    // allocation its pack refused during the call faults it, whether or not JS caught the error (`threw`: the call
    // threw, so its throw, the refusal's error or another, is counted; else the refusal is counted as its throw).
    // Then the jobs pending in its pack run (chargeJobs), charged to the pack's remembered owner when it has one,
    // else to `in`: so its own, and only those, while no other node's are pending. What falls to `in` is returned,
    // for its call to fold in as its own (process faults at once, a hook at the next block); what falls to another
    // node, whose leftovers these are, faults that one at its next block (chargeOther). Once the deadline has stopped
    // the call, nothing runs: what it left waits for the pack's next checkpoint, still charged to it. A call into a
    // pack with nothing pending costs one list test here.
    static Settled checkpoint(ScriptHost& h, Instance& in, bool threw, const char* where) {
        Pack& pack = packOf(in);
        Settled self;
        if (refusedSince(pack)) {
            if (!threw) recordRefusal(h, pack, in.type->id.c_str(), where);
            self.fault = true;
        }
        if (!JS_IsJobPending(pack.audioRt)) return self;
        Instance* const owner = pack.jobsOwner ? pack.jobsOwner : pack.jobsOrphaned ? nullptr : &in;
        if (h.interrupted_) {
            pack.jobsOwner = owner;
            return self;
        }
        const Settled jobs = chargeJobs(h, pack, owner);
        if (owner == &in) {
            self.stopped = jobs.stopped;
            self.fault = self.fault || jobs.fault;
        } else if (owner) {
            chargeOther(*owner, jobs);
        }
        return self;
    }

    // The jobs pending in `pack`, at most kJobsPerCheckpoint, charged to `owner` (null: no node's, the node they were
    // charged to having been released, or none remembered by a run where no call ran). A job that fails (a
    // queueMicrotask callback that threw, or JS out of memory) is the owner's throw, and faults a faultOnThrow owner;
    // one the deadline stops ends the run and counts as the owner's call stopped; an allocation the pack refused in one
    // faults the owner whatever its faultOnThrow (and is its throw when JS caught it). Jobs still pending after the
    // bound are cut short: counted (jobsCutShort), and the owner's throw, which faults a faultOnThrow owner at once, as
    // its every throw does, and any other on its second cut short in a row, as two expiries do. QuickJS cannot drop a
    // job, so the pack remembers the owner while any are pending, and its later checkpoints charge them to the same
    // node, never to a pack-mate; one that empties the queue forgets the owner and its count. A promise reaction's own
    // throw only rejects its promise: the job runs to its end.
    static Settled chargeJobs(ScriptHost& h, Pack& pack, Instance* owner) {
        Settled charged;
        const char* const type = owner ? owner->type->id.c_str() : nullptr;
        for (int n = 0; n < kJobsPerCheckpoint; ++n) {
            JSContext* ctx = nullptr;
            const int r = JS_ExecutePendingJob(pack.audioRt, &ctx);
            if (r == 0) break;
            const bool refused = refusedSince(pack);
            if (r < 0) recordThrow(h, ctx, type, "a promise job");
            else if (refused) recordRefusal(h, pack, type, "a promise job");
            if (owner && (refused || (r < 0 && owner->type->faultOnThrow))) charged.fault = true;
            if (r < 0 && h.interrupted_) {
                charged.stopped = true;
                break;
            }
        }
        if (!JS_IsJobPending(pack.audioRt)) {
            pack.jobsOwner = nullptr;
            pack.jobsOrphaned = false;
            if (owner) owner->cutShorts = 0;
            return charged;
        }
        pack.jobsOwner = owner;
        pack.jobsOrphaned = !owner;
        if (charged.stopped) return charged;  // the deadline's, not the bound's: the rest wait, uncounted
        h.jobsCutShort_.fetch_add(1, std::memory_order_relaxed);
        if (owner) {
            recordCutShort(h, type);
            if (owner->cutShorts < 2) ++owner->cutShorts;
            if (owner->cutShorts >= 2 || owner->type->faultOnThrow) charged.fault = true;
        }
        return charged;
    }

    // What a checkpoint charged to a node other than the one whose call it followed, the owner of the leftovers it ran:
    // it faults at its next block when they fault it, or when the deadline stopping one of them is its second expiry in
    // a row (expiryFaults; a run the deadline did not stop leaves its count alone, its own calls being what clear it).
    static void chargeOther(Instance& owner, const Settled& jobs) {
        const bool expired = jobs.stopped && expiryFaults(owner, true);
        if (jobs.fault || expired) owner.faultNext = true;
    }

    // What `pack` has pending where no call into one of its nodes ran: a release's (releaseJs, once the instance's JS
    // is freed), and a quiescent collection's (collect and runGc: a FinalizationRegistry callback whose target it
    // found). It runs there, as a call of its own, under its own deadline, charged to the pack's remembered owner when
    // it has one (never a released instance, which releaseJs has let go of), else to no node.
    static void betweenCalls(ScriptHost& h, Pack& pack) {
        const Deadline call(h, pack);
        Instance* const owner = pack.jobsOwner;
        const Settled jobs = chargeJobs(h, pack, owner);
        if (owner) chargeOther(*owner, jobs);
    }

    // An owner's jobs cut short (chargeJobs): its throw, with no exception behind it.
    static void recordCutShort(ScriptHost& h, const char* type) {
        countThrow(h, [&](TextOut& t) {
            t.add(type);
            t.add("'s promise jobs did not settle in the ");
            t.number(kJobsPerCheckpoint);
            t.add(" a checkpoint runs");
        });
    }

    // An allocation `pack` refused that JS caught, so no throw stands for it: counted as the node's throw (`type`;
    // null: no node's), which faults it.
    static void recordRefusal(ScriptHost& h, const Pack& pack, const char* type, const char* where) {
        countThrow(h, [&](TextOut& t) {
            t.add(type ? type : where);
            t.add(" reached its pack's ");
            addLimit(t, pack.heap.limit);
            if (type) {
                t.add(" in ");
                t.add(where);
                t.add(" (an allocation was refused), and was faulted");
            } else {
                t.add(" (an allocation was refused)");
            }
        });
    }

    static void bindLayout(JSContext* ctx, Layout& l) {
        if (l.bound) return;
        for (std::size_t i = 0; i < l.ports.size(); ++i) l.js[i].atom = JS_NewAtomLen(ctx, l.ports[i].id.data(), l.ports[i].id.size());
        l.bound = true;
    }

    // Its JS let go of: what JS still holds of it (an array, a view, a copy's buffer) is JS's own memory, and stays
    // good for as long as JS holds it, but nothing reads or fills it again.
    static void freeLayoutJs(JSContext* ctx, Layout& l) {
        for (auto& p : l.js) {
            for (JSValue* v : {&p.value, &p.pool, &p.items, &p.payload}) {
                JS_FreeValue(ctx, *v);
                *v = JS_UNDEFINED;
            }
            for (JSValue& b : p.buffers) {
                JS_FreeValue(ctx, b);
                b = JS_UNDEFINED;
            }
            p.bufferFrames = p.viewFrames = p.viewCount = 0;
            p.pooled = 0;
            if (p.atom != JS_ATOM_NULL) JS_FreeAtom(ctx, p.atom);
            p.atom = JS_ATOM_NULL;
            p.stamp = 0;
        }
        l.bound = false;
    }

    // ---- audio: the instance's own copies ------------------------------------------------------------------------
    // The plan's view of an audio port this block.
    static const rp_audio_view& audioView(const rp_process& b, const Port& port) {
        return (port.side == Side::Output ? b.outputs : b.inputs)[port.index].u.audio;
    }

    // The samples of one copy, or null when it has none that holds `frames`: never made, detached by JS (a view's
    // buffer transferred away), which clears the exception that says so, or shorter than the block. A copy is never
    // made shorter, and JS cannot shrink one it did not make resizable, so the last is a guard: such a copy is refused
    // as a detached one is (an output's channel silent, the copy made again next block).
    static float* samples(JSContext* ctx, JSValueConst buffer, std::uint32_t frames) {
        if (JS_IsUndefined(buffer)) return nullptr;
        std::size_t size = 0;
        std::uint8_t* data = JS_GetArrayBuffer(ctx, &size, buffer);
        if (!data) clearException(ctx);
        return data && size >= std::size_t(frames) * sizeof(float) ? reinterpret_cast<float*>(data) : nullptr;
    }

    // Every audio port's copies, readied for this block: made the first time a channel runs, and again when a larger
    // block than they hold is activated or JS has transferred one away (so a steady block allocates nothing), each
    // view made again only when its copy is or the block's length moves, and an input's samples copied in. False,
    // with the exception pending, when JS is out of memory or a write onto the views array the node holds fails (a
    // setter it hung on a slot threw, or ran past the deadline, or it froze the array), which process then fails as the
    // node's throw. Every view of that port is then made again next block, so the array is never left holding a view of
    // a copy the host has since replaced.
    static bool readyAudio(const Pack& pack, JSContext* ctx, Instance& in, const rp_process& b) {
        Layout& l = *in.layout;
        for (std::size_t i = 0; i < l.ports.size(); ++i) {
            if (l.ports[i].kind != PortKind::Audio) continue;
            PortJs& p = l.js[i];
            const rp_audio_view& v = audioView(b, l.ports[i]);
            if (JS_IsUndefined(p.value)) {
                JSValue views = JS_NewArray(ctx);
                if (JS_IsException(views)) return false;
                p.value = views;
            }
            // A length no block has (one of no frames runs no node), so the next block makes every view again.
            const auto stale = [&p] {
                p.viewFrames = 0;
                return false;
            };
            const std::uint32_t want = std::max(in.maxFrames, b.frames);
            if (p.bufferFrames < want) {  // a block longer than the copies: all of them made again, at the new size
                for (JSValue& c : p.buffers) {
                    JS_FreeValue(ctx, c);
                    c = JS_UNDEFINED;
                }
                p.bufferFrames = want;
            }
            const std::uint32_t count = std::min<std::uint32_t>(v.channel_count, static_cast<std::uint32_t>(p.buffers.size()));
            for (std::uint32_t c = 0; c < count; ++c) {
                float* data = samples(ctx, p.buffers[c], b.frames);
                const bool fresh = !data;
                if (fresh) {
                    JS_FreeValue(ctx, p.buffers[c]);
                    p.buffers[c] = JS_UNDEFINED;
                    JSValue made = JS_NewArrayBufferCopy(ctx, nullptr, std::size_t(p.bufferFrames) * sizeof(float));
                    if (JS_IsException(made)) return stale();
                    p.buffers[c] = made;
                    if (!(data = samples(ctx, made, b.frames))) return stale();
                }
                if (fresh || c >= p.viewCount || p.viewFrames != b.frames) {
                    // All three arguments: the constructor reads offset and length whatever argc says.
                    JSValueConst args[3] = {p.buffers[c], JS_NewUint32(ctx, 0), JS_NewUint32(ctx, b.frames)};
                    JSValue view = JS_NewTypedArray(ctx, 3, args, JS_TYPED_ARRAY_FLOAT32);
                    if (JS_IsException(view) || putSlot(ctx, p.value, c, view) < 0) return stale();
                }
                if (l.ports[i].side == Side::Input) std::memcpy(data, v.channels[c], std::size_t(b.frames) * sizeof(float));
            }
            if (count != p.viewCount || p.viewFrames != b.frames) {
                if (JS_SetProperty(ctx, p.value, atom(pack, kLength), JS_NewUint32(ctx, count)) < 0) return stale();
                p.viewCount = count;
                p.viewFrames = b.frames;
            }
        }
        return true;
    }

    // Every audio output's copies, out into the plan's buffers: a copy JS has transferred away during the block leaves
    // its channel silent (and is made again next block).
    static void copyAudioOut(JSContext* ctx, Instance& in, const rp_process& b) {
        const Layout& l = *in.layout;
        for (std::size_t i = 0; i < l.ports.size(); ++i) {
            const Port& port = l.ports[i];
            if (port.kind != PortKind::Audio || port.side != Side::Output) continue;
            const PortJs& p = l.js[i];
            const rp_audio_view& v = audioView(b, port);
            const std::uint32_t count = std::min<std::uint32_t>(v.channel_count, static_cast<std::uint32_t>(p.buffers.size()));
            for (std::uint32_t c = 0; c < count; ++c) {
                const float* data = samples(ctx, p.buffers[c], b.frames);
                if (data) std::memcpy(v.channels[c], data, std::size_t(b.frames) * sizeof(float));
                else std::memset(v.channels[c], 0, std::size_t(b.frames) * sizeof(float));
            }
        }
    }

    // A block that could not run: its audio outputs silent.
    static void silenceAudio(Instance& in, const rp_process& b) {
        const Layout& l = *in.layout;
        for (std::size_t i = 0; i < l.ports.size(); ++i) {
            const Port& port = l.ports[i];
            if (port.kind != PortKind::Audio || port.side != Side::Output) continue;
            const rp_audio_view& v = audioView(b, port);
            for (std::uint32_t c = 0; c < v.channel_count; ++c) std::memset(v.channels[c], 0, std::size_t(b.frames) * sizeof(float));
        }
    }

    // ---- the Block object ----------------------------------------------------------------------------------------
    // The instance whose process call is running with this block, or null with a TypeError thrown.
    static Instance* running(JSContext* ctx, JSValueConst self) {
        auto* in = static_cast<Instance*>(JS_GetOpaque(self, packOf(ctx).blockClass));
        if (!in || !in->running) {
            JS_ThrowTypeError(ctx, "a block is used only during its node's process call");
            return nullptr;
        }
        return in;
    }

    // The port `id` names, when it is one of these kinds on one of these sides (kInputs | kOutputs | kParams); null,
    // with a TypeError thrown, when not. A linear walk comparing atoms: a layout has a handful of ports.
    static constexpr unsigned kInputs = 1, kOutputs = 2, kParams = 4;
    static PortJs* lookup(JSContext* ctx, Instance& in, JSValueConst id, unsigned sides, std::initializer_list<PortKind> kinds,
                          const char* what, const Port** found) {
        const JSAtom a = JS_ValueToAtom(ctx, id);
        if (a == JS_ATOM_NULL) return nullptr;
        Layout& l = *in.layout;
        for (std::size_t i = 0; i < l.ports.size(); ++i) {
            const Port& p = l.ports[i];
            const unsigned side = p.side == Side::Input ? kInputs : p.side == Side::Output ? kOutputs : kParams;
            if (l.js[i].atom != a || !(sides & side) || std::find(kinds.begin(), kinds.end(), p.kind) == kinds.end()) continue;
            JS_FreeAtom(ctx, a);
            *found = &p;
            return &l.js[i];
        }
        JS_FreeAtom(ctx, a);
        const char* name = JS_ToCString(ctx, id);
        JS_ThrowTypeError(ctx, "%s has no %s %s", in.type->id.c_str(), what, name ? name : "of that id");
        if (name) JS_FreeCString(ctx, name);
        return nullptr;
    }

    // Element i of a port's pool (a message, or a control change), made when it has never been, with `fields` its own
    // (defineFields), so a fill's writes onto it, and its read of a message's data, reach Object.prototype only for a
    // field the node deleted (kDefine). The pool is read only below `pooled`, where every slot is one the host defined,
    // so no read walks to a getter on Array.prototype either. JS_EXCEPTION, with it pending, when JS is out of memory.
    static JSValue element(const Pack& pack, JSContext* ctx, PortJs& p, std::uint32_t i,
                           std::initializer_list<Atom> fields) {
        if (i < p.pooled) return JS_GetPropertyUint32(ctx, p.pool, i);
        assert(i == p.pooled && "a fill makes its elements in order");
        JSValue e = JS_NewObject(ctx);
        if (JS_IsException(e)) return e;
        if (!defineFields(pack, ctx, e, fields) ||
            JS_DefinePropertyValueUint32(ctx, p.pool, i, JS_DupValue(ctx, e), kDefine) < 0) {
            JS_FreeValue(ctx, e);
            return JS_EXCEPTION;
        }
        ++p.pooled;
        return e;
    }

    // A fresh value into *slot; false, with the exception pending, when JS is out of memory.
    static bool make(JSValue* slot, JSValue made) {
        if (JS_IsException(made)) return false;
        *slot = made;
        return true;
    }

    // Does `view` already look at [offset, offset + size) of `buffer`?
    static bool viewing(JSContext* ctx, JSValueConst view, JSValueConst buffer, std::uint32_t offset, std::uint32_t size) {
        if (JS_GetTypedArrayType(view) != JS_TYPED_ARRAY_UINT8) return false;
        std::size_t at = 0, length = 0;
        JSValue b = JS_GetTypedArrayBuffer(ctx, view, &at, &length, nullptr);
        if (JS_IsException(b)) {
            clearException(ctx);
            return false;
        }
        const bool same = sameObject(b, buffer) && at == offset && length == size;
        JS_FreeValue(ctx, b);
        return same;
    }

    // One message's frame, data and flags onto its element. False, with the exception pending, when JS is out of memory
    // or a read or write there fails.
    static bool fillMessage(const Pack& pack, JSContext* ctx, const PortJs& p, JSValueConst e, const rp_message& m) {
        if (JS_SetProperty(ctx, e, atom(pack, kFrame), JS_NewUint32(ctx, m.frame)) < 0) return false;
        JSValue data = JS_GetProperty(ctx, e, atom(pack, kData));
        if (JS_IsException(data)) return false;
        const bool same = viewing(ctx, data, p.payload, m.offset, m.size);
        JS_FreeValue(ctx, data);
        if (!same) {
            JSValueConst args[3] = {p.payload, JS_NewUint32(ctx, m.offset), JS_NewUint32(ctx, m.size)};
            JSValue view = JS_NewTypedArray(ctx, 3, args, JS_TYPED_ARRAY_UINT8);
            if (JS_IsException(view) || JS_SetProperty(ctx, e, atom(pack, kData), view) < 0) return false;
        }
        return JS_SetProperty(ctx, e, atom(pack, kFlags), JS_NewUint32(ctx, m.flags)) >= 0;
    }

    // An events or bytes input as a reused Message[]: the payloads copied into one JS-owned buffer (grown, never
    // shrunk), each message's data a Uint8Array over its bytes there, made again only when its place moves. False, with
    // the exception pending, when JS is out of memory or a write onto what the node holds fails (a setter it hung on a
    // message threw, or it froze the array or a message), which messages() then throws as the node's.
    static bool fillMessages(const Pack& pack, JSContext* ctx, PortJs& p, const rp_queue& q) {
        if (JS_IsUndefined(p.value)) {
            JSValue pool = JS_NewArray(ctx), value = JS_NewArray(ctx);
            if (JS_IsException(pool) || JS_IsException(value)) {
                JS_FreeValue(ctx, pool);
                JS_FreeValue(ctx, value);
                return false;
            }
            p.pool = pool;
            p.value = value;
            p.pooled = 0;
        }
        if (q.count > 0 && q.bytes_capacity > 0) {
            std::size_t room = 0;
            std::uint8_t* bytes = JS_IsUndefined(p.payload) ? nullptr : JS_GetArrayBuffer(ctx, &room, p.payload);
            if (!bytes && !JS_IsUndefined(p.payload)) clearException(ctx);  // detached, by JS
            if (!bytes || q.bytes_used > room) {
                // The queue's arena holds bytes_capacity bytes, so any size up to that can be copied out of it.
                const std::size_t size =
                    std::min<std::size_t>(q.bytes_capacity, std::max<std::size_t>({q.bytes_used, room * 2, 256}));
                JS_FreeValue(ctx, p.payload);
                p.payload = JS_UNDEFINED;
                if (!make(&p.payload, JS_NewArrayBufferCopy(ctx, q.bytes, std::max<std::size_t>(size, 1)))) return false;
                bytes = JS_GetArrayBuffer(ctx, &room, p.payload);
            }
            if (bytes && q.bytes_used) std::memcpy(bytes, q.bytes, q.bytes_used);
        }
        for (std::uint32_t i = 0; i < q.count; ++i) {
            JSValue e = element(pack, ctx, p, i, {kFrame, kData, kFlags});
            if (JS_IsException(e)) return false;
            if (!fillMessage(pack, ctx, p, e, q.items[i])) {
                JS_FreeValue(ctx, e);
                return false;
            }
            if (putSlot(ctx, p.value, i, e) < 0) return false;
        }
        return JS_SetProperty(ctx, p.value, atom(pack, kLength), JS_NewUint32(ctx, q.count)) >= 0;
    }

    // A control input or a param as a reused { start, changes }. False, with the exception pending, when JS is out of
    // memory or a write onto what the node holds fails (a setter it hung on the stream or a change threw, or it froze
    // one), which control() then throws as the node's.
    static bool fillControl(const Pack& pack, JSContext* ctx, PortJs& p, const rp_control_queue& q) {
        if (JS_IsUndefined(p.value)) {
            JSValue items = JS_NewArray(ctx), pool = JS_NewArray(ctx), value = JS_NewObject(ctx);
            const bool made = !JS_IsException(items) && !JS_IsException(pool) && !JS_IsException(value) &&
                              JS_DefinePropertyValue(ctx, value, atom(pack, kChanges), JS_DupValue(ctx, items), kDefine) >= 0 &&
                              defineFields(pack, ctx, value, {kStart});
            if (!made) {  // all made again next time
                for (JSValue v : {items, pool, value}) JS_FreeValue(ctx, v);
                return false;
            }
            p.items = items;
            p.pool = pool;
            p.value = value;
            p.pooled = 0;
        }
        if (JS_SetProperty(ctx, p.value, atom(pack, kStart), JS_NewFloat64(ctx, q.start)) < 0) return false;
        for (std::uint32_t i = 0; i < q.count; ++i) {
            JSValue e = element(pack, ctx, p, i, {kFrame, kValue});
            if (JS_IsException(e)) return false;
            if (JS_SetProperty(ctx, e, atom(pack, kFrame), JS_NewUint32(ctx, q.changes[i].frame)) < 0 ||
                JS_SetProperty(ctx, e, atom(pack, kValue), JS_NewFloat64(ctx, q.changes[i].value)) < 0) {
                JS_FreeValue(ctx, e);
                return false;
            }
            if (putSlot(ctx, p.items, i, e) < 0) return false;
        }
        return JS_SetProperty(ctx, p.items, atom(pack, kLength), JS_NewUint32(ctx, q.count)) >= 0;
    }

    // block.messages(port): an events or bytes input.
    static JSValue messages(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
        Instance* in = running(ctx, self);
        const Port* found = nullptr;
        PortJs* p = in ? lookup(ctx, *in, argc ? argv[0] : JS_UNDEFINED, kInputs, {PortKind::Events, PortKind::Bytes},
                                    "events or bytes input", &found)
                       : nullptr;
        if (!p) return JS_EXCEPTION;
        if (p->stamp != in->blockStamp) {
            Pack& pack = packOf(*in);
            traceBegin(*pack.host, DSP_SPAN_MARSHAL);
            const bool filled = fillMessages(pack, ctx, *p, *in->running->inputs[found->index].u.queue);
            traceEnd(*pack.host);
            if (!filled) return JS_EXCEPTION;
            p->stamp = in->blockStamp;
        }
        return JS_DupValue(ctx, p->value);
    }

    // block.control(id): a control input, or a param.
    static JSValue control(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
        Instance* in = running(ctx, self);
        const Port* found = nullptr;
        PortJs* p = in ? lookup(ctx, *in, argc ? argv[0] : JS_UNDEFINED, kInputs | kParams, {PortKind::Control},
                                    "control input or param", &found)
                       : nullptr;
        if (!p) return JS_EXCEPTION;
        if (p->stamp != in->blockStamp) {
            const rp_control_queue& q =
                found->side == Side::Param ? *in->running->params[found->index] : *in->running->inputs[found->index].u.control;
            Pack& pack = packOf(*in);
            traceBegin(*pack.host, DSP_SPAN_MARSHAL);
            const bool filled = fillControl(pack, ctx, *p, q);
            traceEnd(*pack.host);
            if (!filled) return JS_EXCEPTION;
            p->stamp = in->blockStamp;
        }
        return JS_DupValue(ctx, p->value);
    }

    // block.audio(port): an audio input or output, as the views of the instance's own copies process readied.
    static JSValue audio(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
        Instance* in = running(ctx, self);
        const Port* found = nullptr;
        PortJs* p = in ? lookup(ctx, *in, argc ? argv[0] : JS_UNDEFINED, kInputs | kOutputs, {PortKind::Audio}, "audio port", &found)
                       : nullptr;
        if (!p) return JS_EXCEPTION;
        return JS_DupValue(ctx, p->value);
    }

    // block.send(port, frame, data, flags?): onto an events or bytes output's queue, straight; false when it is full.
    static JSValue send(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
        Instance* in = running(ctx, self);
        const Port* found = nullptr;
        if (!in || !lookup(ctx, *in, argc ? argv[0] : JS_UNDEFINED, kOutputs, {PortKind::Events, PortKind::Bytes},
                               "events or bytes output", &found))
            return JS_EXCEPTION;
        std::uint32_t frame = 0, flags = 0;
        if (argc < 3 || JS_ToUint32(ctx, &frame, argv[1]) < 0 || (argc > 3 && JS_ToUint32(ctx, &flags, argv[3]) < 0))
            return argc < 3 ? JS_ThrowTypeError(ctx, "send takes a port, a frame and the data") : JS_EXCEPTION;
        rp_queue& q = *in->running->outputs[found->index].u.queue;
        const int typed = JS_GetTypedArrayType(argv[2]);
        if (typed == JS_TYPED_ARRAY_UINT8 || typed == JS_TYPED_ARRAY_UINT8C) {
            std::size_t size = 0;
            const std::uint8_t* data = JS_GetUint8Array(ctx, &size, argv[2]);
            if (!data && size == 0 && JS_HasException(ctx)) return JS_EXCEPTION;
            return JS_NewBool(ctx, graph::pushMessage(q, frame, {data, size}, flags));
        }
        // Any other array-like of numbers, each byte read into the queue as it is reserved.
        std::int64_t length = 0;
        if (JS_GetLength(ctx, argv[2], &length) < 0) return JS_EXCEPTION;
        if (length < 0 || length > static_cast<std::int64_t>(UINT32_MAX)) return JS_ThrowRangeError(ctx, "the data is too long");
        std::uint8_t* at = nullptr;
        if (!graph::reserveMessage(q, frame, static_cast<std::size_t>(length), flags, &at)) return JS_NewBool(ctx, false);
        // Where it is: a getter may send onto the same queue before it returns, so it need not stay the last message
        // (its bytes stay where they are, the arena being fixed).
        const std::uint32_t reserved = q.count - 1;
        for (std::int64_t i = 0; i < length; ++i) {
            JSValue v = JS_GetPropertyUint32(ctx, argv[2], static_cast<std::uint32_t>(i));
            std::int32_t b = 0;
            const bool ok = !JS_IsException(v) && JS_ToInt32(ctx, &b, v) == 0;
            JS_FreeValue(ctx, v);
            if (!ok) {  // a node's bug, and it throws: the message is taken back out, so no part of it goes (half a
                        // note-on, zeros after, would be a note-off)
                eraseMessage(q, reserved);
                return JS_EXCEPTION;
            }
            at[i] = static_cast<std::uint8_t>(b & 0xff);
        }
        return JS_NewBool(ctx, true);
    }

    // block.emit(port, frame, value): onto a control output.
    static JSValue emit(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
        Instance* in = running(ctx, self);
        const Port* found = nullptr;
        if (!in || !lookup(ctx, *in, argc ? argv[0] : JS_UNDEFINED, kOutputs, {PortKind::Control}, "control output", &found))
            return JS_EXCEPTION;
        std::uint32_t frame = 0;
        double value = 0.0;
        if (argc < 3) return JS_ThrowTypeError(ctx, "emit takes a port, a frame and a value");
        if (JS_ToUint32(ctx, &frame, argv[1]) < 0 || JS_ToFloat64(ctx, &value, argv[2]) < 0) return JS_EXCEPTION;
        return JS_NewBool(ctx, graph::pushControl(*in->running->outputs[found->index].u.control, frame, value));
    }
};

// ---- ScriptHost ------------------------------------------------------------------------------------------------

ScriptHost::ScriptHost(Binder binder, void* user) : binder_(binder), binderUser_(user) {
    // The fixed labels' names (a profile build's: traceName names nothing in any other); a pack's load names its
    // types'.
    for (const auto& [label, name] : {std::pair{DSP_SPAN_SCRIPT_NODE, "script-node"}, std::pair{DSP_SPAN_MARSHAL, "marshal"},
                                      std::pair{DSP_SPAN_JSCALL, "js-call"}, std::pair{DSP_SPAN_APU, "apu-render"},
                                      std::pair{DSP_SPAN_PUBLISH, "state-publish"}})
        traceName(label, name);
    packs_.push_back(makePack(kBuiltInPack, counters_));  // loaded by load
}

ScriptHost::~ScriptHost() {
    assert(!audioOwns_.load() && "destroy a ScriptHost only once no audio thread runs its blocks");
    releaseQueued();
    assert(live_.load() == 0 && "every shim goes before its script host");
    // Each pack leaves what packs share before it goes, as freePack's does, so no way a pack is freed counts what
    // freeing it frees in the shared counters.
    Counters freeing;
    for (auto& pack : packs_) {
        leave(*pack, freeing);
        freeRuntimes(*pack);
    }
}

ScriptHost::Chunk ScriptHost::embeddedBundle() { return {rp_script_bundle, rp_script_bundle_size}; }

const rp_package* ScriptHost::package() const { return &packs_.front()->package; }

std::unique_ptr<ScriptHost::Pack> ScriptHost::makePack(const std::string& id, Counters& counters) {
    auto pack = std::make_unique<Pack>();
    pack->host = this;
    pack->id = id;
    // Spelled as JSON spells the id: a plain id may hold a quote or a backslash, which written raw would make the
    // manifest unreadable, or name another package.
    pack->manifest = R"({"id":)" + graph::jsonString(id) + "}";
    pack->package = rp_package{RP_ABI_VERSION, pack->manifest.c_str(), 0, nullptr};
    pack->heap.counters = &counters;  // before its runtimes are made, which allocate
    pack->controlHeap.counters = &pack->controlCounts;
    makeRuntimes(*pack);
    return pack;
}

void ScriptHost::makeRuntimes(Pack& pack) {
    // Both through the host's allocator, each with a heap of its pack's (Glue's allocator): QuickJS copies the
    // functions and keeps the opaque.
    const JSMallocFunctions counted{&Glue::jsCalloc, &Glue::jsMalloc, &Glue::jsFree, &Glue::jsRealloc, &Glue::jsUsable};
    pack.controlRt = JS_NewRuntime2(&counted, &pack.controlHeap);
    pack.control = JS_NewContext(pack.controlRt);
    pack.audioRt = JS_NewRuntime2(&counted, &pack.heap);
    pack.audio = JS_NewContext(pack.audioRt);
    JS_SetContextOpaque(pack.audio, &pack);  // what a Block method runs in: Glue::packOf
    // Both runtimes' JS stacks, inside the smallest thread that enters either (the class comment), and what the control
    // thread runs in either held to its deadline: the audio runtime's handler is the control thread's until join gives
    // it the block thread's.
    JS_SetMaxStackSize(pack.controlRt, kStackBytes);
    JS_SetMaxStackSize(pack.audioRt, kStackBytes);
    JS_SetInterruptHandler(pack.controlRt, &Glue::controlInterrupt, this);
    JS_SetInterruptHandler(pack.audioRt, &Glue::controlInterrupt, this);

    // Object.prototype, which a node's methods are never read from (Glue::method): by address, as the context holds it
    // for its life and no script can replace it.
    JSContext* ctx = pack.audio;
    JSValue plain = JS_NewObject(ctx);
    JSValue objectProto = JS_GetPrototype(ctx, plain);
    pack.objectProto = JS_VALUE_GET_PTR(objectProto);
    JS_FreeValue(ctx, objectProto);
    JS_FreeValue(ctx, plain);

    // The Block object: one per instance, its methods on the class prototype, the instance its opaque. Every node of
    // the pack reaches that prototype (Object.getPrototypeOf(block)), so it and its methods are frozen: no node
    // replaces, removes or shadows a method there, or on one of the functions, for another. A class id is its runtime's
    // own (JS_NewClassID takes the runtime, and the pack's id is 0 until then), so each pack's has its own Block class.
    JS_NewClassID(pack.audioRt, &pack.blockClass);
    JSClassDef block{};
    block.class_name = "Block";
    JS_NewClass(pack.audioRt, pack.blockClass, &block);
    JSValue proto = JS_NewObject(ctx);
    const auto bindFrozen = [&](JSValueConst on, const char* name, JSCFunction* fn, int length, int flags) {
        JSValue f = JS_NewCFunction(ctx, fn, name, length);
        JS_FreezeObject(ctx, f);
        JS_DefinePropertyValueStr(ctx, on, name, f, flags);
    };
    bindFrozen(proto, "messages", &Glue::messages, 1, JS_PROP_C_W_E);
    bindFrozen(proto, "send", &Glue::send, 4, JS_PROP_C_W_E);
    bindFrozen(proto, "control", &Glue::control, 1, JS_PROP_C_W_E);
    bindFrozen(proto, "emit", &Glue::emit, 3, JS_PROP_C_W_E);
    bindFrozen(proto, "audio", &Glue::audio, 1, JS_PROP_C_W_E);
    JS_FreezeObject(ctx, proto);
    JS_SetClassProto(ctx, pack.blockClass, proto);
    for (std::size_t a = 0; a < kAtomCount; ++a) pack.atoms[a] = JS_NewAtom(ctx, kAtomNames[a]);

    if (binder_) {
        binder_(binderUser_, pack.control, false);
        binder_(binderUser_, pack.audio, true);
    }
}

void ScriptHost::freeRuntimes(Pack& pack) {
    freeTypes(pack);
    for (std::size_t a = 0; a < kAtomCount; ++a) JS_FreeAtom(pack.audio, pack.atoms[a]);
    JS_FreeContext(pack.audio);
    JS_FreeRuntime(pack.audioRt);
    JS_FreeContext(pack.control);
    JS_FreeRuntime(pack.controlRt);
    pack.audio = pack.control = nullptr;
    pack.audioRt = pack.controlRt = nullptr;
}

bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error, std::size_t memoryLimit) {
    // Refused without touching what a first load registered: a registry may hold its types already.
    if (loaded_) {
        if (error) *error = "a script host loads one bundle";
        return false;
    }
    loaded_ = true;
    Pack& pack = *packs_.front();
    // Both runtimes' limit, before the load, whose allocations it holds.
    pack.heap.limit = pack.controlHeap.limit = memoryLimit ? memoryLimit : kDefaultMemoryLimit;
    const bool loaded = loadInto(pack, chunks, error);
    join(pack);  // counted in the shared counters from the start: no audio thread runs before the host is loaded
    return loaded;
}

bool ScriptHost::canLoadPack(const std::string& id, std::string* error) const {
    const auto refuse = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (!graph::isPlainId(id))
        return refuse("pack id " + graph::jsonString(id) +
                      " must be non-empty, with no whitespace or control characters");
    if (id == kBuiltInPack)
        return refuse("pack id " + graph::jsonString(id) + " is the built-in pack, which is not reloadable");
    if (id == "rp" || id.rfind("rp.", 0) == 0)
        return refuse("pack id " + graph::jsonString(id) +
                      " is the app's to give: \"rp\" and every id under \"rp.\" name its own packages (its built-in "
                      "script pack, its native ones)");
    return true;
}

const rp_package* ScriptHost::loadPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
                                       std::string* error) {
    if (!canLoadPack(id, error)) return nullptr;
    // The id's next generation, whatever came of its loads before and whenever their generations were freed, so one
    // number never names two sources.
    const std::uint32_t generation = ++generations_[id];
    // What its audio runtime allocates until it joins, the load's own, counted apart: this is the control thread, and
    // the block thread may be counting into the shared counters, which have one writer at a time.
    Counters loading;
    auto pack = makePack(id, loading);
    pack->generation = generation;
    // Both runtimes' limit, before the load, whose allocations it holds.
    pack->heap.limit = pack->controlHeap.limit = memoryLimit ? memoryLimit : kDefaultMemoryLimit;
    if (!loadInto(*pack, chunks, error)) {
        freeRuntimes(*pack);  // counted in `loading` still
        return nullptr;
    }
    join(*pack);  // what its load left in cycles collected at its end (loadInto)
    packs_.push_back(std::move(pack));
    return &packs_.back()->package;
}

ScriptHost::Packs::iterator ScriptHost::findPack(const rp_package* package) {
    return std::find_if(packs_.begin() + 1, packs_.end(), [&](const auto& p) { return &p->package == package; });
}

void ScriptHost::retirePack(const rp_package* package) {
    const auto at = findPack(package);
    assert(at != packs_.end() && "retirePack takes a pack loadPack returned, never the built-in one");
    if (at == packs_.end()) return;
    (*at)->standing = PackStanding::Retired;
    if ((*at)->records == 0) freePack(at);
}

void ScriptHost::discardPack(const rp_package* package) {
    const auto at = findPack(package);
    assert(at != packs_.end() && "discardPack takes a pack loadPack returned, never the built-in one");
    if (at == packs_.end()) return;
    (*at)->standing = PackStanding::Discarded;
    if ((*at)->records == 0) freePack(at);
}

const rp_package* ScriptHost::currentPack(const std::string& id) const {
    for (auto at = packs_.begin() + 1; at != packs_.end(); ++at)
        if ((*at)->id == id && (*at)->standing == PackStanding::Current) return &(*at)->package;
    return nullptr;
}

void ScriptHost::setPackFreed(PackFreed freed, void* user) {
    packFreed_ = freed;
    packFreedUser_ = user;
}

std::vector<ScriptHost::PackInfo> ScriptHost::packs() const {
    std::vector<PackInfo> out;
    for (const auto& pack : packs_)
        out.push_back({pack->id, pack->generation, pack->standing, pack->records});
    return out;
}

// Every retired or discarded pack no record of whose types is alive: the block thread holds nothing of it (the class
// comment), so it goes here, on the control thread, whether or not an audio thread runs.
void ScriptHost::freeIdle() {
    for (auto at = packs_.begin() + 1; at != packs_.end();) {
        if ((*at)->standing != PackStanding::Current && (*at)->records == 0) at = freePack(at);
        else ++at;
    }
}

// A retired generation's package named to the callback while its types still stand (what holds its retired entries
// drops them), then its audio runtime leaves what every pack shares and its runtimes go, with the pack.
ScriptHost::Packs::iterator ScriptHost::freePack(Packs::iterator at) {
    Pack& pack = **at;
    if (pack.standing == PackStanding::Retired && packFreed_) packFreed_(packFreedUser_, &pack.package);
    Counters freeing;  // what freeing it frees, counted apart (leave)
    leave(pack, freeing);
    freeRuntimes(pack);
    at = packs_.erase(at);
    settleFreedLive();
    return at;
}

// join's reverse, for a pack about to be freed on the control thread: its frees counted apart, since the block thread
// may be writing the shared counters (a profile build notes what it had added to their live heap, for settleFreedLive),
// and the control thread's interrupt handler in place of the block thread's, whose deadline state it must not touch.
// Both runtimes' stacks anchored here: the audio one was last entered on the block thread.
void ScriptHost::leave(Pack& pack, Counters& counters) {
#ifdef RETROPLUG_PROFILE
    freedLive_ += static_cast<std::int64_t>(pack.heap.live) - static_cast<std::int64_t>(pack.joinedLive);
#endif
    pack.heap.counters = &counters;
    JS_SetInterruptHandler(pack.audioRt, &Glue::controlInterrupt, this);
    JS_UpdateStackTop(pack.audioRt);
    JS_UpdateStackTop(pack.controlRt);
}

// A profile build's live heap in the shared counters, without what freed packs had added to it since they joined:
// taken off once no audio thread runs, when this thread is their one writer.
void ScriptHost::settleFreedLive() {
#ifdef RETROPLUG_PROFILE
    if (freedLive_ == 0 || audioOwns_.load(std::memory_order_acquire)) return;
    Glue::grew(counters_, -freedLive_);
    freedLive_ = 0;
#endif
}

void ScriptHost::deleteRecord(Instance* in) {
    --in->type->pack->records;
    delete in;
}

// The pack's audio runtime joins what every pack shares: what the block thread runs in it held to the call deadline,
// whose state every pack shares (the handler's opaque is the host), and its allocations counted with every other
// pack's (and, in a profile build, its auto-GC pinned off with theirs). Not before its load is done, which runs on the
// control thread while an audio thread may be making a call into another pack: a handler polled in the load would read
// and write that call's deadline state, and its allocations would be a second writer of the counters.
void ScriptHost::join(Pack& pack) {
    JS_SetInterruptHandler(pack.audioRt, &Glue::interrupt, this);  // in place of the control thread's
    pack.heap.counters = &counters_;
    pack.joinedLive = pack.heap.live;
    if (gcPinned_) JS_SetGCThreshold(pack.audioRt, static_cast<std::size_t>(-1));
}

bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error) {
    // An allocation the load was refused fails it (the chunk throws out of memory); no node's call is to be faulted,
    // and no settings check is to say so.
    struct Forget {
        Pack* pack;
        ~Forget() { pack->heap.refused = pack->controlHeap.refused = false; }
    } forget{&pack};
    // The whole load is one call, held to the control thread's deadline: a top level, a job or a definition that runs
    // past it refuses the pack, saying so.
    const Glue::ControlDeadline call(*this);
    auto fail = [&](std::string why) {
        freeTypes(pack);
        // What a read that failed left pending (the deadline's error among them) goes with the load: the built-in pack
        // keeps its runtimes, and nothing is to find it there.
        clearException(pack.control);
        clearException(pack.audio);
        if (controlInterrupted_) why = Glue::stopped(*this, "its load") + ": " + why;
        // A load the memory limit refused an allocation says so: the limit holds each of the pack's runtimes whole,
        // its intrinsics included, and once it cannot even make its out-of-memory error QuickJS throws null, which
        // says nothing.
        if (pack.heap.refused || pack.controlHeap.refused)
            why += " (the load reached the pack's " + limitText(pack.heap.limit) + ")";
        if (error) *error = std::move(why);
        return false;
    };
    // The promise jobs a stretch of the load queued in `rt`, run there and then, at most kJobsPerCheckpoint; whether
    // they settled. A load's jobs are no node's, and must settle: QuickJS cannot drop a pending job, so one a load left
    // would run at its pack's first checkpoint, charged to whichever of its nodes' calls came first. So a load that
    // leaves any is refused, saying which stretch left them.
    const auto settle = [](JSRuntime* rt) {
        return runJobs(rt, kJobsPerCheckpoint, [](JSContext* c) { clearException(c); });
    };
    const auto unsettled = [](const std::string& whose) {
        return whose + " promise jobs did not settle in the " + std::to_string(kJobsPerCheckpoint) + " a load runs";
    };
    JSContext* const control = pack.control;
    JSContext* const audio = pack.audio;
    for (const bool audioSide : {false, true}) {
        JSContext* ctx = audioSide ? audio : control;
        JS_UpdateStackTop(audioSide ? pack.audioRt : pack.controlRt);
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            JSValue code = JS_ReadObject(ctx, chunks[i].data(), chunks[i].size(), JS_READ_OBJ_BYTECODE);
            JSValue result = JS_IsException(code) ? JS_EXCEPTION : JS_EvalFunction(ctx, code);  // consumes `code`
            if (JS_IsException(result)) return fail("chunk " + std::to_string(i) + ": " + exceptionText(ctx, false));
            JS_FreeValue(ctx, result);
        }
        // What the bundle's top level queued runs now, in this context's runtime (no block has begun), and must settle.
        const char* const side = audioSide ? "audio" : "control";
        const bool settled = settle(audioSide ? pack.audioRt : pack.controlRt);
        if (controlInterrupted_)
            return fail(std::string("a promise job its top level queued in the ") + side + " context");
        if (!settled) return fail(unsettled("its top level's") + ", in the " + side + " context");
    }
    // What the load leaves in cycles in the audio runtime, its top level's and the descriptors' below, goes at the first
    // quiescent reclaim (the built-in pack's) or at the load's end (a loaded pack's), rather than waiting for a block's
    // threshold GC. (The production bundle left about 144 KiB while zod and the DSP kernel were in it; it has left none
    // since B3b.)
    pack.garbage = true;
    // The definitions, in the same order in both contexts (the same bundle ran in each).
    JSValue lists[2];
    for (const bool audioSide : {false, true}) {
        JSContext* ctx = audioSide ? audio : control;
        JSValue global = JS_GetGlobalObject(ctx);
        lists[audioSide] = JS_GetPropertyStr(ctx, global, kNodesGlobal);
        JS_FreeValue(ctx, global);
    }
    struct Lists {
        JSContext* control;
        JSContext* audio;
        JSValue*   v;
        ~Lists() {
            JS_FreeValue(control, v[0]);
            JS_FreeValue(audio, v[1]);
        }
    } owned{control, audio, lists};
    if (JS_IsUndefined(lists[0])) return true;  // a bundle that registers nothing
    std::int64_t count = 0;
    if (!JS_IsArray(lists[0]) || JS_GetLength(control, lists[0], &count) < 0)
        return fail(std::string(kNodesGlobal) + " is not an array of node definitions");
    for (std::int64_t i = 0; i < count; ++i) {
        auto t = std::make_unique<Type>();
        t->host = this;
        t->pack = &pack;
        t->control = JS_GetPropertyUint32(control, lists[0], static_cast<std::uint32_t>(i));
        t->audio = JS_GetPropertyUint32(audio, lists[1], static_cast<std::uint32_t>(i));
        pack.types.push_back(std::move(t));
        Type& type = *pack.types.back();
        const std::string at = "definition #" + std::to_string(i);
        JSValue id = JS_GetPropertyStr(control, type.control, "id");
        const char* text = JS_IsString(id) ? JS_ToCString(control, id) : nullptr;
        if (text) {
            type.id = text;
            JS_FreeCString(control, text);
        }
        JS_FreeValue(control, id);
        if (type.id.empty()) return fail(at + " has no id");
        // A loaded pack's types are named under its id (the built-in pack's predate the rule: rp.midi.route, ...), so
        // its namespace meets another's only where one pack's id runs on into another's ("a" beside "a.b", or "rp.midi"
        // beside the built-in rp.midi.route), and there the registry refuses the type a second pack would take.
        if (pack.id != kBuiltInPack &&
            (type.id.size() <= pack.id.size() + 1 || type.id.compare(0, pack.id.size(), pack.id) != 0 ||
             type.id[pack.id.size()] != '.'))
            return fail(at + " (" + type.id + ") is outside pack " + graph::jsonString(pack.id) +
                        ": its type ids are " + graph::jsonString(pack.id + ".<name>"));
        JSValue audioId = JS_GetPropertyStr(audio, type.audio, "id");
        const char* audioText = JS_IsString(audioId) ? JS_ToCString(audio, audioId) : nullptr;
        const bool same = audioText && type.id == audioText;
        if (audioText) JS_FreeCString(audio, audioText);
        JS_FreeValue(audio, audioId);
        if (!same) return fail(at + " (" + type.id + ") is not the same in both contexts: its id differs");
        JSValue d = callMethod(control, type.control, "descriptor", 0, nullptr);
        const bool described = !JS_IsException(d) && stringify(control, d, type.descriptor);
        JS_FreeValue(control, d);
        const bool settled = drainControlJobs(pack);
        if (!described) return fail(type.id + ": its descriptor: " + exceptionText(control, true));
        if (!settled && !controlInterrupted_) return fail(type.id + ": " + unsettled("its descriptor's"));
        // The audio side's must be the same: the control side lays out and checks what the audio side then runs.
        std::string audioDescriptor;
        JSValue ad = callMethod(audio, type.audio, "descriptor", 0, nullptr);
        const bool audioDescribed = !JS_IsException(ad) && stringify(audio, ad, audioDescriptor);
        JS_FreeValue(audio, ad);
        if (!audioDescribed)
            return fail(type.id + ": its descriptor, in the audio context: " + exceptionText(audio, true));
        if (audioDescriptor != type.descriptor)
            return fail(at + " (" + type.id + ") is not the same in both contexts: its descriptor differs, so the bundle "
                             "defines it by which context it runs in");
        for (const auto& [flag, key] : {std::pair{&type.reshapes, "reshapes"}, std::pair{&type.faultOnThrow, "faultOnThrow"}}) {
            JSValue v = JS_GetPropertyStr(control, type.control, key);
            *flag = JS_IsBool(v) && JS_ToBool(control, v);
            JS_FreeValue(control, v);
        }
        type.abi = rp_node_type{type.id.c_str(), &Glue::descriptor, &Glue::layoutFor, &Glue::queryMemory,
                                &Glue::create, &Glue::destroy, RP_TYPE_STATELESS_PREPARE, &type};
        // A read the deadline stopped that left no exception behind (a getter on a flag): refused all the same.
        if (controlInterrupted_) return fail(at + " (" + type.id + ")");
    }
    // What the reads after the last descriptor's drain queued in the control runtime (a getter on a flag), which no
    // later drain there runs.
    const bool read = settle(pack.controlRt);
    if (controlInterrupted_) return fail("a promise job its definitions queued");
    if (!read) return fail(unsettled("its definitions'") + ", in the control context");
    const bool settled = settle(pack.audioRt);  // what the descriptors queued there
    if (controlInterrupted_) return fail("a promise job its descriptors queued");
    if (!settled) return fail(unsettled("its descriptors'") + ", in the audio context");
    // A loaded pack's load collects what it left in cycles now, while the pack is still this thread's alone: once a
    // plan places one of its nodes, its audio runtime is the block thread's, and no block collects. What the collection
    // queues (a FinalizationRegistry callback whose target it found) is the load's too, and settles as the rest must.
    if (pack.id != kBuiltInPack) {
        JS_RunGC(pack.audioRt);
        pack.garbage = false;
        const bool collected = settle(pack.audioRt);
        if (controlInterrupted_) return fail("a promise job its collection queued");
        if (!collected) return fail(unsettled("its collection's") + ", in the audio context");
    }
    // Each type's span label, in registration order (spanLabelFor). Given here, as the pack loads and before any
    // registry is asked, so the type ids of a pack a registry then refuses, or of a reload whose replan is refused,
    // keep a label and a trace name though nothing placed them. Bounded all the same: a type id keeps the label it was
    // given, so the same ids loaded again reuse theirs, and once kDspSpanLabels is reached a new one is traced as the
    // shared label.
    for (const auto& t : pack.types) t->spanLabel = spanLabelFor(t->id);
    pack.typeList.clear();
    for (const auto& t : pack.types) pack.typeList.push_back(&t->abi);
    pack.package = rp_package{RP_ABI_VERSION, pack.manifest.c_str(), static_cast<std::uint32_t>(pack.typeList.size()),
                              pack.typeList.data()};
    return true;
}

// A type id's label: the one it was given when a pack defining it first loaded, else the next one, named by its id,
// while the name table has room; past it, the shared script-node label.
std::uint32_t ScriptHost::spanLabelFor(const std::string& typeId) {
    if (const auto at = spanLabels_.find(typeId); at != spanLabels_.end()) return at->second;
    if (nextSpanLabel_ >= kDspSpanLabels) return DSP_SPAN_SCRIPT_NODE;
    const std::uint32_t label = nextSpanLabel_++;
    spanLabels_.emplace(typeId, label);
    traceName(label, typeId);
    return label;
}

void ScriptHost::freeTypes(Pack& pack) {
    for (auto& t : pack.types) {
        JS_FreeValue(pack.control, t->control);
        JS_FreeValue(pack.audio, t->audio);
    }
    pack.types.clear();
    pack.typeList.clear();
    pack.package = rp_package{RP_ABI_VERSION, pack.manifest.c_str(), 0, nullptr};
}

void ScriptHost::setAudioThreadOwns(bool owns) {
    audioOwns_.store(owns, std::memory_order_release);
    if (owns) return;
    releaseQueued();
    freeIdle();  // a generation whose last records were still queued
    settleFreedLive();
    collect();  // the runtimes are this thread's again: what the audio thread's releases left in cycles goes now
}

void ScriptHost::setCallDeadline(std::chrono::milliseconds limit) {
    callDeadlineNs_.store(std::chrono::nanoseconds(limit).count(), std::memory_order_relaxed);
}

// A destroyed shim's record: released there and then while no audio thread runs blocks (what it leaves in cycles
// waits for the reclaim that ends the operation, so a plan of hundreds going costs one GC, not one each), else queued
// for a later block. Allocates nothing: it runs in a destructor (a plan's), where a bad_alloc would end the process.
void ScriptHost::retire(Instance* in) {
    drainReleased();
    if (!audioOwns_.load(std::memory_order_acquire)) {
        releaseJs(*in);
        deleteRecord(in);
        return;
    }
    if (!backlogHead_ && releases_.tryPush(in)) return;
    in->backlogNext = nullptr;
    (backlogTail_ ? backlogTail_->backlogNext : backlogHead_) = in;
    backlogTail_ = in;
}

void ScriptHost::beginBlock() {
#ifdef RETROPLUG_PROFILE
    blockStart_ = audioAllocations();
#endif
    Instance* in = nullptr;
    for (std::uint32_t n = 0; n < kReleasesPerBlock && releases_.tryPop(in); ++n) {
        releaseJs(*in);
        if (!released_.tryPush(in)) leakedRecords_.fetch_add(1, std::memory_order_relaxed);
    }
}

// The block's promise jobs have run already, each at the checkpoint after the call that queued it.
void ScriptHost::endBlock() {
#ifdef RETROPLUG_PROFILE
    // The allocation window's per-block counts, the promise jobs included.
    const ScriptAllocations now = audioAllocations();
    bump(windowBlocks_);
    bumpMax(maxBlockAllocs_, now.allocs - blockStart_.allocs);
    bumpMax(maxBlockBytes_, now.bytes - blockStart_.bytes);
#endif
}

bool ScriptHost::drainControlJobs(Pack& pack) {
    return !JS_IsJobPending(pack.controlRt) ||
           runJobs(pack.controlRt, kJobsPerCheckpoint, [](JSContext* ctx) { clearException(ctx); });
}

void ScriptHost::drainReleased() {
    Instance* in = nullptr;
    while (released_.tryPop(in)) deleteRecord(in);
    while (backlogHead_) {  // the next read first: once pushed, a record is the block thread's until it comes back
        Instance* next = backlogHead_->backlogNext;
        if (!releases_.tryPush(backlogHead_)) break;
        backlogHead_ = next;
    }
    if (!backlogHead_) backlogTail_ = nullptr;
}

void ScriptHost::reclaim() {
    drainReleased();
    if (const auto leaked = leakedRecords_.load(std::memory_order_relaxed); leaked != leaksReported_) {
        std::fprintf(stderr, "[script] the release ring back was full: %u instance record(s) leaked rather than freed on the audio thread\n",
                     static_cast<unsigned>(leaked - leaksReported_));
        leaksReported_ = leaked;
    }
    if (!jobsLogged_ && jobsCutShort() > 0) {
        std::fprintf(stderr,
                     "[script] a script node's promise jobs outran the %d a checkpoint runs; the rest wait for its "
                     "pack's next checkpoint, charged to it while it lives and to no node once it is released (said "
                     "once; jobsCutShort() counts them)\n",
                     kJobsPerCheckpoint);
        jobsLogged_ = true;
    }
    if (!firstThrowLogged_ && firstThrowState_.load(std::memory_order_acquire) == 2) {
        std::fprintf(stderr, "[script] %s (a throw is counted, and the node runs on unless it faults on one; later ones are "
                             "counted only)\n",
                     firstThrow_);
        firstThrowLogged_ = true;
    }
    freeIdle();  // a retired or discarded generation whose last record just came back
    if (!audioOwns_.load(std::memory_order_acquire)) collect();  // quiescent: what this thread's releases left in cycles
}

std::string ScriptHost::firstThrow() const {
    return firstThrowState_.load(std::memory_order_acquire) == 2 ? std::string(firstThrow_) : std::string();
}

// Quiescent: everything queued for the block thread is released here, in the order it was queued, and every record
// deleted.
void ScriptHost::releaseQueued() {
    Instance* in = nullptr;
    while (releases_.tryPop(in)) {
        releaseJs(*in);
        deleteRecord(in);
    }
    for (Instance* b = backlogHead_; b;) {
        Instance* next = b->backlogNext;
        releaseJs(*b);
        deleteRecord(b);
        b = next;
    }
    backlogHead_ = backlogTail_ = nullptr;
    while (released_.tryPop(in)) deleteRecord(in);
}

// Quiescent only, and never by a block: a GC's cost is the whole heap's, which a block cannot afford. QuickJS's own
// threshold GC may still run inside one when JS allocates past it; this is what keeps a released instance's cycles
// from waiting for that. Each pack's runtime is collected only when something has left garbage in it.
void ScriptHost::collect() {
    for (const auto& pack : packs_) {
        if (!pack->garbage) continue;
        pack->garbage = false;
        if (collectAudio(*pack)) Glue::betweenCalls(*this, *pack);
    }
}

// One quiescent GC of the pack's audio runtime. Whether it queued jobs (a FinalizationRegistry callback whose target
// it found) into a queue that was empty: the caller runs them there and then (Glue::betweenCalls), where at the pack's
// next checkpoint they would be charged to whichever node's call came first. Queued behind another node's leftovers,
// they wait with them, charged to that node.
bool ScriptHost::collectAudio(Pack& pack) {
    const bool idle = !JS_IsJobPending(pack.audioRt);
    JS_UpdateStackTop(pack.audioRt);
    JS_RunGC(pack.audioRt);
    return idle && JS_IsJobPending(pack.audioRt);
}

void ScriptHost::releaseJs(Instance& in) {
    Pack& pack = Glue::packOf(in);
    JSContext* ctx = pack.audio;
    JS_UpdateStackTop(pack.audioRt);
    if (in.layout) Glue::freeLayoutJs(ctx, *in.layout);
    if (!JS_IsUndefined(in.block)) JS_SetOpaque(in.block, nullptr);  // a block JS kept is no longer this instance's
    for (JSValue* v : {&in.object, &in.process, &in.onSettings, &in.portsChanged, &in.activate, &in.block, &in.transport}) {
        JS_FreeValue(ctx, *v);
        *v = JS_UNDEFINED;
    }
    if (in.state == Instance::State::Live) live_.fetch_sub(1, std::memory_order_relaxed);
    in.state = Instance::State::Released;
    pack.garbage = true;
    // Its record is deleted after this, so the pack's owner is never left pointing at it: what it left pending is no
    // node's now.
    if (pack.jobsOwner == &in) {
        pack.jobsOwner = nullptr;
        pack.jobsOrphaned = true;
    }
    // What its pack has pending runs now, as a call of its own, whose deadline drops what freeing it was refused: no
    // node's to fault (and with nothing pending, the next call into the pack drops it).
    if (JS_IsJobPending(pack.audioRt)) Glue::betweenCalls(*this, pack);
}

ScriptAllocations ScriptHost::audioAllocations() const {
    return {counters_.allocs.load(std::memory_order_relaxed), counters_.reallocs.load(std::memory_order_relaxed),
            counters_.frees.load(std::memory_order_relaxed), counters_.bytes.load(std::memory_order_relaxed)};
}

// ---- profiling (spec/08-profiling.md) ----------------------------------------------------------------------------
#ifdef RETROPLUG_PROFILE
DspAllocStats ScriptHost::allocStats() const {
    const ScriptAllocations now = audioAllocations();
    DspAllocStats s;
    s.enabled = true;
    s.allocCalls = now.allocs - windowStart_.allocs;
    s.reallocCalls = now.reallocs - windowStart_.reallocs;
    s.freeCalls = now.frees - windowStart_.frees;
    s.allocBytes = now.bytes - windowStart_.bytes;
    s.liveBytesDelta = counters_.liveBytes.load(std::memory_order_relaxed) - windowLiveBytes_;
    s.peakBytes = counters_.peakBytes.load(std::memory_order_relaxed);
    s.blockCount = windowBlocks_.load(std::memory_order_relaxed);
    s.maxBlockAllocCalls = maxBlockAllocs_.load(std::memory_order_relaxed);
    s.maxBlockAllocBytes = maxBlockBytes_.load(std::memory_order_relaxed);
    return s;
}

bool ScriptHost::refuseWhileOwned(const char* what) {
    if (!audioOwns_.load(std::memory_order_acquire)) return false;
    if (!ownedRefusalLogged_) {
        std::fprintf(stderr, "[script] %s refused: an audio thread is running blocks (profile with the audio stopped; said once)\n",
                     what);
        ownedRefusalLogged_ = true;
    }
    return true;
}

bool ScriptHost::resetAllocStats(bool disableAutoGc) {
    if (refuseWhileOwned("the allocation window")) return false;
    windowStart_ = audioAllocations();
    windowLiveBytes_ = counters_.liveBytes.load(std::memory_order_relaxed);
    windowBlocks_.store(0, std::memory_order_relaxed);
    maxBlockAllocs_.store(0, std::memory_order_relaxed);
    maxBlockBytes_.store(0, std::memory_order_relaxed);
    // QuickJS checks its threshold only as it makes an object, so SIZE_MAX means a collection never starts by itself.
    if (disableAutoGc) {
        gcPinned_ = true;  // a pack loaded later joins pinned
        for (const auto& pack : packs_) JS_SetGCThreshold(pack->audioRt, static_cast<std::size_t>(-1));
    }
    return true;
}

DspGcResult ScriptHost::runGc() {
    if (refuseWhileOwned("a GC pass")) return {};  // enabled false
    const std::int64_t before = counters_.liveBytes.load(std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<Pack*> queued;  // whose collection queued jobs, run once the collections are measured
    for (const auto& pack : packs_)
        if (collectAudio(*pack)) queued.push_back(pack.get());
    DspGcResult r;
    r.enabled = true;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.freedBytes = before - counters_.liveBytes.load(std::memory_order_relaxed);  // the cycles collected
    for (Pack* pack : queued) Glue::betweenCalls(*this, *pack);
    return r;
}

namespace {
std::uint64_t steadyNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

double ScriptHost::traceNow() const { return static_cast<double>(steadyNs() - traceStartNs_) / 1000.0; }

void ScriptHost::spanBegin(std::uint32_t label) {
    if (traceArmed_) traceStack_.emplace_back(label, traceNow());
}

void ScriptHost::spanEnd() {
    if (!traceArmed_ || traceStack_.empty()) return;
    const auto [label, t0] = traceStack_.back();
    traceStack_.pop_back();
    traceSpans_.push_back({label, t0, traceNow()});
}

void ScriptHost::spanSince(std::uint32_t label, double t0) {
    if (traceArmed_) traceSpans_.push_back({label, t0, traceNow()});
}

void ScriptHost::traceName(std::uint32_t label, const std::string& name) {
    if (label >= kDspSpanLabels) return;
    if (traceNames_.size() <= label) traceNames_.resize(std::size_t{label} + 1);
    traceNames_[label] = name;
}

bool ScriptHost::traceReset(bool arm) {
    if (refuseWhileOwned("the trace window")) return false;
    traceStack_.clear();
    traceSpans_.clear();
    traceStartNs_ = steadyNs();
    traceArmed_ = arm;
    return true;
}

std::vector<DspTraceSpan> ScriptHost::traceSpans() const {
    return audioOwns_.load(std::memory_order_acquire) ? std::vector<DspTraceSpan>{} : traceSpans_;
}

std::vector<std::string> ScriptHost::traceNames() const {
    return audioOwns_.load(std::memory_order_acquire) ? std::vector<std::string>{} : traceNames_;
}
#else
DspAllocStats ScriptHost::allocStats() const { return {}; }  // enabled false
bool          ScriptHost::resetAllocStats(bool) { return false; }
DspGcResult   ScriptHost::runGc() { return {}; }
double        ScriptHost::traceNow() const { return 0.0; }
void          ScriptHost::spanBegin(std::uint32_t) {}
void          ScriptHost::spanEnd() {}
void          ScriptHost::spanSince(std::uint32_t, double) {}
void          ScriptHost::traceName(std::uint32_t, const std::string&) {}
bool          ScriptHost::traceReset(bool) { return false; }
std::vector<DspTraceSpan> ScriptHost::traceSpans() const { return {}; }
std::vector<std::string>  ScriptHost::traceNames() const { return {}; }
#endif

}  // namespace rp::host
