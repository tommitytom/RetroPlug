#include "host/script/ScriptHost.hpp"

#include <algorithm>
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
constexpr const char* kPackageManifest = R"({"id":"rp.script"})";

// The property names the block path reads and writes, as atoms made once, so naming one allocates nothing.
enum Atom : std::size_t {
    kFrames, kTransport, kSampleRate, kTempo, kPpq, kPlaying, kFrame, kData, kFlags, kStart, kChanges, kValue,
    kLength, kFault, kAtomCount
};
constexpr const char* kAtomNames[kAtomCount] = {"frames", "transport", "sampleRate", "tempo", "ppq",     "playing", "frame",
                                                "data",   "flags",     "start",      "changes", "value", "length",  "fault"};

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

std::size_t usableSize(const void* p) {
#if defined(__APPLE__)
    return malloc_size(p);
#elif defined(_WIN32)
    return _msize(const_cast<void*>(p));
#elif defined(__linux__) || defined(__GLIBC__)
    return malloc_usable_size(const_cast<void*>(p));
#else
    (void)p;
    return 0;
#endif
}

// One writer (the thread running blocks, or the control thread while quiescent), so a plain increment, kept atomic
// for the readers on other threads.
void bump(std::atomic<std::uint64_t>& c, std::uint64_t by = 1) {
    c.store(c.load(std::memory_order_relaxed) + by, std::memory_order_relaxed);
}

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
    rp_node_type abi{};
    ScriptHost*  host = nullptr;
    std::string  id;
    std::string  descriptor;
    bool         reshapes = false;
    bool         faultOnThrow = false;
    JSValue      control = JS_UNDEFINED;  // the definition, in the control context
    JSValue      audio = JS_UNDEFINED;    // and in the audio context
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

namespace {

// The rp_node a script node is to the planner: a pointer to its record, which outlives it until its release.
struct Shim : rp_node {
    ScriptHost::Instance* in = nullptr;
};

}  // namespace

// Everything the ABI tables, the Block object's methods and the runtime's allocator reach the host through.
struct ScriptHost::Glue {
    static Type&     typeOf(const rp_node_type* t) { return *static_cast<Type*>(t->ctx); }
    static Instance& instanceOf(rp_node* n) { return *static_cast<Shim*>(n)->in; }
    static ScriptHost& hostOf(JSContext* ctx) { return *static_cast<ScriptHost*>(JS_GetContextOpaque(ctx)); }
    static JSAtom    atom(ScriptHost& h, Atom a) { return h.atoms_[a]; }

    // ---- the call deadline ---------------------------------------------------------------------------------------
    // While one lives, what the block thread runs in the audio runtime may use the deadline's worth of the thread's CPU
    // time, and the interrupt handler stops it after: QuickJS throws an uncatchable error, which the call returns as a
    // throw. Arming one reads no clock: the handler takes the baseline at its first poll inside the call and compares at
    // the later ones, so a call too short to reach a poll costs nothing (and one that does goes uncounted for up to
    // 10000 polls, far under any deadline). One made inside another's call is part of that call, and its budget.
    struct Deadline {
        ScriptHost& h;
        bool        nested;
        explicit Deadline(ScriptHost& host) : h(host), nested(host.armed_) {
            if (nested) return;
            h.armed_ = true;
            h.cpuStart_ = -1;
            h.interrupted_ = false;
        }
        ~Deadline() {
            if (!nested) h.armed_ = false;
        }
        Deadline(const Deadline&) = delete;
        Deadline& operator=(const Deadline&) = delete;
    };

    // QuickJS asks once every 10000 of its polls (at each call and each jump back in a loop) whether to stop.
    static int interrupt(JSRuntime*, void* opaque) {
        auto& h = *static_cast<ScriptHost*>(opaque);
        if (!h.armed_) return 0;
        const std::int64_t now = threadCpuNs();
        if (h.cpuStart_ < 0) {
            h.cpuStart_ = now;
            return 0;
        }
        if (now - h.cpuStart_ < h.callDeadlineNs_.load(std::memory_order_relaxed)) return 0;
        h.interrupted_ = true;
        return 1;
    }

    // After one of a node's calls: whether the deadline stopping it faults the node. Under faultOnThrow the first expiry
    // does; without, only a second in a row (design section 4.5: one throw and the next block runs again), and a call
    // that ran to its end clears the count. So a node that hangs still faults within two deadlines, and one long call
    // costs a block rather than the session, which a fault would: it outlives a reshape.
    static bool expiryFaults(const ScriptHost& h, Instance& in) {
        if (!h.interrupted_) {
            in.expiries = 0;
            return false;
        }
        if (in.type->faultOnThrow) return true;
        if (in.expiries < 2) ++in.expiries;
        return in.expiries >= 2;
    }

    // ---- the audio runtime's allocator: the platform's, counted ------------------------------------------------
#ifdef RETROPLUG_PROFILE
    // A profile build keeps the live heap and its peak too, in usable bytes, as QuickJS's own accounting (jsUsable)
    // reads a block.
    static void grew(Counters& c, std::int64_t by) {
        const std::int64_t live = c.liveBytes.load(std::memory_order_relaxed) + by;
        c.liveBytes.store(live, std::memory_order_relaxed);
        if (live > static_cast<std::int64_t>(c.peakBytes.load(std::memory_order_relaxed)))
            c.peakBytes.store(static_cast<std::uint64_t>(live), std::memory_order_relaxed);
    }
#endif
    static void allocated(Counters& c, [[maybe_unused]] void* p, std::size_t size) {
        bump(c.allocs);
        bump(c.bytes, size);
#ifdef RETROPLUG_PROFILE
        grew(c, static_cast<std::int64_t>(usableSize(p)));
#endif
    }
    static void* jsCalloc(void* opaque, std::size_t count, std::size_t size) {
        void* p = std::calloc(count, size);
        if (p) allocated(*static_cast<Counters*>(opaque), p, count * size);
        return p;
    }
    static void* jsMalloc(void* opaque, std::size_t size) {
        void* p = std::malloc(size);
        if (p) allocated(*static_cast<Counters*>(opaque), p, size);
        return p;
    }
    static void jsFree(void* opaque, void* p) {
        if (p) {
            auto& c = *static_cast<Counters*>(opaque);
            bump(c.frees);
#ifdef RETROPLUG_PROFILE
            grew(c, -static_cast<std::int64_t>(usableSize(p)));
#endif
        }
        std::free(p);
    }
    static void* jsRealloc(void* opaque, void* p, std::size_t size) {
        const std::size_t before = p ? usableSize(p) : 0;
        void* q = std::realloc(p, size);
        if (q || size == 0) {
            auto& c = *static_cast<Counters*>(opaque);
            bump(c.reallocs);
            if (size > before) bump(c.bytes, size - before);
#ifdef RETROPLUG_PROFILE
            grew(c, static_cast<std::int64_t>(q ? usableSize(q) : 0) - static_cast<std::int64_t>(before));
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
        ScriptHost& h = *t.host;
        JSContext* ctx = h.control_;
        JS_UpdateStackTop(h.controlRt_);  // QuickJS guards the stack from where it was last entered: re-anchor it here
        const std::string text = s && s->data && s->size ? std::string(s->data, s->size) : std::string("{}");
        JSValue raw = JS_ParseJSON(ctx, text.c_str(), text.size(), "<settings>");
        JSValue parsed = JS_IsException(raw) ? JS_EXCEPTION : callMethod(ctx, t.control, "parseSettings", 1, &raw);
        JSValue lay = JS_IsException(parsed) ? JS_EXCEPTION : callMethod(ctx, t.control, "layout", 1, &parsed);
        const bool ok = !JS_IsException(lay) && stringify(ctx, parsed, settings) && stringify(ctx, lay, layout);
        if (!ok) copyError(err, exceptionText(ctx, true));
        JS_FreeValue(ctx, lay);
        JS_FreeValue(ctx, parsed);
        JS_FreeValue(ctx, raw);
        h.drainControlJobs();
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
        if (!ensureLive(h, in)) return;
        JSContext* ctx = h.audio_;
        if (!JS_IsFunction(ctx, in.portsChanged)) return;
        const Deadline call(h);
        JSValue mask = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < count; ++i) JS_SetPropertyUint32(ctx, mask, i, JS_NewBool(ctx, connected[i] != 0));
        JSValue result = JS_IsException(mask) ? JS_EXCEPTION : JS_Call(ctx, in.portsChanged, in.object, 1, &mask);
        hookDone(h, in, JS_IsException(result), "portsChanged");
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, mask);
    }

    static rp_status process(rp_node* n, const rp_process* block) {
        Instance& in = instanceOf(n);
        ScriptHost& h = *in.type->host;
        if (!ensureLive(h, in) || in.faultNext) return RP_FAULT;
        JSContext* ctx = h.audio_;
        // The whole block, not just the call: a node may hang a setter on what the block path fills in.
        const Deadline call(h);
        traceBegin(h, DSP_SPAN_KERNEL);
        traceBegin(h, DSP_SPAN_MARSHAL);
        ++in.blockStamp;
        // Every audio port's copies first, the inputs' samples in them: a node may read views it kept from a block
        // before, so the copy cannot wait for it to ask.
        if (!readyAudio(ctx, in, *block)) {  // out of memory in JS, so the block cannot run
            const bool expired = expiryFaults(h, in);
            recordThrow(h, in, "process");
            silenceAudio(in, *block);
            traceEnd(h);  // marshal
            processSpanEnd(h);
            return in.type->faultOnThrow || expired ? RP_FAULT : RP_OK;
        }
        in.running = block;
        JS_SetProperty(ctx, in.block, atom(h, kFrames), JS_NewUint32(ctx, block->frames));
        const rp_transport& t = *block->transport;
        JS_SetProperty(ctx, in.transport, atom(h, kSampleRate), JS_NewFloat64(ctx, t.sample_rate));
        JS_SetProperty(ctx, in.transport, atom(h, kTempo), JS_NewFloat64(ctx, t.tempo));
        JS_SetProperty(ctx, in.transport, atom(h, kPpq), JS_NewFloat64(ctx, t.ppq));
        JS_SetProperty(ctx, in.transport, atom(h, kPlaying), JS_NewBool(ctx, (t.flags & RP_TRANSPORT_PLAYING) != 0));
        traceEnd(h);
        jsCallBegin(h);
        JSValue result = JS_Call(ctx, in.process, in.object, 1, &in.block);
        jsCallEnd(h);
        in.running = nullptr;
        const bool threw = JS_IsException(result), expired = expiryFaults(h, in);
        if (threw) recordThrow(h, in, "process");  // first: the copy out may meet a detached buffer, which throws too
        copyAudioOut(ctx, in, *block);             // what it wrote up to a throw stands, as its sends do
        bool fault = false;
        if (threw) {
            fault = in.type->faultOnThrow || expired;
        } else if (JS_IsString(result)) {
            const JSAtom said = JS_ValueToAtom(ctx, result);
            fault = said == atom(h, kFault);
            JS_FreeAtom(ctx, said);
        }
        JS_FreeValue(ctx, result);
        processSpanEnd(h);
        return fault ? RP_FAULT : RP_OK;
    }

    // A process call's js-call span (a profile build), which the spans its JS opens stand inside: opened with the trace
    // stack's depth there kept as their floor, and closed as the call returns, with every span the JS left open above
    // it first, whatever happened in JS (a throw, or the deadline, which skips every finally, can leave a role's open).
    // So a span JS leaves open never closes js-call or dsp-kernel for it.
    static void jsCallBegin([[maybe_unused]] ScriptHost& h) {
#ifdef RETROPLUG_PROFILE
        if (!h.traceArmed_) return;
        h.spanBegin(DSP_SPAN_JSCALL);
        h.jsSpanFloor_ = h.traceStack_.size();
#endif
    }
    static void jsCallEnd([[maybe_unused]] ScriptHost& h) {
#ifdef RETROPLUG_PROFILE
        if (!h.traceArmed_) return;
        const std::size_t floor = h.jsSpanFloor_;
        h.jsSpanFloor_ = kNoJsSpans;
        while (!h.traceStack_.empty() && h.traceStack_.size() >= floor) h.spanEnd();  // JS's, then js-call
#endif
    }

    // Around the host writing onto objects JS holds (a setter JS defined there runs): the spans that setter's JS opens or
    // closes record nothing, as outside a process call, so it cannot close the marshal span the host has open. lift()
    // restores the floor early, before the host closes that span; the destructor does it on every other way out.
    struct JsSpanFence {
        ScriptHost&       h;
        const std::size_t saved;
        mutable bool      lifted = false;
        explicit JsSpanFence(ScriptHost& host) : h(host), saved(host.jsSpanFloor_) {
#ifdef RETROPLUG_PROFILE
            h.jsSpanFloor_ = kNoJsSpans;
#endif
        }
        void lift() const {
#ifdef RETROPLUG_PROFILE
            if (!lifted) h.jsSpanFloor_ = saved;
#endif
            lifted = true;
        }
        ~JsSpanFence() { lift(); }
        JsSpanFence(const JsSpanFence&) = delete;
        JsSpanFence& operator=(const JsSpanFence&) = delete;
    };

    // The end of a process call's dsp-kernel span (a profile build), where apu-render starts if no script node runs
    // after it.
    static void processSpanEnd([[maybe_unused]] ScriptHost& h) {
#ifdef RETROPLUG_PROFILE
        if (h.traceArmed_) {
            h.spanEnd();  // dsp-kernel
            h.lastProcessEnd_ = h.traceNow();
        }
#endif
    }

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
        const bool live = ensureLive(h, in);  // a fresh instance is made from the settings it was created with first
        JSContext* ctx = h.audio_;
        if (next->layout) {
            if (next->layout->json != in.layout->json) {
                freeLayoutJs(ctx, *in.layout);
                prev->layout = std::move(in.layout);
                in.layout = std::move(next->layout);
                bindLayout(ctx, *in.layout);
            } else {
                prev->layout = std::move(next->layout);
            }
        }
        in.settings = next;
        *old = prev;
        if (!live || !JS_IsFunction(ctx, in.onSettings)) return;
        const Deadline call(h);
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
        else JS_UpdateStackTop(h.audioRt_);
        return in.state == Instance::State::Live;
    }

    // The definition's create, with the instance's current settings: it fails (and the node faults) when create
    // throws or returns no process.
    static void createJs(ScriptHost& h, Instance& in) {
        JSContext* ctx = h.audio_;
        JS_UpdateStackTop(h.audioRt_);
        bindLayout(ctx, *in.layout);
        const Deadline call(h);
        // The settings as the control side's parseSettings returned them, in their JSON form (the class comment).
        JSValue settings = JS_ParseJSON(ctx, in.settings->json.c_str(), in.settings->json.size(), "<settings>");
        JSValue cc = JS_IsException(settings) ? JS_EXCEPTION : JS_NewObject(ctx);
        if (!JS_IsException(cc)) {
            JS_SetPropertyStr(ctx, cc, "settings", settings);
            JS_SetPropertyStr(ctx, cc, "sampleRate", JS_NewFloat64(ctx, in.sampleRate));
            JS_SetPropertyStr(ctx, cc, "maxFrames", JS_NewUint32(ctx, in.maxFrames));
        } else {
            JS_FreeValue(ctx, settings);
        }
        JSValue object = JS_IsException(cc) ? JS_EXCEPTION : callMethod(ctx, in.type->audio, "create", 1, &cc);
        JS_FreeValue(ctx, cc);
        if (JS_IsException(object)) {
            recordThrow(h, in, "create");
            in.state = Instance::State::Failed;
            return;
        }
        JSValue process = JS_IsObject(object) ? JS_GetPropertyStr(ctx, object, "process") : JS_UNDEFINED;
        if (!JS_IsFunction(ctx, process)) {
            JS_FreeValue(ctx, process);
            JS_FreeValue(ctx, object);
            JS_ThrowTypeError(ctx, "create returned no process function");
            recordThrow(h, in, "create");
            in.state = Instance::State::Failed;
            return;
        }
        auto hook = [&](const char* name) {
            JSValue fn = JS_GetPropertyStr(ctx, object, name);
            if (JS_IsFunction(ctx, fn)) return fn;
            JS_FreeValue(ctx, fn);
            return JS_UNDEFINED;
        };
        in.object = object;
        in.process = process;
        in.onSettings = hook("onSettings");
        in.portsChanged = hook("portsChanged");
        in.activate = hook("activate");
        in.block = JS_NewObjectClass(ctx, h.blockClass_);
        JS_SetOpaque(in.block, &in);
        in.transport = JS_NewObject(ctx);
        JS_SetProperty(ctx, in.block, atom(h, kTransport), JS_DupValue(ctx, in.transport));
        in.state = Instance::State::Live;
        h.live_.fetch_add(1, std::memory_order_relaxed);
    }

    static bool callActivate(ScriptHost& h, Instance& in) {
        JSContext* ctx = h.audio_;
        if (!JS_IsFunction(ctx, in.activate)) return true;
        JS_UpdateStackTop(h.audioRt_);
        const Deadline call(h);
        JSValueConst args[2] = {JS_NewFloat64(ctx, in.sampleRate), JS_NewUint32(ctx, in.maxFrames)};
        JSValue result = JS_Call(ctx, in.activate, in.object, 2, args);
        bool ok = true;
        if (JS_IsException(result)) {
            if (expiryFaults(h, in)) in.faultNext = true;
            recordThrow(h, in, "activate");
        } else {
            in.expiries = 0;
            ok = !(JS_IsBool(result) && !JS_ToBool(ctx, result));
        }
        JS_FreeValue(ctx, result);
        return ok;
    }

    // Counted, and the first one kept for reclaim to log; the exception is cleared. A call the deadline stopped counts
    // as a throw, and says so.
    static void recordThrow(ScriptHost& h, Instance& in, const char* where) { recordThrow(h, in.type->id.c_str(), where); }
    // `type` null: no one node's (a promise job).
    static void recordThrow(ScriptHost& h, const char* type, const char* where) {
        JSContext* ctx = h.audio_;
        JSValue thrown = JS_GetException(ctx);
        h.throws_.fetch_add(1, std::memory_order_relaxed);
        int unclaimed = 0;
        if (h.firstThrowState_.compare_exchange_strong(unclaimed, 1, std::memory_order_acq_rel)) {
            char* const out = h.firstThrow_;
            std::size_t n = 0;
            const auto add = [&](std::string_view text) { n += copyText(out + n, sizeof h.firstThrow_ - n, text); };
            add(type ? type : where);
            if (h.interrupted_) {
                char ms[24];
                std::snprintf(ms, sizeof ms, "%lld",
                              static_cast<long long>(h.callDeadlineNs_.load(std::memory_order_relaxed) / 1000000));
                add(" ran past the ");
                add(ms);
                add(" ms call deadline");
                if (type) {
                    add(" in ");
                    add(where);
                }
                add(" (CPU time), and was stopped: ");
            } else if (type) {
                add(" threw in ");
                add(where);
                add(": ");
            } else {
                add(" threw: ");
            }
            describe(ctx, thrown, false, out + n, sizeof h.firstThrow_ - n);
            h.firstThrowState_.store(2, std::memory_order_release);
        }
        JS_FreeValue(ctx, thrown);
    }

    // After a hook that runs at a plan swap (onSettings, portsChanged): a throw is counted, and under faultOnThrow, or
    // when it is the deadline's second expiry in a row (expiryFaults), the node faults at its next block, since it may
    // hold half of what the hook would have set.
    static void hookDone(ScriptHost& h, Instance& in, bool threw, const char* where) {
        const bool expired = expiryFaults(h, in);
        if (!threw) return;
        recordThrow(h, in, where);
        if (in.type->faultOnThrow || expired) in.faultNext = true;
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
    // with the exception pending, when JS is out of memory.
    static bool readyAudio(JSContext* ctx, Instance& in, const rp_process& b) {
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
                    if (JS_IsException(made)) return false;
                    p.buffers[c] = made;
                    if (!(data = samples(ctx, made, b.frames))) return false;
                }
                if (fresh || c >= p.viewCount || p.viewFrames != b.frames) {
                    // All three arguments: the constructor reads offset and length whatever argc says.
                    JSValueConst args[3] = {p.buffers[c], JS_NewUint32(ctx, 0), JS_NewUint32(ctx, b.frames)};
                    JSValue view = JS_NewTypedArray(ctx, 3, args, JS_TYPED_ARRAY_FLOAT32);
                    if (JS_IsException(view)) return false;
                    // Refused only by an array JS has frozen, which is the node's to answer for: its views stay as they are.
                    if (JS_SetPropertyUint32(ctx, p.value, c, view) < 0) clearException(ctx);
                }
                if (l.ports[i].side == Side::Input) std::memcpy(data, v.channels[c], std::size_t(b.frames) * sizeof(float));
            }
            if (count != p.viewCount || p.viewFrames != b.frames) {
                if (JS_SetProperty(ctx, p.value, atom(hostOf(ctx), kLength), JS_NewUint32(ctx, count)) < 0) clearException(ctx);
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
        auto* in = static_cast<Instance*>(JS_GetOpaque(self, hostOf(ctx).blockClass_));
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

    // The message objects of `array`, element i made (into pool) when it has never been; JS_EXCEPTION, with it pending,
    // when JS is out of memory.
    static JSValue element(JSContext* ctx, PortJs& p, std::uint32_t i) {
        JSValue e = JS_GetPropertyUint32(ctx, p.pool, i);
        if (JS_IsObject(e)) return e;
        JS_FreeValue(ctx, e);
        e = JS_NewObject(ctx);
        if (JS_IsException(e)) return e;
        if (JS_SetPropertyUint32(ctx, p.pool, i, JS_DupValue(ctx, e)) < 0) {
            JS_FreeValue(ctx, e);
            return JS_EXCEPTION;
        }
        return e;
    }

    // A fresh array (or object) into *slot; false, with the exception pending, when JS is out of memory.
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

    // An events or bytes input as a reused Message[]: the payloads copied into one JS-owned buffer (grown, never
    // shrunk), each message's data a Uint8Array over its bytes there, made again only when its place moves. False, with
    // the exception pending, when JS is out of memory.
    static bool fillMessages(ScriptHost& h, JSContext* ctx, PortJs& p, const rp_queue& q) {
        if (JS_IsUndefined(p.value) && !(make(&p.pool, JS_NewArray(ctx)) && make(&p.value, JS_NewArray(ctx)))) return false;
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
            const rp_message& m = q.items[i];
            JSValue e = element(ctx, p, i);
            if (JS_IsException(e)) return false;
            JS_SetProperty(ctx, e, atom(h, kFrame), JS_NewUint32(ctx, m.frame));
            JSValue data = JS_GetProperty(ctx, e, atom(h, kData));
            if (!viewing(ctx, data, p.payload, m.offset, m.size)) {
                JSValueConst args[3] = {p.payload, JS_NewUint32(ctx, m.offset), JS_NewUint32(ctx, m.size)};
                JSValue view = JS_NewTypedArray(ctx, 3, args, JS_TYPED_ARRAY_UINT8);
                if (JS_IsException(view)) {
                    JS_FreeValue(ctx, data);
                    JS_FreeValue(ctx, e);
                    return false;
                }
                JS_SetProperty(ctx, e, atom(h, kData), view);
            }
            JS_FreeValue(ctx, data);
            JS_SetProperty(ctx, e, atom(h, kFlags), JS_NewUint32(ctx, m.flags));
            JS_SetPropertyUint32(ctx, p.value, i, e);
        }
        JS_SetProperty(ctx, p.value, atom(h, kLength), JS_NewUint32(ctx, q.count));
        return true;
    }

    // A control input or a param as a reused { start, changes }. False, with the exception pending, when JS is out of
    // memory.
    static bool fillControl(ScriptHost& h, JSContext* ctx, PortJs& p, const rp_control_queue& q) {
        if (JS_IsUndefined(p.value)) {
            if (!(make(&p.items, JS_NewArray(ctx)) && make(&p.pool, JS_NewArray(ctx)) && make(&p.value, JS_NewObject(ctx))))
                return false;
            JS_SetProperty(ctx, p.value, atom(h, kChanges), JS_DupValue(ctx, p.items));
        }
        JS_SetProperty(ctx, p.value, atom(h, kStart), JS_NewFloat64(ctx, q.start));
        for (std::uint32_t i = 0; i < q.count; ++i) {
            JSValue e = element(ctx, p, i);
            if (JS_IsException(e)) return false;
            JS_SetProperty(ctx, e, atom(h, kFrame), JS_NewUint32(ctx, q.changes[i].frame));
            JS_SetProperty(ctx, e, atom(h, kValue), JS_NewFloat64(ctx, q.changes[i].value));
            JS_SetPropertyUint32(ctx, p.items, i, e);
        }
        JS_SetProperty(ctx, p.items, atom(h, kLength), JS_NewUint32(ctx, q.count));
        return true;
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
            traceBegin(hostOf(ctx), DSP_SPAN_MARSHAL);
            const JsSpanFence fence(hostOf(ctx));
            const bool filled = fillMessages(hostOf(ctx), ctx, *p, *in->running->inputs[found->index].u.queue);
            fence.lift();
            traceEnd(hostOf(ctx));
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
            traceBegin(hostOf(ctx), DSP_SPAN_MARSHAL);
            const JsSpanFence fence(hostOf(ctx));
            const bool filled = fillControl(hostOf(ctx), ctx, *p, q);
            fence.lift();
            traceEnd(hostOf(ctx));
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

#ifdef RETROPLUG_PROFILE
    // ---- the trace window's globals, in the audio context of a profile build -------------------------------------
    // A span JS opens stands inside its process call's js-call span (jsCallBegin), and one opened outside a process
    // call records nothing, as an unarmed window records nothing: spanEnd closes only a span JS opened.
    static JSValue spanBegin(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        std::uint32_t label = 0;
        if (argc < 1) return JS_ThrowTypeError(ctx, "spanBegin takes a label");
        if (JS_ToUint32(ctx, &label, argv[0]) < 0) return JS_EXCEPTION;
        ScriptHost& h = hostOf(ctx);
        if (h.jsSpanFloor_ != kNoJsSpans) h.spanBegin(label);
        return JS_UNDEFINED;
    }
    static JSValue spanEnd(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        ScriptHost& h = hostOf(ctx);
        if (h.jsSpanFloor_ != kNoJsSpans && h.traceStack_.size() > h.jsSpanFloor_) h.spanEnd();
        return JS_UNDEFINED;
    }
    static JSValue traceName(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        double at = 0.0;
        if (argc < 2) return JS_ThrowTypeError(ctx, "traceName takes a label and a name");
        if (JS_ToFloat64(ctx, &at, argv[0]) < 0) return JS_EXCEPTION;
        // Read whole, so a label past the bound is refused rather than wrapped onto one below it.
        if (!(at >= 0.0 && at < kDspSpanLabels))
            return JS_ThrowRangeError(ctx, "a trace label is from 0 to %u", static_cast<unsigned>(kDspSpanLabels - 1));
        const auto label = static_cast<std::uint32_t>(at);
        const char* name = JS_ToCString(ctx, argv[1]);
        if (!name) return JS_EXCEPTION;
        hostOf(ctx).traceName(label, name);
        JS_FreeCString(ctx, name);
        return JS_UNDEFINED;
    }
    static JSValue traceArmed(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return JS_NewBool(ctx, hostOf(ctx).traceArmed());
    }
#endif
};

// ---- ScriptHost ------------------------------------------------------------------------------------------------

ScriptHost::ScriptHost(Binder binder, void* user) {
    package_ = rp_package{RP_ABI_VERSION, kPackageManifest, 0, nullptr};
    controlRt_ = JS_NewRuntime();
    control_ = JS_NewContext(controlRt_);
    const JSMallocFunctions counted{&Glue::jsCalloc, &Glue::jsMalloc, &Glue::jsFree, &Glue::jsRealloc, &Glue::jsUsable};
    audioRt_ = JS_NewRuntime2(&counted, &counters_);  // QuickJS copies the functions and keeps the opaque
    audio_ = JS_NewContext(audioRt_);
    JS_SetContextOpaque(audio_, this);
    // Both runtimes' JS stacks, inside the smallest thread that enters either (the class comment), and what the block
    // thread runs held to the call deadline.
    JS_SetMaxStackSize(controlRt_, kStackBytes);
    JS_SetMaxStackSize(audioRt_, kStackBytes);
    JS_SetInterruptHandler(audioRt_, &Glue::interrupt, this);

    // The Block object: one per instance, its methods on the class prototype, the instance its opaque.
    JS_NewClassID(audioRt_, &blockClass_);
    JSClassDef block{};
    block.class_name = "Block";
    JS_NewClass(audioRt_, blockClass_, &block);
    JSValue proto = JS_NewObject(audio_);
    JS_SetPropertyStr(audio_, proto, "messages", JS_NewCFunction(audio_, &Glue::messages, "messages", 1));
    JS_SetPropertyStr(audio_, proto, "send", JS_NewCFunction(audio_, &Glue::send, "send", 4));
    JS_SetPropertyStr(audio_, proto, "control", JS_NewCFunction(audio_, &Glue::control, "control", 1));
    JS_SetPropertyStr(audio_, proto, "emit", JS_NewCFunction(audio_, &Glue::emit, "emit", 3));
    JS_SetPropertyStr(audio_, proto, "audio", JS_NewCFunction(audio_, &Glue::audio, "audio", 1));
    JS_SetClassProto(audio_, blockClass_, proto);
    static_assert(kAtomCount <= std::tuple_size_v<decltype(atoms_)>, "atoms_ holds every block-path atom");
    for (std::size_t a = 0; a < kAtomCount; ++a) atoms_[a] = JS_NewAtom(audio_, kAtomNames[a]);

#ifdef RETROPLUG_PROFILE
    // The trace window's globals, which only a profile build binds (so a script tests `typeof spanBegin`), and the
    // fixed labels' names.
    JSValue global = JS_GetGlobalObject(audio_);
    JS_SetPropertyStr(audio_, global, "spanBegin", JS_NewCFunction(audio_, &Glue::spanBegin, "spanBegin", 1));
    JS_SetPropertyStr(audio_, global, "spanEnd", JS_NewCFunction(audio_, &Glue::spanEnd, "spanEnd", 0));
    JS_SetPropertyStr(audio_, global, "traceName", JS_NewCFunction(audio_, &Glue::traceName, "traceName", 2));
    JS_SetPropertyStr(audio_, global, "traceArmed", JS_NewCFunction(audio_, &Glue::traceArmed, "traceArmed", 0));
    JS_FreeValue(audio_, global);
    for (const auto& [label, name] : {std::pair{DSP_SPAN_KERNEL, "dsp-kernel"}, std::pair{DSP_SPAN_MARSHAL, "marshal"},
                                      std::pair{DSP_SPAN_JSCALL, "js-call"}, std::pair{DSP_SPAN_APU, "apu-render"},
                                      std::pair{DSP_SPAN_PUBLISH, "state-publish"}})
        traceName(label, name);
#endif

    if (binder) {
        binder(user, control_, false);
        binder(user, audio_, true);
    }
}

ScriptHost::~ScriptHost() {
    assert(!audioOwns_.load() && "destroy a ScriptHost only once no audio thread runs its blocks");
    releaseQueued();
    assert(live_.load() == 0 && "every shim goes before its script host");
    freeTypes();
    for (std::size_t a = 0; a < kAtomCount; ++a) JS_FreeAtom(audio_, atoms_[a]);
    JS_FreeContext(audio_);
    JS_FreeRuntime(audioRt_);
    JS_FreeContext(control_);
    JS_FreeRuntime(controlRt_);
}

ScriptHost::Chunk ScriptHost::embeddedBundle() { return {rp_script_bundle, rp_script_bundle_size}; }

bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error) {
    // Refused without touching what a first load registered: a registry may hold its types already.
    if (loaded_) {
        if (error) *error = "a script host loads one bundle";
        return false;
    }
    loaded_ = true;
    auto fail = [&](std::string why) {
        freeTypes();
        if (error) *error = std::move(why);
        return false;
    };
    for (const bool audioSide : {false, true}) {
        JSContext* ctx = audioSide ? audio_ : control_;
        JS_UpdateStackTop(audioSide ? audioRt_ : controlRt_);
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            JSValue code = JS_ReadObject(ctx, chunks[i].data(), chunks[i].size(), JS_READ_OBJ_BYTECODE);
            JSValue result = JS_IsException(code) ? JS_EXCEPTION : JS_EvalFunction(ctx, code);  // consumes `code`
            if (JS_IsException(result)) return fail("chunk " + std::to_string(i) + ": " + exceptionText(ctx, false));
            JS_FreeValue(ctx, result);
        }
        // What the bundle's top level queued runs now, in this context's runtime: no block has begun.
        runJobs(audioSide ? audioRt_ : controlRt_, kJobsPerBlock, [](JSContext* c) { clearException(c); });
    }
    // What the load leaves in cycles in the audio runtime, its top level's and the descriptors' below (about 144 KiB of
    // the production bundle's), goes at the first quiescent reclaim, rather than waiting for a block's threshold GC.
    garbage_ = true;
    // The definitions, in the same order in both contexts (the same bundle ran in each).
    JSValue lists[2];
    for (const bool audioSide : {false, true}) {
        JSContext* ctx = audioSide ? audio_ : control_;
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
    } owned{control_, audio_, lists};
    if (JS_IsUndefined(lists[0])) return true;  // a bundle that registers nothing
    std::int64_t count = 0;
    if (!JS_IsArray(lists[0]) || JS_GetLength(control_, lists[0], &count) < 0)
        return fail(std::string(kNodesGlobal) + " is not an array of node definitions");
    for (std::int64_t i = 0; i < count; ++i) {
        auto t = std::make_unique<Type>();
        t->host = this;
        t->control = JS_GetPropertyUint32(control_, lists[0], static_cast<std::uint32_t>(i));
        t->audio = JS_GetPropertyUint32(audio_, lists[1], static_cast<std::uint32_t>(i));
        types_.push_back(std::move(t));
        Type& type = *types_.back();
        const std::string at = "definition #" + std::to_string(i);
        JSValue id = JS_GetPropertyStr(control_, type.control, "id");
        const char* text = JS_IsString(id) ? JS_ToCString(control_, id) : nullptr;
        if (text) {
            type.id = text;
            JS_FreeCString(control_, text);
        }
        JS_FreeValue(control_, id);
        if (type.id.empty()) return fail(at + " has no id");
        JSValue audioId = JS_GetPropertyStr(audio_, type.audio, "id");
        const char* audioText = JS_IsString(audioId) ? JS_ToCString(audio_, audioId) : nullptr;
        const bool same = audioText && type.id == audioText;
        if (audioText) JS_FreeCString(audio_, audioText);
        JS_FreeValue(audio_, audioId);
        if (!same) return fail(at + " (" + type.id + ") is not the same in both contexts: its id differs");
        JSValue d = callMethod(control_, type.control, "descriptor", 0, nullptr);
        const bool described = !JS_IsException(d) && stringify(control_, d, type.descriptor);
        JS_FreeValue(control_, d);
        drainControlJobs();
        if (!described) return fail(type.id + ": its descriptor: " + exceptionText(control_, true));
        // The audio side's must be the same: the control side lays out and checks what the audio side then runs.
        std::string audioDescriptor;
        JSValue ad = callMethod(audio_, type.audio, "descriptor", 0, nullptr);
        const bool audioDescribed = !JS_IsException(ad) && stringify(audio_, ad, audioDescriptor);
        JS_FreeValue(audio_, ad);
        if (!audioDescribed) return fail(type.id + ": its descriptor, in the audio context: " + exceptionText(audio_, true));
        if (audioDescriptor != type.descriptor)
            return fail(at + " (" + type.id + ") is not the same in both contexts: its descriptor differs, so the bundle "
                             "defines it by which context it runs in");
        for (const auto& [flag, key] : {std::pair{&type.reshapes, "reshapes"}, std::pair{&type.faultOnThrow, "faultOnThrow"}}) {
            JSValue v = JS_GetPropertyStr(control_, type.control, key);
            *flag = JS_IsBool(v) && JS_ToBool(control_, v);
            JS_FreeValue(control_, v);
        }
        type.abi = rp_node_type{type.id.c_str(), &Glue::descriptor, &Glue::layoutFor, &Glue::queryMemory,
                                &Glue::create, &Glue::destroy, RP_TYPE_STATELESS_PREPARE, &type};
    }
    runJobs(audioRt_, kJobsPerBlock, [](JSContext* c) { clearException(c); });  // what the descriptors queued there
    typeList_.clear();
    for (const auto& t : types_) typeList_.push_back(&t->abi);
    package_ = rp_package{RP_ABI_VERSION, kPackageManifest, static_cast<std::uint32_t>(typeList_.size()), typeList_.data()};
    return true;
}

void ScriptHost::freeTypes() {
    for (auto& t : types_) {
        JS_FreeValue(control_, t->control);
        JS_FreeValue(audio_, t->audio);
    }
    types_.clear();
    typeList_.clear();
    package_ = rp_package{RP_ABI_VERSION, kPackageManifest, 0, nullptr};
}

void ScriptHost::setAudioThreadOwns(bool owns) {
    audioOwns_.store(owns, std::memory_order_release);
    if (owns) return;
    releaseQueued();
    collect();  // the runtime is this thread's again: what the audio thread's releases left in cycles goes now
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
        delete in;
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

// The promise jobs the block's JS queued (its process calls, and whatever the adoption before it ran), bounded so a
// reaction that queues another every time cannot hold the block, and held to one call deadline. A job that fails (a
// reaction's own throw only rejects its promise: this is the deadline, or JS out of memory) is counted as a node's
// throw is, and one the deadline stopped leaves the rest for the next block's end.
void ScriptHost::endBlock() {
    if (JS_IsJobPending(audioRt_)) runAudioJobs();
#ifdef RETROPLUG_PROFILE
    // The allocation window's per-block counts, the promise jobs included.
    const ScriptAllocations now = audioAllocations();
    ++windowBlocks_;
    maxBlockAllocs_ = std::max(maxBlockAllocs_, now.allocs - blockStart_.allocs);
    maxBlockBytes_ = std::max(maxBlockBytes_, now.bytes - blockStart_.bytes);
#endif
}

void ScriptHost::runAudioJobs() {
    JS_UpdateStackTop(audioRt_);
    const Glue::Deadline call(*this);
    for (int n = 0; n < kJobsPerBlock; ++n) {
        JSContext* ctx = nullptr;
        const int r = JS_ExecutePendingJob(audioRt_, &ctx);
        if (r == 0) return;
        if (r > 0) continue;
        Glue::recordThrow(*this, nullptr, "a promise job");
        if (interrupted_) break;
    }
    if (JS_IsJobPending(audioRt_)) jobsCutShort_.fetch_add(1, std::memory_order_relaxed);
}

void ScriptHost::drainControlJobs() {
    if (JS_IsJobPending(controlRt_)) runJobs(controlRt_, kJobsPerBlock, [](JSContext* ctx) { clearException(ctx); });
}

void ScriptHost::drainReleased() {
    Instance* in = nullptr;
    while (released_.tryPop(in)) delete in;
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
        std::fprintf(stderr, "[script] a block's promise jobs outran the %d a block runs; the rest ran at the next block's end "
                             "(said once; jobsCutShort() counts them)\n",
                     kJobsPerBlock);
        jobsLogged_ = true;
    }
    if (!firstThrowLogged_ && firstThrowState_.load(std::memory_order_acquire) == 2) {
        std::fprintf(stderr, "[script] %s (a throw is counted, and the node runs on unless it faults on one; later ones are "
                             "counted only)\n",
                     firstThrow_);
        firstThrowLogged_ = true;
    }
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
        delete in;
    }
    for (Instance* b = backlogHead_; b;) {
        Instance* next = b->backlogNext;
        releaseJs(*b);
        delete b;
        b = next;
    }
    backlogHead_ = backlogTail_ = nullptr;
    while (released_.tryPop(in)) delete in;
}

// Quiescent only, and never by a block: a GC's cost is the whole heap's, which a block cannot afford. QuickJS's own
// threshold GC may still run inside one when JS allocates past it; this is what keeps a released instance's cycles
// from waiting for that.
void ScriptHost::collect() {
    if (!garbage_) return;
    garbage_ = false;
    JS_UpdateStackTop(audioRt_);
    JS_RunGC(audioRt_);
}

void ScriptHost::releaseJs(Instance& in) {
    JSContext* ctx = audio_;
    JS_UpdateStackTop(audioRt_);
    if (in.layout) Glue::freeLayoutJs(ctx, *in.layout);
    if (!JS_IsUndefined(in.block)) JS_SetOpaque(in.block, nullptr);  // a block JS kept is no longer this instance's
    for (JSValue* v : {&in.object, &in.process, &in.onSettings, &in.portsChanged, &in.activate, &in.block, &in.transport}) {
        JS_FreeValue(ctx, *v);
        *v = JS_UNDEFINED;
    }
    if (in.state == Instance::State::Live) live_.fetch_sub(1, std::memory_order_relaxed);
    in.state = Instance::State::Released;
    garbage_ = true;
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
    s.blockCount = windowBlocks_;
    s.maxBlockAllocCalls = maxBlockAllocs_;
    s.maxBlockAllocBytes = maxBlockBytes_;
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
    windowBlocks_ = maxBlockAllocs_ = maxBlockBytes_ = 0;
    // QuickJS checks its threshold only as it makes an object, so SIZE_MAX means a collection never starts by itself.
    if (disableAutoGc) JS_SetGCThreshold(audioRt_, static_cast<std::size_t>(-1));
    return true;
}

DspGcResult ScriptHost::runGc() {
    if (refuseWhileOwned("a GC pass")) return {};  // enabled false
    JS_UpdateStackTop(audioRt_);
    const std::int64_t before = counters_.liveBytes.load(std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    JS_RunGC(audioRt_);
    DspGcResult r;
    r.enabled = true;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.freedBytes = before - counters_.liveBytes.load(std::memory_order_relaxed);  // the cycles collected
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
    lastProcessEnd_ = 0.0;
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
