#include "script/MqjsHost.hpp"

#include <atomic>
#include <cstring>
#include <new>

#include "graph/Queue.hpp"
#include "graph/Text.hpp"
#include "graph/msgpack_reader.hpp"
#include "script/rp_mqjs.h"

#if defined(RP_DEVICE) && defined(__GLIBCXX__)
// What a device build of the host must not carry, as graph/node.hpp checks a node's translation unit and the plan
// loader its own: the standard library's heap and exception headers (each name is its libstdc++ include guard). The
// device image's link trap is the guarantee; this is the early, readable error.
#if defined(_GLIBCXX_STRING) || defined(_GLIBCXX_VECTOR) || defined(_GLIBCXX_MAP) || defined(_GLIBCXX_OPTIONAL) || \
    defined(_GLIBCXX_STDEXCEPT) || defined(__EXCEPTION__) || defined(_GLIBCXX_IOSTREAM) ||                      \
    defined(_GLIBCXX_FUNCTIONAL) || defined(_GLIBCXX_MEMORY)
#error "the MicroQuickJS host's device build includes a heap or exception header"
#endif
#endif

#ifndef RP_DEVICE
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "graph/JsonToMsgpack.hpp"
#include "graph/ThreadClock.hpp"
#include "graph/node_desc.hpp"  // rp.sdk.settings-json/1
#endif

namespace rp::mqjs {

static_assert(kMaxReentries == RP_MQJS_MAX_REENTRIES, "MqjsHost.hpp's kMaxReentries is rp_mqjs.h's figure");

namespace {

// How many packs and types one build registers: the device's is fixed storage, so it keeps what a box carries.
#ifdef RP_DEVICE
#ifndef RP_MQJS_MAX_PACKS
#define RP_MQJS_MAX_PACKS 4
#endif
#ifndef RP_MQJS_MAX_TYPES
#define RP_MQJS_MAX_TYPES 16
#endif
#else
#define RP_MQJS_MAX_PACKS 32
#define RP_MQJS_MAX_TYPES 64
#endif
constexpr std::uint32_t kMaxPacks = RP_MQJS_MAX_PACKS;
constexpr std::uint32_t kMaxTypes = RP_MQJS_MAX_TYPES;

// A message's and a change's object name their node's slot, their port and their place in the port's list, in 6, 6 and
// 20 bits: a queue holds at most 2^20 messages (Descriptor.hpp's kMaxQueueMessages).
constexpr std::uint32_t kItemBits = 20;
constexpr std::uint32_t kBuckets = kItemBits + 2;  // the empty array, then one per power of two up to 2^20

// ---- what a context points to -------------------------------------------------------------------------------------

enum class Kind : std::uint8_t { Pack, Control, Script };

// The state every context the host makes has as its opaque: what the standard library's dispatchers (rp_mqjs.h),
// the interrupt handler and the log function find through it. `api` first, as rp_mqjs_table.c reads it.
struct ContextState {
    const rp_mqjs_api* api = nullptr;
    Kind               kind = Kind::Pack;
    JSContext*         ctx = nullptr;
    // The deadline: armed by the outermost entry into the context (Armed), polled by the interrupt handler.
    std::uint64_t (*clock)(void* ctx) = nullptr;
    void*         clockCtx = nullptr;
    std::uint64_t start = 0;
    std::uint64_t limit = kHostClockDeadlineMicros;
    // A host's clock gives a block's entry kHostClockMessageMicros more for each message it hands the node (perMessage:
    // the entry's `extra`, set by process); every other entry, and the desktop's CPU-time default, has none.
    std::uint64_t perMessage = 0;
    std::uint64_t extra = 0;
    // The polls (interrupt): the clock at the last one (or at the entry's start), and how many calls and branches the
    // engine runs before the next.
    std::uint64_t lastPoll = 0;
    std::uint32_t poll = 0;
    std::uint32_t depth = 0;
    bool          timed = false;    // the entry's start was read: an entry the clock could not time runs untimed
    bool          expired = false;  // this entry ran past its deadline
    // console.log's bytes, and what MicroQuickJS's own printing writes (JS_PrintValueF).
    void (*log)(void* ctx, const char* text, std::size_t size) = nullptr;
    void* logCtx = nullptr;
};

struct Pack;
struct TypeInfo {
    Pack*         pack = nullptr;
    std::uint32_t index = 0;
};

#ifndef RP_DEVICE
// The desktop's control context for a pack (the header comment): one per process, entered under its mutex.
struct Control {
    std::mutex                    mutex;
    std::unique_ptr<std::uint64_t[]> memory;
    std::unique_ptr<std::uint64_t[]> bytecode;  // the context's own copy (rp_mqjs_pack.h)
    ContextState                  state;
    JSGCRef                       defs{};
    std::vector<std::string>      descriptors;
};
// The control context's size and its calls' deadline (the thread's CPU time): it reads definitions and checks settings,
// on the control thread, where nothing waits on a block.
constexpr std::size_t   kControlBytes = std::size_t{1} << 20;
constexpr std::uint64_t kControlDeadlineMicros = 2000000;
#endif

// How a registration sizes a pack's context (Pack::sizing).
enum class Sizing : std::uint8_t { Device, Check, Desktop };

// One pack this build registered: its rp_package and its types.
struct Pack {
    const rp_mqjs_pack_source* source = nullptr;
    rp_package                 package{};
    rp_package_memory          memory{};
    rp_node_type               types[kMaxTypes]{};
    const rp_node_type*        typeList[kMaxTypes]{};
    TypeInfo                   infos[kMaxTypes]{};
    char                       manifest[96]{};
    // How its registration sizes its context from the manifest's heapBytes (MqjsHost.hpp), read at each query: a
    // device build's kDeviceHalves halves of it, the toolchain check's kCheckHalves, the desktop's
    // desktopContextBytes. The source's heapBytes is read every time, so a tool may move it between setups (the
    // runner's search for the least heap).
    Sizing                     sizing = Sizing::Device;
#ifndef RP_DEVICE
    Control*                   control = nullptr;
#endif
};

std::uint32_t heapOf(const Pack& pack) {
    const rp_mqjs_pack_source& source = *pack.source;
    switch (pack.sizing) {
#ifndef RP_DEVICE
        case Sizing::Desktop: return static_cast<std::uint32_t>(desktopContextBytes(source));
        case Sizing::Check: return static_cast<std::uint32_t>(std::uint64_t{source.heap_bytes} * kCheckHalves / 2);
#endif
        default: return static_cast<std::uint32_t>(std::uint64_t{source.heap_bytes} * kDeviceHalves / 2);
    }
}

Pack          gPacks[kMaxPacks];
std::uint32_t gPackCount = 0;
char          gRefusal[200] = {};  // why the last package() answered null (packageRefusal)
// The longest pack name a manifest's id holds ({"id":"rp.<name>.script",...} in Pack::manifest).
constexpr std::size_t kMaxNameLength = 48;

// ---- a pack's state in its package memory --------------------------------------------------------------------------

// The pack's root object's slots, its instance records' slots, and a port's state's slots.
enum : std::uint32_t { kRootDefs, kRootInstances, kRootTransport, kRootLength };
enum : std::uint32_t {
    kRecInstance, kRecProcess, kRecPortsChanged, kRecOnSettings, kRecActivate, kRecBlock, kRecPortIds, kRecPortState,
    kRecLength
};
// A port's state: its head (a queue's empty Uint8Array, a control stream's object), its pool of message or change
// objects, how many the pool holds, a queue's byte buffer for the payloads, then its arrays by bucket.
enum : std::uint32_t {
    kStateHead, kStatePool, kStatePooled, kStateBuffer, kStateBuckets, kStateLength = kStateBuckets + kBuckets
};

struct Node;

// The state at the start of a pack's package memory (its layout is below).
struct PackState {
    ContextState  base;  // first: the context's opaque is this
    const Pack*   pack = nullptr;
    JSGCRef       root{};  // [defs, instances, transport]
    // The thread that runs blocks: the block a process call is running, its node and its node's slot.
    const rp_process*  block = nullptr;
    Node*              running = nullptr;
    std::int32_t       runningSlot = -1;
    bool               dead = false;
    char               deadReason[160] = {};
    std::uint8_t       freeSlots[kMaxInstances] = {};
    std::uint32_t      freeCount = 0;
    // Slots the control thread released (destroy), which the block thread frees at its next entry.
    std::atomic<std::uint32_t> releaseHead{0};
    std::atomic<std::uint32_t> releaseTail{0};
    std::uint8_t               releases[kMaxInstances] = {};
    std::uint32_t      contextBytes = 0;
    std::uint8_t*      contextMemory = nullptr;
    bool               configured = false;  // the host's clock and console taken (packMemory)
#ifndef RP_DEVICE
    std::uint32_t      nodes = 0;  // the control thread's: nodes made under this memory and not yet destroyed
#endif
};

// The pack's memory: the state, then the context's own copy of the bytecode (rp_mqjs_pack.h), then the context, each
// at a multiple of 8 bytes. At 32 bits the state takes kBoxStateBytes, whatever its size, so that what a 64-bit build
// answers for the box (boxPackageBytes, query_target) is what the box's own build takes.
constexpr std::size_t aligned8(std::size_t n) { return (n + 7) & ~std::size_t{7}; }
#if INTPTR_MAX >= INT64_MAX
constexpr std::size_t kStateBytes = aligned8(sizeof(PackState));
#else
static_assert(sizeof(PackState) <= kBoxStateBytes, "a pack's state at 32 bits is past what the box's sizing allows");
constexpr std::size_t kStateBytes = kBoxStateBytes;
#endif
std::size_t packBytes(const Pack& pack) { return kStateBytes + aligned8(pack.source->bytecode_size) + heapOf(pack); }

PackState* stateAt(void* memory) { return static_cast<PackState*>(memory); }

// ---- a node ---------------------------------------------------------------------------------------------------------

enum class State : std::uint8_t { Pending, Live, Faulted };
enum Side : std::uint32_t { kInput = 0, kOutput = 1, kParam = 2 };

struct Node {
    rp_node         base;  // first: an rp_node* is a Node*
    const TypeInfo* type = nullptr;
    PackState*      pack = nullptr;
    std::int32_t    slot = -1;
    State           state = State::Pending;
    std::uint8_t    faultKind = 0;      // RP_SCRIPT_FAULT_*
    bool            activated = false;  // activate ran on a live instance: its hook runs at the next call
    // Its definition says idle: "skip" (defineNode.ts's host contract), read as its instance is made; whether its
    // process has run, and where each control input and param stood at the end of its last call's block (its block's
    // control inputs in order, then its params), which is where the next block of the same plan starts it: from then
    // on a block that brings it nothing, with every stream starting there, does not call it, whichever plan's step
    // it comes from (process says why).
    bool            idle = false;
    bool            ran = false;
    double          ends[kMaxPorts] = {};
    double          sampleRate = 48000.0;
    std::uint32_t   maxFrames = 1024;
    char            fault[160] = {};
    // Its ports and params by ordinal, as its layout gave them when its instance was made (the thread that runs
    // blocks): side | kind << 2 | index << 8 (index on that side of rp_process). Native, so a block's checks read
    // integers.
    std::uint32_t   portCount = 0;
    std::uint32_t   metas[kMaxPorts] = {};
    // Each input's and param's pool, made (a bit a port) to this capacity (ensurePool): a block whose queues hold no
    // more passes the port by, with no JS read.
    std::uint64_t   readied = 0;
    std::uint32_t   pooled[kMaxPorts] = {};
    // The port names its blocks have asked for that never move (rp_mqjs_is_fixed: a constant of the pack's bytecode,
    // `b.send("out", ...)`), each with its port's ordinal: found by the value alone, where any other name is compared
    // as text (findPort).
    static constexpr std::uint32_t kNames = 4;
    JSValue         names[kNames] = {};
    std::uint8_t    nameOrdinals[kNames] = {};
    std::uint32_t   nameCount = 0;
#ifndef RP_DEVICE
    std::string*    settings = nullptr;  // as the definition parsed them, JSON
    bool            settingsChanged = false;
    std::string     layout;               // as it was created, which prepare_settings compares against
#else
    std::uint32_t   settingsSize = 0;     // the msgpack, after the node
#endif
};

Node* as(rp_node* n) { return reinterpret_cast<Node*>(n); }

#ifdef RP_DEVICE
const std::uint8_t* settingsBytes(const Node& n) { return reinterpret_cast<const std::uint8_t*>(&n + 1); }
#endif

// ---- small things ---------------------------------------------------------------------------------------------------

using graph::Text;

void writeError(rp_error* err, const char* a, const char* b = "", const char* c = "") {
    if (err) Text(err->message, sizeof err->message) << a << b << c;
}

// A value where MicroQuickJS's collector finds it, for this object's life: the collector compacts, so a value held
// across any call that may allocate must be one of these. JS_PushGCRef's list is last in, first out, as scopes are.
class Rooted {
public:
    explicit Rooted(JSContext* ctx, JSValue v = JS_UNDEFINED) : ctx_(ctx) {
        slot_ = JS_PushGCRef(ctx, &ref_);
        *slot_ = v;
    }
    ~Rooted() { JS_PopGCRef(ctx_, &ref_); }
    Rooted(const Rooted&) = delete;
    Rooted& operator=(const Rooted&) = delete;
    JSValue  get() const { return *slot_; }
    void     set(JSValue v) { *slot_ = v; }
    // Where the value is kept, which the collector updates: what an engine call that writes back through a JSValue*
    // (rp_mqjs_set_length) takes, never a copy of the value.
    JSValue* slot() { return slot_; }

private:
    JSContext* ctx_;
    JSGCRef    ref_{};
    JSValue*   slot_;
};

JSValue at(JSContext* ctx, JSValue array, std::uint32_t i) { return rp_mqjs_array_get(ctx, array, i); }
bool    isObject(JSContext* ctx, JSValue v) { return JS_GetClassID(ctx, v) >= 0; }
bool    isFunction(JSContext* ctx, JSValue v) { return JS_IsFunction(ctx, v) != 0; }

// The string `v` is, compared with `s`, with no allocation (a value that is not a string is not equal).
bool equals(JSContext* ctx, JSValue v, const char* s) {
    if (!JS_IsString(ctx, v)) return false;
    JSCStringBuf buf;
    std::size_t  n = 0;
    const char*  p = JS_ToCStringLen(ctx, &n, v, &buf);
    return p && n == std::strlen(s) && std::memcmp(p, s, n) == 0;
}

// A value the host read of what a node handed it (a property, through any getter) as a uint32: false, with the
// exception pending, when the read threw (a getter's throw, the deadline's interrupt inside it) or the conversion did.
// JS_ToUint32 alone takes a read's JS_EXCEPTION for a value, converts it to 0 and reports success.
bool toUint32(JSContext* ctx, JSValue v, std::uint32_t* out) {
    return !JS_IsException(v) && JS_ToUint32(ctx, out, v) == 0;
}

bool sameString(JSContext* ctx, JSValue a, JSValue b) {
    if (a == b) return true;
    if (!JS_IsString(ctx, a) || !JS_IsString(ctx, b)) return false;
    JSCStringBuf ba, bb;
    std::size_t  na = 0, nb = 0;
    const char*  pa = JS_ToCStringLen(ctx, &na, a, &ba);
    const char*  pb = JS_ToCStringLen(ctx, &nb, b, &bb);
    return pa && pb && na == nb && std::memcmp(pa, pb, na) == 0;
}

// `v` as text into `out` (its toString, so an error reads "TypeError: ..."), and where it was thrown from (an error's
// first stack line), cut to fit.
void describe(JSContext* ctx, JSValue v, Text& out) {
    if (JS_IsNull(v)) {
        out << "null";
        return;
    }
    Rooted e(ctx, v);
    JSCStringBuf buf;
    std::size_t  n = 0;
    const char*  s = JS_ToCStringLen(ctx, &n, e.get(), &buf);
    if (!s) {
        JS_GetException(ctx);
        out << "a value that cannot be printed";
        return;
    }
    out.put(s, n);
    const JSValue stack = rp_mqjs_error_field(ctx, e.get(), 1);
    if (!JS_IsString(ctx, stack)) return;
    const char* t = JS_ToCStringLen(ctx, &n, stack, &buf);
    if (!t) return;
    // Its first line, "at <function> (<file>:<line>:<column>)" with the function's name empty for an anonymous one,
    // as "(at <file>:<line>:<column>)" or "(at <function>, <file>:...)".
    std::size_t i = 0;
    while (i < n && (t[i] == ' ' || t[i] == '\n')) ++i;
    std::size_t j = i;
    while (j < n && t[j] != '\n') ++j;
    if (j - i > 3 && std::memcmp(t + i, "at ", 3) == 0) i += 3;
    while (i < j && t[i] == ' ') ++i;
    if (i >= j) return;
    out << " (at ";
    const char* open = static_cast<const char*>(std::memchr(t + i, '(', j - i));
    if (open && t[j - 1] == ')') {
        std::size_t name = static_cast<std::size_t>(open - (t + i));
        while (name > 0 && t[i + name - 1] == ' ') --name;
        if (name > 0) out.put(t + i, name) << ", ";
        out.put(open + 1, static_cast<std::size_t>(t + j - 1 - (open + 1)));
    } else {
        out.put(t + i, j - i);
    }
    out << ")";
}

// Out of memory, as MicroQuickJS throws it: an InternalError "out of memory", or null where even that could not be
// made. Read with no allocation, in a heap that has just run out.
bool outOfMemory(JSContext* ctx, JSValue e) {
    return JS_IsNull(e) || equals(ctx, rp_mqjs_error_field(ctx, e, 0), "out of memory");
}

// ---- the clock and the deadline -------------------------------------------------------------------------------------

#ifndef RP_DEVICE
using graph::threadCpuMicros;
#endif

// An entry's first poll of its deadline: after its deadline in microseconds over kFirstPollDivisor of the engine's
// calls and branches, and at least kLeastPoll (MqjsHost.hpp says why).
std::uint32_t firstPoll(std::uint64_t limit) {
    const std::uint64_t first = limit / kFirstPollDivisor;
    return first < kLeastPoll ? kLeastPoll : first > kMostPoll ? kMostPoll : static_cast<std::uint32_t>(first);
}

// The poll after one that found the deadline not yet past, `since` microseconds after the poll before it, `poll` calls
// and branches ago, with `left` microseconds left: twice as far, to kMostPoll, so a node that finishes its entry in
// microseconds reads the clock a few times at most; but no further than the last interval's rate says fits in half of
// what is left, so the polls close in on the deadline and the one past it lands about one call's or branch's work (one
// builtin's) beyond it.
std::uint32_t nextPoll(std::uint32_t poll, std::uint64_t since, std::uint64_t left) {
    std::uint64_t next = std::uint64_t{poll} * 2;
    if (next > kMostPoll) next = kMostPoll;
    // Multiplied out first, so a poll far from its deadline divides nothing (a 64-bit division is a library call on
    // the box's M33).
    if (since > 0 && next * since > left / 2 * poll) {
        const std::uint64_t fits = left / 2 * poll / since;
        next = fits < 1 ? 1 : fits;
    }
    return static_cast<std::uint32_t>(next);
}

// The engine's interrupt handler, at each poll of a timed entry's deadline: past it, the entry has expired (and the
// next poll comes at once, so an interrupt a builtin dropped is thrown again at the interpreter's next call or branch);
// before it, the next poll's distance is set (nextPoll). A reading of 0, or one earlier than the entry's start, is no
// reading: the call goes on untimed rather than past its deadline at once, polled as before.
int interrupt(JSContext* ctx, void* opaque) {
    auto* s = static_cast<ContextState*>(opaque);
    if (!s->timed || s->depth == 0) return 0;
    const std::uint64_t now = s->clock(s->clockCtx);
    if (now == 0 || now < s->start) {
        rp_mqjs_set_poll_interval(ctx, static_cast<int>(s->poll));
        return 0;
    }
    const std::uint64_t budget = s->limit + s->extra, spent = now - s->start;
    if (spent > budget) {
        s->expired = true;
        rp_mqjs_set_poll_interval(ctx, 1);
        return 1;
    }
    s->poll = nextPoll(s->poll, now > s->lastPoll ? now - s->lastPoll : 0, budget - spent);
    s->lastPoll = now;
    rp_mqjs_set_poll_interval(ctx, static_cast<int>(s->poll));
    return 0;
}

// The deadline over all one entry into a context runs (the outermost arms it; one inside it changes nothing): every
// JS call, every getter the host reads and every value it prints, with the entry's expired mark cleared as it starts,
// so a fault an earlier entry ran into is never this one's.
class Armed {
public:
    explicit Armed(ContextState& s) : s_(s), outermost_(s.depth == 0) {
        ++s_.depth;
        if (!outermost_) return;
        s_.expired = false;
        s_.extra = 0;
        restart();
    }
    ~Armed() { --s_.depth; }
    // The deadline started again from now (the outermost entry's alone): for what the host does before the node's own
    // calls that the plan bounds and the node does not (a port's pools made at its first block).
    void restart() {
        if (!outermost_) return;
        s_.start = s_.clock ? s_.clock(s_.clockCtx) : 0;
        s_.timed = s_.start != 0;
        // The first poll soon after (interrupt): an untimed entry polls as the engine does.
        if (s_.timed && s_.ctx) {
            s_.lastPoll = s_.start;
            s_.poll = firstPoll(s_.limit);
            rp_mqjs_set_poll_interval(s_.ctx, static_cast<int>(s_.poll));
        }
    }
    // The outermost entry's deadline lengthened by `micros` (a block's messages, process).
    void allow(std::uint64_t micros) {
        if (outermost_) s_.extra = micros;
    }
    Armed(const Armed&) = delete;
    Armed& operator=(const Armed&) = delete;

private:
    ContextState& s_;
    bool          outermost_;
};

// fn.call(self, args...), under the deadline (armed here when no entry has armed it). The result, or JS_EXCEPTION with
// the exception pending.
JSValue invoke(ContextState& s, const Rooted& fn, const Rooted& self, const Rooted* a0 = nullptr,
               const Rooted* a1 = nullptr) {
    JSContext* ctx = s.ctx;
    const Armed armed(s);
    const int argc = a1 ? 2 : a0 ? 1 : 0;
    if (JS_StackCheck(ctx, static_cast<std::uint32_t>(argc + 2))) return JS_EXCEPTION;
    if (a1) JS_PushArg(ctx, a1->get());
    if (a0) JS_PushArg(ctx, a0->get());
    JS_PushArg(ctx, fn.get());
    JS_PushArg(ctx, self.get());
    return JS_Call(ctx, argc);
}

// ---- console.log ----------------------------------------------------------------------------------------------------

void writeLog(void* opaque, const void* text, std::size_t size) {
    auto* s = static_cast<ContextState*>(opaque);
    if (s && s->log) s->log(s->logCtx, static_cast<const char*>(text), size);
}

// As upstream's REPL prints (mqjs.c's js_print): each argument, a string as it is and any other value as the engine
// prints it, separated by spaces, then a newline.
JSValue consoleLog(JSContext* ctx, ContextState& s, int argc, JSValue* argv) {
    if (!s.log) return JS_UNDEFINED;
    for (int i = 0; i < argc; ++i) {
        if (i) writeLog(&s, " ", 1);
        if (JS_IsString(ctx, argv[i])) {
            JSCStringBuf buf;
            std::size_t  n = 0;
            const char*  p = JS_ToCStringLen(ctx, &n, argv[i], &buf);
            if (p) writeLog(&s, p, n);
        } else {
            JS_PrintValueF(ctx, argv[i], JS_DUMP_LONG);
        }
    }
    writeLog(&s, "\n", 1);
    return JS_UNDEFINED;
}

// ---- the bytecode ---------------------------------------------------------------------------------------------------

// A fresh context over the standard library in `memory`, `bytecode` (one of the pack's bundles) copied to `code` (its
// own, which lives as long as the context), relocated there and run: its definitions are then in the global
// __rp_scriptNodes. Null, with `why` filled, when any of it fails (the context is freed then). The copy is relocated in
// the context before it loads anything: relocation finds the library's atoms in the context's tables, and a context
// refuses bytecode once an atom of its own exists.
JSContext* loadPack(ContextState& s, const std::uint8_t* bytecode, std::uint32_t size, void* memory,
                    std::size_t bytes, std::uint8_t* code, Text& why) {
    if (bytes < kLeastContextBytes) {
        why << "its heap of " << bytes << " bytes is under the least context the engine sets itself up in, "
            << std::uint64_t{kLeastContextBytes} << " bytes at this width";
        return nullptr;
    }
    JSContext* ctx = JS_NewContext(memory, bytes, &rp_mqjs_stdlib);
    s.ctx = ctx;
    JS_SetContextOpaque(ctx, &s);
    JS_SetInterruptHandler(ctx, &interrupt);
    JS_SetLogFunc(ctx, &writeLog);
    std::memcpy(code, bytecode, size);
    if (JS_RelocateBytecode(ctx, code, size) != 0) {
        why << "its bytecode could not be relocated (not MicroQuickJS bytecode of this width)";
        JS_FreeContext(ctx);
        return nullptr;
    }
    rp_mqjs_keep_bytecode_names(code, size);
    bool ran = false;
    {
        Rooted main(ctx, JS_LoadBytecode(ctx, code));
        if (!JS_IsException(main.get())) {
            const Armed armed(s);
            ran = !JS_IsException(JS_Run(ctx, main.get())) && !s.expired;
        }
        if (s.expired) {
            // Said as an overrun whatever the run returned (process says why).
            JS_GetException(ctx);
            why << "running its bytecode ran past its deadline of " << s.limit << " us";
        } else if (!ran) {
            why << "running its bytecode threw: ";
            describe(ctx, JS_GetException(ctx), why);
        }
    }
    // Every value this rooted is unrooted by now: a context is freed with nothing of the caller's still on its list.
    if (!ran) {
        JS_FreeContext(ctx);
        return nullptr;
    }
    // A node's context is held to the box's figure from here on (rp_mqjs.h's RP_MQJS_MAX_REENTRIES): every call into
    // it after its load, its nodes' create, hooks and blocks. Its load (the pack's definitions, made once, at setup)
    // and the desktop's control context, which reads definitions, descriptors and settings and never runs on a block,
    // keep the engine's own, as the SDK's definitions nest deeper.
    if (s.kind == Kind::Pack) rp_mqjs_limit_reentries(ctx, RP_MQJS_MAX_REENTRIES);
    return ctx;
}

// The definitions a pack's bytecode registered, checked against the ids its build recorded: JS_EXCEPTION (nothing
// pending) with `why` filled when they differ.
JSValue definitions(JSContext* ctx, const rp_mqjs_pack_source& source, Text& why) {
    Rooted defs(ctx, JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "__rp_scriptNodes"));
    if (!JS_IsArray(ctx, defs.get())) {
        why << "its bytecode registers no node types (registerScriptNodes)";
        return JS_EXCEPTION;
    }
    std::uint32_t n = 0;
    if (JS_ToUint32(ctx, &n, JS_GetPropertyStr(ctx, defs.get(), "length")) || n != source.type_count) {
        why << "its bytecode registers " << std::uint64_t{n} << " node types, its build recorded "
            << std::uint64_t{source.type_count};
        return JS_EXCEPTION;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        const JSValue id = JS_GetPropertyStr(ctx, at(ctx, defs.get(), i), "id");
        if (!equals(ctx, id, source.type_ids[i])) {
            why << "its node type #" << std::uint64_t{i} << " is not " << source.type_ids[i]
                << ", the id its build recorded";
            return JS_EXCEPTION;
        }
    }
    return defs.get();
}

// ---- settings as JS -------------------------------------------------------------------------------------------------

constexpr int kMaxSettingsDepth = 8;

// The msgpack value at the reader's cursor, as JS: maps as objects (in their order), arrays, strings, numbers, bools
// and nil as null. JS_EXCEPTION, with an error pending, for a blob that is malformed or deeper than kMaxSettingsDepth.
JSValue decodeMsgpack(JSContext* ctx, graph::msgpack::Reader& r, int depth) {
    using graph::msgpack::Type;
    switch (r.peek()) {
        case Type::Nil: r.readNil(); return JS_NULL;
        case Type::Bool: {
            bool b = false;
            r.readBool(b);
            return JS_NewBool(b);
        }
        case Type::Int: {
            graph::msgpack::Int i;
            r.readInt(i);
            if (i.negative) return JS_NewInt64(ctx, i.i);
            return i.u <= static_cast<std::uint64_t>(INT64_MAX) ? JS_NewInt64(ctx, static_cast<std::int64_t>(i.u))
                                                                 : JS_NewFloat64(ctx, static_cast<double>(i.u));
        }
        case Type::Float: {
            double d = 0.0;
            r.readFloat(d);
            return JS_NewFloat64(ctx, d);
        }
        case Type::Str: {
            std::string_view s;
            r.readStr(s);
            return JS_NewStringLen(ctx, s.data(), s.size());
        }
        case Type::Array:
        case Type::Map: break;
        default: return JS_ThrowTypeError(ctx, "the settings hold a value a script node cannot read");
    }
    if (depth >= kMaxSettingsDepth) return JS_ThrowTypeError(ctx, "the settings nest deeper than a script node reads");
    std::uint32_t n = 0;
    if (r.peek() == Type::Array) {
        r.readArray(n);
        Rooted list(ctx, JS_NewArray(ctx, 0));
        if (JS_IsException(list.get())) return JS_EXCEPTION;
        for (std::uint32_t i = 0; i < n; ++i) {
            const JSValue v = decodeMsgpack(ctx, r, depth + 1);
            if (JS_IsException(v) || JS_IsException(JS_SetPropertyUint32(ctx, list.get(), i, v))) return JS_EXCEPTION;
        }
        return list.get();
    }
    r.readMap(n);
    Rooted object(ctx, JS_NewObject(ctx));
    if (JS_IsException(object.get())) return JS_EXCEPTION;
    for (std::uint32_t i = 0; i < n; ++i) {
        std::string_view key;
        if (!r.readStr(key)) return JS_ThrowTypeError(ctx, "the settings name a field by something not a string");
        char name[64];
        if (key.size() >= sizeof name) return JS_ThrowTypeError(ctx, "the settings name a field longer than 63 bytes");
        std::memcpy(name, key.data(), key.size());
        name[key.size()] = '\0';
        const JSValue v = decodeMsgpack(ctx, r, depth + 1);
        if (JS_IsException(v) || JS_IsException(JS_SetPropertyStr(ctx, object.get(), name, v))) return JS_EXCEPTION;
    }
    return object.get();
}

JSValue decodeSettings(JSContext* ctx, const std::uint8_t* data, std::size_t size) {
    if (size == 0) return JS_NewObject(ctx);
    graph::msgpack::Reader r(data, size);
    const JSValue v = decodeMsgpack(ctx, r, 0);
    if (JS_IsException(v)) return v;
    if (!r.atEnd() || r.truncated()) return JS_ThrowTypeError(ctx, "the settings are not one msgpack value");
    return v;
}

#ifndef RP_DEVICE
JSValue parseJson(JSContext* ctx, const std::string& text) {
    return JS_Parse(ctx, text.c_str(), text.size(), "<settings>", JS_EVAL_JSON);
}

// JSON.stringify(v) into `out`; false, with the exception pending, when it throws.
bool stringify(ContextState& s, const Rooted& v, std::string& out) {
    JSContext* ctx = s.ctx;
    Rooted json(ctx, JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "JSON"));
    Rooted fn(ctx, JS_GetPropertyStr(ctx, json.get(), "stringify"));
    const JSValue r = invoke(s, fn, json, &v);
    if (JS_IsException(r)) return false;
    JSCStringBuf buf;
    std::size_t  n = 0;
    const char*  p = JS_ToCStringLen(ctx, &n, r, &buf);
    if (!p) return false;
    out.assign(p, n);
    return true;
}
#endif

// ---- faults ---------------------------------------------------------------------------------------------------------

bool fault(Node& node, std::uint8_t kind, const char* what, const char* more = "") {
    if (node.state != State::Faulted) {
        Text(node.fault, sizeof node.fault) << what << more;
        node.state = State::Faulted;
        node.faultKind = kind;
    }
    return false;
}

// The deadline's fault: `during` (what was called, "process") ran past it. Said as an overrun whatever ended the call,
// since the engine's own words for one are only "interrupted", and a builtin or a getter the host read may have dropped
// even those on the way out; an exception the entry left pending is taken.
bool deadlineFault(Node& node, const char* during) {
    const PackState& pk = *node.pack;
    JS_GetException(pk.base.ctx);
    char buf[96];
    Text(buf, sizeof buf) << " ran past its deadline of " << pk.base.limit + pk.base.extra << " us";
    return fault(node, RP_SCRIPT_FAULT_DEADLINE, during, buf);
}

// A call into the pack that ended in an exception, which this takes: the node faults, and an out-of-memory error kills
// the pack's context first. `during` names what was called ("process").
bool faultFromException(Node& node, const char* during) {
    PackState& pk = *node.pack;
    JSContext* ctx = pk.base.ctx;
    if (pk.base.expired) return deadlineFault(node, during);
    const JSValue e = JS_GetException(ctx);
    if (outOfMemory(ctx, e)) {
        if (!pk.dead) {
            pk.dead = true;
            Text(pk.deadReason, sizeof pk.deadReason)
                << "the pack's heap of " << std::uint64_t{pk.contextBytes} << " bytes ran out (in a node's " << during
                << "), so every node of the pack is silenced";
        }
        return fault(node, RP_SCRIPT_FAULT_HEAP, pk.deadReason);
    }
    char buf[160];
    Text t(buf, sizeof buf);
    t << during << " threw: ";
    describe(ctx, e, t);
    return fault(node, RP_SCRIPT_FAULT_THREW, buf);
}

// Whether a value a call returned says the call is not done (a promise, or anything with a then: 1), or not (0): a
// device pack's node runs synchronously. -1, with the exception pending, when reading its then threw (a getter's
// throw, or the deadline's interrupt), which is the node's fault like any throw, never an answer.
int thenable(JSContext* ctx, JSValue v) {
    if (!isObject(ctx, v)) return 0;
    const JSValue then = JS_GetPropertyStr(ctx, v, "then");
    if (JS_IsException(then)) return -1;
    return isFunction(ctx, then) ? 1 : 0;
}

// ---- the instance ---------------------------------------------------------------------------------------------------

JSValue instances(PackState& pk) { return at(pk.base.ctx, pk.root.val, kRootInstances); }
JSValue record(PackState& pk, std::int32_t slot) {
    return at(pk.base.ctx, instances(pk), static_cast<std::uint32_t>(slot));
}

void* encode(std::uint32_t slot, std::uint32_t ordinal, std::uint32_t item) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>((slot << 26) | (ordinal << kItemBits) | item));
}
std::uint32_t slotOf(void* opaque) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(opaque)) >> 26;
}
std::uint32_t ordinalOf(void* opaque) {
    return (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(opaque)) >> kItemBits) & 63u;
}
std::uint32_t itemOf(void* opaque) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(opaque)) & ((1u << kItemBits) - 1);
}

std::uint32_t kindOf(JSContext* ctx, JSValue kind) {
    static constexpr const char* kKinds[] = {"audio", "events", "bytes", "control", "link", "resource"};
    for (std::uint32_t k = 0; k < 6; ++k)
        if (equals(ctx, kind, kKinds[k])) return k;
    return 6;
}

// Frees the slots the control thread released since the last entry.
void drainReleases(PackState& pk) {
    std::uint32_t tail = pk.releaseTail.load(std::memory_order_relaxed);
    const std::uint32_t head = pk.releaseHead.load(std::memory_order_acquire);
    while (tail != head) {
        const std::uint8_t slot = pk.releases[tail % kMaxInstances];
        JS_SetPropertyUint32(pk.base.ctx, instances(pk), slot, JS_UNDEFINED);
        pk.freeSlots[pk.freeCount++] = slot;
        ++tail;
    }
    pk.releaseTail.store(tail, std::memory_order_release);
}

// The node's JS instance, made (the header comment): its record in the pack's instance table, or a fault.
bool materialize(Node& node) {
    PackState&  pk = *node.pack;
    ContextState& s = pk.base;
    JSContext*  ctx = s.ctx;
    if (pk.freeCount == 0)
        return fault(node, RP_SCRIPT_FAULT_REFUSED, "the pack's context holds as many instances as it can, 64");
    const std::uint8_t slot = pk.freeSlots[pk.freeCount - 1];

    Rooted settings(ctx);
#ifdef RP_DEVICE
    settings.set(decodeSettings(ctx, settingsBytes(node), node.settingsSize));
#else
    settings.set(node.settings ? parseJson(ctx, *node.settings) : JS_NewObject(ctx));
#endif
    if (JS_IsException(settings.get())) return faultFromException(node, "reading its settings");
    Rooted def(ctx, at(ctx, at(ctx, pk.root.val, kRootDefs), node.type->index));
    Rooted context(ctx, JS_NewObject(ctx));
    if (JS_IsException(context.get())) return faultFromException(node, "create");
    if (JS_IsException(JS_SetPropertyStr(ctx, context.get(), "settings", settings.get()))) {
        return faultFromException(node, "create");
    }
    {
        const JSValue rate = JS_NewFloat64(ctx, node.sampleRate);
        if (JS_IsException(rate) || JS_IsException(JS_SetPropertyStr(ctx, context.get(), "sampleRate", rate)))
            return faultFromException(node, "create");
        const JSValue frames = JS_NewUint32(ctx, node.maxFrames);
        if (JS_IsException(frames) || JS_IsException(JS_SetPropertyStr(ctx, context.get(), "maxFrames", frames)))
            return faultFromException(node, "create");
    }
    node.idle = equals(ctx, JS_GetPropertyStr(ctx, def.get(), "idle"), "skip");
    Rooted fn(ctx, JS_GetPropertyStr(ctx, def.get(), "create"));
    Rooted instance(ctx, invoke(s, fn, def, &context));
    if (JS_IsException(instance.get())) return faultFromException(node, "create");
    if (!isObject(ctx, instance.get()))
        return fault(node, RP_SCRIPT_FAULT_REFUSED, "create returned no object with a process function");
    switch (thenable(ctx, instance.get())) {
        case -1: return faultFromException(node, "create");
        case 1:
            return fault(node, RP_SCRIPT_FAULT_REFUSED,
                         "create returned a promise or a thenable: a device pack's node is made synchronously");
        default: break;
    }
    // What the instance holds, read through any getter it has: a getter's throw, or an overrun in it, is create's.
    Rooted process(ctx, JS_GetPropertyStr(ctx, instance.get(), "process"));
    if (JS_IsException(process.get())) return faultFromException(node, "create");
    if (!isFunction(ctx, process.get()))
        return fault(node, RP_SCRIPT_FAULT_REFUSED, "create returned no object with a process function");
    Rooted portsChanged(ctx, JS_GetPropertyStr(ctx, instance.get(), "portsChanged"));
    if (JS_IsException(portsChanged.get())) return faultFromException(node, "create");
    Rooted onSettings(ctx, JS_GetPropertyStr(ctx, instance.get(), "onSettings"));
    if (JS_IsException(onSettings.get())) return faultFromException(node, "create");
    Rooted activate(ctx, JS_GetPropertyStr(ctx, instance.get(), "activate"));
    if (JS_IsException(activate.get())) return faultFromException(node, "create");

    // Its ports and params, by ordinal: their ids (what a block method is given), and what each is in the node's own
    // metas (Node::metas).
    fn.set(JS_GetPropertyStr(ctx, def.get(), "layout"));
    Rooted layout(ctx, invoke(s, fn, def, &settings));
    if (JS_IsException(layout.get())) return faultFromException(node, "layout");
    Rooted ports(ctx, JS_GetPropertyStr(ctx, layout.get(), "ports"));
    Rooted params(ctx, JS_GetPropertyStr(ctx, layout.get(), "params"));
    std::uint32_t portCount = 0, paramCount = 0;
    if (!JS_IsArray(ctx, ports.get()) || JS_ToUint32(ctx, &portCount, JS_GetPropertyStr(ctx, ports.get(), "length")) ||
        (JS_IsArray(ctx, params.get()) &&
         JS_ToUint32(ctx, &paramCount, JS_GetPropertyStr(ctx, params.get(), "length"))))
        return fault(node, RP_SCRIPT_FAULT_REFUSED, "layout returned no ports");
    const std::uint32_t count = portCount + paramCount;
    if (count > kMaxPorts)
        return fault(node, RP_SCRIPT_FAULT_REFUSED,
                     "the node has more than 64 ports and params, the most a script node has");
    Rooted ids(ctx, JS_NewArray(ctx, static_cast<int>(count)));
    if (JS_IsException(ids.get())) return faultFromException(node, "create");
    node.portCount = count;
    std::uint32_t inputs = 0, outputs = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const bool param = i >= portCount;
        Rooted p(ctx, at(ctx, param ? params.get() : ports.get(), param ? i - portCount : i));
        const JSValue id = JS_GetPropertyStr(ctx, p.get(), "id");
        JS_SetPropertyUint32(ctx, ids.get(), i, id);
        std::uint32_t side = kParam, kind = RP_PORT_CONTROL, index = i - portCount;
        if (!param) {
            kind = kindOf(ctx, JS_GetPropertyStr(ctx, p.get(), "kind"));
            const bool out = equals(ctx, JS_GetPropertyStr(ctx, p.get(), "dir"), "out");
            side = out ? kOutput : kInput;
            index = out ? outputs++ : inputs++;
        }
        node.metas[i] = side | (kind << 2) | (index << 8);
    }
    // An overrun the making of the instance ran into and a builtin or a getter dropped is create's all the same.
    if (s.expired) return deadlineFault(node, "create");
    Rooted block(ctx, JS_NewObjectClassUser(ctx, RP_MQJS_CLASS_BLOCK, 0));
    if (JS_IsException(block.get())) return faultFromException(node, "create");
    JS_SetOpaque(ctx, block.get(), encode(slot, 0, 0));
    Rooted portState(ctx, JS_NewArray(ctx, static_cast<int>(count)));
    if (JS_IsException(portState.get())) return faultFromException(node, "create");
    Rooted rec(ctx, JS_NewArray(ctx, kRecLength));
    if (JS_IsException(rec.get())) return faultFromException(node, "create");
    const Rooted* fields[kRecLength] = {&instance, &process, &portsChanged, &onSettings,
                                        &activate, &block,   &ids,          &portState};
    for (std::uint32_t i = 0; i < kRecLength; ++i) JS_SetPropertyUint32(ctx, rec.get(), i, fields[i]->get());
    JS_SetPropertyUint32(ctx, instances(pk), slot, rec.get());
    --pk.freeCount;
    node.slot = slot;
    node.state = State::Live;
    node.activated = false;
    return true;
}

// The hooks that wait for the node's next call: activate (after a re-activation), onSettings (after a settings swap).
bool runHooks(Node& node) {
    PackState& pk = *node.pack;
    JSContext* ctx = pk.base.ctx;
    if (node.activated) {
        node.activated = false;
        Rooted rec(ctx, record(pk, node.slot));
        Rooted fn(ctx, at(ctx, rec.get(), kRecActivate));
        if (isFunction(ctx, fn.get())) {
            Rooted self(ctx, at(ctx, rec.get(), kRecInstance));
            Rooted rate(ctx, JS_NewFloat64(ctx, node.sampleRate));
            Rooted frames(ctx, JS_NewUint32(ctx, node.maxFrames));
            const JSValue r = invoke(pk.base, fn, self, &rate, &frames);
            if (JS_IsException(r)) return faultFromException(node, "activate");
            if (pk.base.expired) return deadlineFault(node, "activate");
            if (r == JS_FALSE)
                return fault(node, RP_SCRIPT_FAULT_REFUSED, "activate refused the sample rate or block size");
        }
    }
#ifndef RP_DEVICE
    if (node.settingsChanged) {
        node.settingsChanged = false;
        Rooted rec(ctx, record(pk, node.slot));
        Rooted fn(ctx, at(ctx, rec.get(), kRecOnSettings));
        if (isFunction(ctx, fn.get())) {
            Rooted self(ctx, at(ctx, rec.get(), kRecInstance));
            Rooted settings(ctx, parseJson(ctx, *node.settings));
            if (JS_IsException(settings.get())) return faultFromException(node, "onSettings");
            if (JS_IsException(invoke(pk.base, fn, self, &settings))) return faultFromException(node, "onSettings");
            if (pk.base.expired) return deadlineFault(node, "onSettings");
        }
    }
#endif
    return true;
}

// A call into the node on the thread that runs blocks: the pack's releases freed, the instance made if it is not yet,
// and the hooks that wait run. False when the node is, or now is, faulted.
bool enter(Node& node) {
    if (node.state == State::Faulted) return false;
    PackState& pk = *node.pack;
    if (pk.dead) return fault(node, RP_SCRIPT_FAULT_HEAP, pk.deadReason);
    drainReleases(pk);
    if (node.state == State::Pending && !materialize(node)) return false;
    return runHooks(node);
}

// ---- the block API --------------------------------------------------------------------------------------------------

PackState* stateOf(JSContext* ctx) {
    auto* s = static_cast<ContextState*>(JS_GetContextOpaque(ctx));
    return s->kind == Kind::Pack ? reinterpret_cast<PackState*>(s) : nullptr;
}

constexpr const char* kNotRunning = "a block, and what it hands out, is good only during its node's process call";

// The pack whose node's process call `self` (a block object, or one it handed out) belongs to: null, with a TypeError
// thrown, when no process call is running or it is another node's.
PackState* running(JSContext* ctx, JSValue self, int classId) {
    PackState* pk = stateOf(ctx);
    if (!pk || JS_GetClassID(ctx, self) != classId) {
        JS_ThrowTypeError(ctx, "not a block's own object");
        return nullptr;
    }
    if (!pk->block || static_cast<std::int32_t>(slotOf(JS_GetOpaque(ctx, self))) != pk->runningSlot) {
        JS_ThrowTypeError(ctx, "%s", kNotRunning);
        return nullptr;
    }
    return pk;
}

struct Port {
    std::uint32_t ordinal = 0;
    std::uint32_t side = 0;
    std::uint32_t kind = 0;
    std::uint32_t index = 0;
};

bool inBlock(const PackState& pk, const Port& p);

// The running node's port or param `ordinal` (below its portCount), from its native metas.
Port portAt(const PackState& pk, std::uint32_t ordinal) {
    const std::uint32_t m = pk.running->metas[ordinal];
    return Port{ordinal, m & 3u, (m >> 2) & 63u, m >> 8};
}

// The running node's port or param named `name`, on a side in `sides` and of a kind in `kinds` (bit masks): false,
// with a TypeError thrown, for one it does not have.
bool findPort(JSContext* ctx, PackState& pk, JSValue name, std::uint32_t sides, std::uint32_t kinds, const char* what,
              Port* out) {
    Node& node = *pk.running;
    std::uint32_t found = kMaxPorts;
    for (std::uint32_t k = 0; k < node.nameCount && found == kMaxPorts; ++k)
        if (node.names[k] == name) found = node.nameOrdinals[k];
    if (found == kMaxPorts) {
        const JSValue ids = at(ctx, record(pk, pk.runningSlot), kRecPortIds);
        for (std::uint32_t i = 0; i < node.portCount; ++i) {
            if (!sameString(ctx, at(ctx, ids, i), name)) continue;
            found = i;
            if (node.nameCount < Node::kNames && rp_mqjs_is_fixed(ctx, name)) {
                node.names[node.nameCount] = name;
                node.nameOrdinals[node.nameCount++] = static_cast<std::uint8_t>(i);
            }
            break;
        }
    }
    if (found < node.portCount) {
        const Port p = portAt(pk, found);
        if ((sides >> p.side & 1u) && (kinds >> p.kind & 1u) && inBlock(pk, p)) {
            *out = p;
            return true;
        }
    }
    JSCStringBuf buf;
    const char*  s = JS_IsString(ctx, name) ? JS_ToCString(ctx, name, &buf) : nullptr;
    JS_ThrowTypeError(ctx, "the node has no %s \"%s\"", what, s ? s : "?");
    return false;
}

// Bucket b (the header comment): its arrays' capacity, and the one a count goes in.
std::uint32_t capacityOf(std::uint32_t b) { return b == 0 ? 0 : 1u << (b - 1); }
std::uint32_t bucketOf(std::uint32_t n) {
    if (n == 0) return 0;
    std::uint32_t b = 1;
    while (capacityOf(b) < n) ++b;
    return b;
}

// A port's state, made the first time it is asked for: [head, pool, pooled, buffer, buckets...], the pool made by
// ensurePool.
JSValue portState(JSContext* ctx, PackState& pk, std::uint32_t ordinal) {
    Rooted states(ctx, at(ctx, record(pk, pk.runningSlot), kRecPortState));
    const JSValue st = at(ctx, states.get(), ordinal);
    if (!JS_IsUndefined(st)) return st;
    Rooted made(ctx, JS_NewArray(ctx, kStateLength));
    if (JS_IsException(made.get())) return JS_EXCEPTION;
    JS_SetPropertyUint32(ctx, made.get(), kStatePooled, JS_NewInt32(ctx, 0));
    JS_SetPropertyUint32(ctx, states.get(), ordinal, made.get());
    return made.get();
}

// new Uint8Array(buffer, offset, length): a view of `length` bytes of a port's payload buffer, made by the engine's own
// constructor (rp_mqjs.h): no call into JavaScript, so a refill works at any depth of a node's C re-entries.
JSValue view(JSContext* ctx, const Rooted& buffer, std::uint32_t offset, std::uint32_t length) {
    return rp_mqjs_new_uint8_view(ctx, buffer.get(), offset, length);
}

// A port's pool grown to `capacity` places, with every bucket array up to capacity's: what a block hands out for this
// port, made before the node's JS runs on it, so no block that fits its queue makes any of it. A message place is a
// message object with a view of its own of each length from 1 to kPlacedLengths, over three bytes of the buffer made
// for the places added; a change place, a change object. A queue's head is the empty Uint8Array every empty payload
// shares; a control's, its stream object.
bool ensurePool(JSContext* ctx, PackState& pk, std::uint32_t ordinal, std::uint32_t capacity, bool messages) {
    Rooted st(ctx, portState(ctx, pk, ordinal));
    if (JS_IsException(st.get())) return false;
    int pooled = 0;
    JS_ToInt32(ctx, &pooled, at(ctx, st.get(), kStatePooled));
    const auto have = static_cast<std::uint32_t>(pooled);
    if (JS_IsUndefined(at(ctx, st.get(), kStateHead))) {
        const JSValue head =
            messages ? rp_mqjs_new_uint8_array(ctx, 0) : JS_NewObjectClassUser(ctx, RP_MQJS_CLASS_CONTROL_STREAM, 0);
        if (JS_IsException(head)) return false;
        if (!messages) JS_SetOpaque(ctx, head, encode(static_cast<std::uint32_t>(pk.runningSlot), ordinal, 0));
        JS_SetPropertyUint32(ctx, st.get(), kStateHead, head);
    }
    if (JS_IsUndefined(at(ctx, st.get(), kStatePool))) {
        // At the capacity it is first asked for, so filling it grows nothing.
        const JSValue pool = JS_NewArray(ctx, static_cast<int>(capacity));
        if (JS_IsException(pool)) return false;
        JS_SetPropertyUint32(ctx, st.get(), kStatePool, pool);
    }
    if (have < capacity) {
        Rooted buffer(ctx);
        if (messages) {
            buffer.set(rp_mqjs_new_array_buffer(ctx, (capacity - have) * kPlacedLengths));
            if (JS_IsException(buffer.get())) return false;
        }
        for (std::uint32_t k = have; k < capacity; ++k) {
            Rooted item(ctx, JS_NewObjectClassUser(ctx, messages ? RP_MQJS_CLASS_MESSAGE : RP_MQJS_CLASS_CONTROL_CHANGE,
                                                   messages ? 1 + kPlacedLengths : 0));
            if (JS_IsException(item.get())) return false;
            JS_SetOpaque(ctx, item.get(), encode(static_cast<std::uint32_t>(pk.runningSlot), ordinal, k));
            for (std::uint32_t length = 1; messages && length <= kPlacedLengths; ++length) {
                const JSValue v = view(ctx, buffer, (k - have) * kPlacedLengths, length);
                if (JS_IsException(v)) return false;
                JS_SetUserValue(ctx, item.get(), length, v);
            }
            if (JS_IsException(JS_SetPropertyUint32(ctx, at(ctx, st.get(), kStatePool), k, item.get()))) return false;
            JS_SetPropertyUint32(ctx, st.get(), kStatePooled, JS_NewInt32(ctx, static_cast<int>(k + 1)));
        }
    }
    for (std::uint32_t b = 0; b <= bucketOf(capacity); ++b) {
        if (!JS_IsUndefined(at(ctx, st.get(), kStateBuckets + b))) continue;
        const JSValue list = JS_NewArray(ctx, static_cast<int>(capacityOf(b)));
        if (JS_IsException(list)) return false;
        JS_SetPropertyUint32(ctx, st.get(), kStateBuckets + b, list);
    }
    return true;
}

// The port's list of `n` items: the array of n's bucket, holding the pool's first n, its length set within the half of
// its capacity the engine keeps, so nothing is made. JS may have changed the array since (a splice, a push): its length
// is set on the array's rooted slot, which is right whatever the engine then grows. A port whose pool is short of `n`
// (its queue grew since the block's start: no host does) is refused rather than grown here.
JSValue listOf(JSContext* ctx, PackState& pk, std::uint32_t ordinal, std::uint32_t n) {
    Rooted st(ctx, portState(ctx, pk, ordinal));
    if (JS_IsException(st.get())) return JS_EXCEPTION;
    if (pk.running->pooled[ordinal] < n)
        return JS_ThrowInternalError(ctx, "the port holds more than its queue held at the block's start");
    Rooted list(ctx, at(ctx, st.get(), kStateBuckets + bucketOf(n)));
    if (rp_mqjs_set_length(ctx, list.slot(), n)) return JS_EXCEPTION;
    const JSValue pool = at(ctx, st.get(), kStatePool);
    for (std::uint32_t k = 0; k < n; ++k) rp_mqjs_array_put(ctx, list.get(), k, at(ctx, pool, k));
    return list.get();
}

// A message's data for this block, refilled with the payload: the shared empty view, the message's own view of the
// payload's length up to kPlacedLengths, or, for a longer payload (a SysEx), a Uint8Array made for this block.
// The bytes are copied into the view where it lies (rp_mqjs_bytes), which allocates nothing.
bool fillData(JSContext* ctx, JSValue message, JSValue empty, const std::uint8_t* bytes, std::uint32_t size) {
    Rooted m(ctx, message);
    Rooted data(ctx, size == 0 ? empty : size <= kPlacedLengths ? JS_GetUserValue(ctx, message, size) : JS_UNDEFINED);
    if (size > kPlacedLengths) {
        data.set(rp_mqjs_new_uint8_array(ctx, size));
        if (JS_IsException(data.get())) return false;
    }
    std::uint32_t length = 0;
    std::uint8_t* to = size ? rp_mqjs_bytes(ctx, data.get(), &length) : nullptr;
    if (to && length == size) {
        std::memcpy(to, bytes, size);
    } else {
        for (std::uint32_t j = 0; j < size; ++j) JS_SetPropertyUint32(ctx, data.get(), j, JS_NewInt32(ctx, bytes[j]));
    }
    JS_SetUserValue(ctx, m.get(), 0, data.get());
    return true;
}

constexpr std::uint32_t kQueueKinds = (1u << RP_PORT_EVENTS) | (1u << RP_PORT_BYTES);
constexpr std::uint32_t kControlKinds = 1u << RP_PORT_CONTROL;

// Whether a port the running node's layout names is one the running block has, of the kind the layout says: what
// every read of the block through a port is checked against, so an object a pack kept past its block, which names an
// ordinal of another node's layout (its slot now this node's), is refused rather than read out of bounds.
bool inBlock(const PackState& pk, const Port& p) {
    const rp_process& b = *pk.block;
    if (p.side == kInput) return p.index < b.input_count && b.inputs[p.index].kind == p.kind;
    if (p.side == kOutput) return p.index < b.output_count && b.outputs[p.index].kind == p.kind;
    return p.side == kParam && p.kind == RP_PORT_CONTROL && p.index < b.param_count;
}

// The port a message, stream or change object names, checked: on one of `sides`, of one of `kinds`, past none of the
// running node's ports and in the running block. False, with a TypeError thrown, otherwise.
bool keptPort(JSContext* ctx, PackState& pk, void* opaque, std::uint32_t sides, std::uint32_t kinds, Port* out) {
    const std::uint32_t ordinal = ordinalOf(opaque);
    if (ordinal < pk.running->portCount) {
        const Port p = portAt(pk, ordinal);
        if ((sides >> p.side & 1u) && (kinds >> p.kind & 1u) && inBlock(pk, p)) {
            *out = p;
            return true;
        }
    }
    JS_ThrowTypeError(ctx, "a message, stream or change kept past its block, of a node no longer running");
    return false;
}

// Every port of the running node readied for its block (ensurePool), before its JS runs: what keeps a warmed node's
// heap where its first block left it.
bool preparePorts(JSContext* ctx, PackState& pk) {
    Node& node = *pk.running;
    for (std::uint32_t i = 0; i < node.portCount; ++i) {
        const Port p = portAt(pk, i);
        if (p.side == kOutput || !inBlock(pk, p)) continue;
        if (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES) {
            const std::uint32_t capacity = pk.block->inputs[p.index].u.queue->capacity;
            if ((node.readied >> i & 1u) && node.pooled[i] >= capacity) continue;
            if (!ensurePool(ctx, pk, i, capacity, true)) return false;
            node.readied |= std::uint64_t{1} << i;
            node.pooled[i] = capacity;
        } else if (p.kind == RP_PORT_CONTROL) {
            const rp_control_queue& q =
                p.side == kParam ? *pk.block->params[p.index] : *pk.block->inputs[p.index].u.control;
            if ((node.readied >> i & 1u) && node.pooled[i] >= q.capacity) continue;
            if (!ensurePool(ctx, pk, i, q.capacity, false)) return false;
            node.readied |= std::uint64_t{1} << i;
            node.pooled[i] = q.capacity;
        }
    }
    return true;
}

// block.messages(port): an events or bytes input's messages this block.
JSValue blockMessages(JSContext* ctx, JSValue* self, int argc, JSValue* argv) {
    PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
    Port p;
    if (!pk || !findPort(ctx, *pk, argc > 0 ? argv[0] : JS_UNDEFINED, 1u << kInput, kQueueKinds,
                         "events or bytes input", &p))
        return JS_EXCEPTION;
    const rp_queue& q = *pk->block->inputs[p.index].u.queue;
    Rooted list(ctx, listOf(ctx, *pk, p.ordinal, q.count));
    if (JS_IsException(list.get())) return JS_EXCEPTION;
    Rooted empty(ctx, at(ctx, portState(ctx, *pk, p.ordinal), kStateHead));
    for (std::uint32_t k = 0; k < q.count; ++k) {
        const rp_message& m = q.items[k];
        if (!fillData(ctx, at(ctx, list.get(), k), empty.get(), q.bytes + m.offset, m.size)) return JS_EXCEPTION;
    }
    return list.get();
}

// Message `index` taken back out of a queue, the ones after it moved down with their bytes.
void eraseMessage(rp_queue& q, std::uint32_t index) {
    const rp_message gone = q.items[index];
    std::memmove(q.bytes + gone.offset, q.bytes + gone.offset + gone.size,
                 q.bytes_used - gone.offset - gone.size);
    for (std::uint32_t k = index + 1; k < q.count; ++k) {
        q.items[k - 1] = q.items[k];
        q.items[k - 1].offset -= gone.size;
    }
    --q.count;
    q.bytes_used -= gone.size;
}

// A message onto an events or bytes output, copied at the call: send's and forward's. False when the queue is full.
// JS_EXCEPTION when an element of the data (an array-like that is no Uint8Array) throws, the message taken back out.
// `data` is a slot the collector updates (an argument on the engine's stack, a Rooted's), read at every use: reading
// the length or an element by name, or converting an element (a valueOf, a boxed float at 32 bits), may allocate, and
// a collection moves what it keeps.
JSValue sendData(JSContext* ctx, PackState& pk, const Port& p, std::uint32_t frame, JSValue* data,
                 std::uint32_t flags) {
    // An array's or a typed array's length is read in place; any other array-like's by name (which allocates), a
    // getter's throw taken as the send's.
    std::uint32_t length = rp_mqjs_length(ctx, *data);
    if (length == UINT32_MAX && !toUint32(ctx, JS_GetPropertyStr(ctx, *data, "length"), &length)) return JS_EXCEPTION;
    rp_queue& q = *pk.block->outputs[p.index].u.queue;
    std::uint8_t* at_ = nullptr;
    if (!graph::reserveMessage(q, frame, length, flags, &at_)) return JS_FALSE;
    // A Uint8Array's bytes (a message's own data, a node's array) are copied as they lie: nothing runs in between to
    // move them, and no element can throw.
    std::uint32_t viewed = 0;
    if (const std::uint8_t* from = rp_mqjs_bytes(ctx, *data, &viewed); from && viewed == length) {
        std::memcpy(at_, from, length);
        return JS_TRUE;
    }
    // Where it is: an element's getter or valueOf may send onto the same queue before this returns, so it need not stay
    // the last message (its bytes stay where they are).
    const std::uint32_t reserved = q.count - 1;
    for (std::uint32_t i = 0; i < length; ++i) {
        int b = 0;
        const JSValue v = JS_GetPropertyUint32(ctx, *data, i);
        if (JS_IsException(v) || JS_ToInt32(ctx, &b, v)) {
            // A node's bug, and it throws: the message is taken back out, so no part of it goes.
            eraseMessage(q, reserved);
            return JS_EXCEPTION;
        }
        at_[i] = static_cast<std::uint8_t>(b & 0xff);
    }
    return JS_TRUE;
}

// block.send(port, frame, data, flags?): onto an events or bytes output, copied at the call; false when it is full.
JSValue blockSend(JSContext* ctx, JSValue* self, int argc, JSValue* argv) {
    PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
    Port p;
    if (!pk || !findPort(ctx, *pk, argc > 0 ? argv[0] : JS_UNDEFINED, 1u << kOutput, kQueueKinds,
                         "events or bytes output", &p))
        return JS_EXCEPTION;
    if (argc < 3) return JS_ThrowTypeError(ctx, "send takes a port, a frame and the data");
    std::uint32_t frame = 0, flags = 0;
    if (JS_ToUint32(ctx, &frame, argv[1]) || (argc > 3 && JS_ToUint32(ctx, &flags, argv[3]))) return JS_EXCEPTION;
    return sendData(ctx, *pk, p, frame, &argv[2], flags);
}

// block.forward(port, message): send(port, message.frame, message.data, message.flags) in one call. A message this
// node's block handed out is read where it lies, as its getters read it: its frame and flags from the queue its object
// names, its data as the message holds it now (a node may have written its bytes). Any other object is read by its
// fields.
JSValue blockForward(JSContext* ctx, JSValue* self, int argc, JSValue* argv) {
    PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
    Port p;
    if (!pk || !findPort(ctx, *pk, argc > 0 ? argv[0] : JS_UNDEFINED, 1u << kOutput, kQueueKinds,
                         "events or bytes output", &p))
        return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "forward takes a port and a message");
    if (JS_GetClassID(ctx, argv[1]) == RP_MQJS_CLASS_MESSAGE) {
        void* opaque = JS_GetOpaque(ctx, argv[1]);
        if (static_cast<std::int32_t>(slotOf(opaque)) != pk->runningSlot)
            return JS_ThrowTypeError(ctx, "%s", kNotRunning);
        Port in;
        if (!keptPort(ctx, *pk, opaque, 1u << kInput, kQueueKinds, &in)) return JS_EXCEPTION;
        const rp_queue& q = *pk->block->inputs[in.index].u.queue;
        const std::uint32_t k = itemOf(opaque);
        // Past the queue's count its getters read undefined, which send converts to 0.
        const std::uint32_t frame = k < q.count ? q.items[k].frame : 0, flags = k < q.count ? q.items[k].flags : 0;
        Rooted data(ctx, JS_GetUserValue(ctx, argv[1], 0));
        return sendData(ctx, *pk, p, frame, data.slot(), flags);
    }
    Rooted data(ctx);
    std::uint32_t frame = 0, flags = 0;
    if (!toUint32(ctx, JS_GetPropertyStr(ctx, argv[1], "frame"), &frame)) return JS_EXCEPTION;
    data.set(JS_GetPropertyStr(ctx, argv[1], "data"));
    if (JS_IsException(data.get())) return JS_EXCEPTION;
    if (!toUint32(ctx, JS_GetPropertyStr(ctx, argv[1], "flags"), &flags)) return JS_EXCEPTION;
    return sendData(ctx, *pk, p, frame, data.slot(), flags);
}

// block.control(id): a control input's or a param's stream object, which preparePorts made.
JSValue blockControl(JSContext* ctx, JSValue* self, int argc, JSValue* argv) {
    PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
    Port p;
    if (!pk || !findPort(ctx, *pk, argc > 0 ? argv[0] : JS_UNDEFINED, (1u << kInput) | (1u << kParam), kControlKinds,
                         "control input or param", &p))
        return JS_EXCEPTION;
    return at(ctx, portState(ctx, *pk, p.ordinal), kStateHead);
}

// block.emit(port, frame, value): onto a control output.
JSValue blockEmit(JSContext* ctx, JSValue* self, int argc, JSValue* argv) {
    PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
    Port p;
    if (!pk || !findPort(ctx, *pk, argc > 0 ? argv[0] : JS_UNDEFINED, 1u << kOutput, kControlKinds, "control output",
                         &p))
        return JS_EXCEPTION;
    if (argc < 3) return JS_ThrowTypeError(ctx, "emit takes a port, a frame and a value");
    std::uint32_t frame = 0;
    double value = 0.0;
    if (JS_ToUint32(ctx, &frame, argv[1]) || JS_ToNumber(ctx, &value, argv[2])) return JS_EXCEPTION;
    return JS_NewBool(graph::pushControl(*pk->block->outputs[p.index].u.control, frame, value));
}

const rp_control_queue* streamOf(PackState& pk, const Port& p) {
    return p.side == kParam ? pk.block->params[p.index] : pk.block->inputs[p.index].u.control;
}

JSValue apiCall(JSContext* ctx, JSValue* self, int argc, JSValue* argv, int magic) {
    switch (magic) {
        case RP_MQJS_LOG: return consoleLog(ctx, *static_cast<ContextState*>(JS_GetContextOpaque(ctx)), argc, argv);
        case RP_MQJS_MESSAGES: return blockMessages(ctx, self, argc, argv);
        case RP_MQJS_SEND: return blockSend(ctx, self, argc, argv);
        case RP_MQJS_FORWARD: return blockForward(ctx, self, argc, argv);
        case RP_MQJS_CONTROL: return blockControl(ctx, self, argc, argv);
        case RP_MQJS_EMIT: return blockEmit(ctx, self, argc, argv);
        case RP_MQJS_AUDIO: return JS_ThrowTypeError(ctx, "a device pack's node has no audio ports");
        default: return JS_ThrowTypeError(ctx, "no such block method");
    }
}

JSValue apiGet(JSContext* ctx, JSValue* self, int, JSValue*, int magic) {
    switch (magic) {
        case RP_MQJS_FRAMES: {
            PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
            return pk ? JS_NewUint32(ctx, pk->block->frames) : JS_EXCEPTION;
        }
        case RP_MQJS_TRANSPORT: {
            PackState* pk = running(ctx, *self, RP_MQJS_CLASS_BLOCK);
            return pk ? at(ctx, pk->root.val, kRootTransport) : JS_EXCEPTION;
        }
        case RP_MQJS_SAMPLE_RATE:
        case RP_MQJS_TEMPO:
        case RP_MQJS_PPQ:
        case RP_MQJS_PLAYING: {
            PackState* pk = stateOf(ctx);
            if (!pk || JS_GetClassID(ctx, *self) != RP_MQJS_CLASS_TRANSPORT)
                return JS_ThrowTypeError(ctx, "not a transport");
            if (!pk->block || !pk->block->transport) return JS_ThrowTypeError(ctx, "%s", kNotRunning);
            const rp_transport& t = *pk->block->transport;
            if (magic == RP_MQJS_PLAYING) return JS_NewBool((t.flags & RP_TRANSPORT_PLAYING) != 0);
            return JS_NewFloat64(ctx, magic == RP_MQJS_SAMPLE_RATE ? t.sample_rate
                                      : magic == RP_MQJS_TEMPO     ? t.tempo
                                                                   : t.ppq);
        }
        case RP_MQJS_MESSAGE_FRAME:
        case RP_MQJS_MESSAGE_FLAGS:
        case RP_MQJS_MESSAGE_DATA: {
            PackState* pk = running(ctx, *self, RP_MQJS_CLASS_MESSAGE);
            if (!pk) return JS_EXCEPTION;
            // data is the message's own value, in the JS heap: a kept message reads what it last held, and nothing
            // native, so it is not checked against the block.
            if (magic == RP_MQJS_MESSAGE_DATA) return JS_GetUserValue(ctx, *self, 0);
            void* opaque = JS_GetOpaque(ctx, *self);
            Port p;
            if (!keptPort(ctx, *pk, opaque, 1u << kInput, kQueueKinds, &p)) return JS_EXCEPTION;
            const rp_queue& q = *pk->block->inputs[p.index].u.queue;
            const std::uint32_t k = itemOf(opaque);
            if (k >= q.count) return JS_UNDEFINED;
            return JS_NewUint32(ctx, magic == RP_MQJS_MESSAGE_FRAME ? q.items[k].frame : q.items[k].flags);
        }
        case RP_MQJS_STREAM_START:
        case RP_MQJS_STREAM_CHANGES: {
            PackState* pk = running(ctx, *self, RP_MQJS_CLASS_CONTROL_STREAM);
            if (!pk) return JS_EXCEPTION;
            Port p;
            if (!keptPort(ctx, *pk, JS_GetOpaque(ctx, *self), (1u << kInput) | (1u << kParam), kControlKinds, &p))
                return JS_EXCEPTION;
            const rp_control_queue& q = *streamOf(*pk, p);
            if (magic == RP_MQJS_STREAM_START) return JS_NewFloat64(ctx, q.start);
            return listOf(ctx, *pk, p.ordinal, q.count);
        }
        case RP_MQJS_CHANGE_FRAME:
        case RP_MQJS_CHANGE_VALUE: {
            PackState* pk = running(ctx, *self, RP_MQJS_CLASS_CONTROL_CHANGE);
            if (!pk) return JS_EXCEPTION;
            void* opaque = JS_GetOpaque(ctx, *self);
            Port p;
            if (!keptPort(ctx, *pk, opaque, (1u << kInput) | (1u << kParam), kControlKinds, &p)) return JS_EXCEPTION;
            const rp_control_queue& q = *streamOf(*pk, p);
            const std::uint32_t k = itemOf(opaque);
            if (k >= q.count) return JS_UNDEFINED;
            return magic == RP_MQJS_CHANGE_FRAME ? JS_NewUint32(ctx, q.changes[k].frame)
                                                 : JS_NewFloat64(ctx, q.changes[k].value);
        }
        default: return JS_ThrowTypeError(ctx, "no such property");
    }
}

constexpr rp_mqjs_api kApi{&apiCall, &apiGet};

// ---- the node ABI ---------------------------------------------------------------------------------------------------

bool activate(rp_node* n, double sampleRate, std::uint32_t maxFrames, rp_error*) {
    Node& node = *as(n);
    node.sampleRate = sampleRate;
    node.maxFrames = maxFrames;
    if (node.state == State::Live) node.activated = true;
    return true;
}

void deactivate(rp_node*) {}

void portsChanged(rp_node* n, const std::uint8_t* connected, std::uint32_t count) {
    Node& node = *as(n);
    const Armed armed(node.pack->base);
    if (!enter(node)) return;
    PackState& pk = *node.pack;
    JSContext* ctx = pk.base.ctx;
    Rooted rec(ctx, record(pk, node.slot));
    Rooted fn(ctx, at(ctx, rec.get(), kRecPortsChanged));
    if (!isFunction(ctx, fn.get())) return;
    Rooted flags(ctx, JS_NewArray(ctx, static_cast<int>(count)));
    if (JS_IsException(flags.get())) {
        faultFromException(node, "portsChanged");
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) JS_SetPropertyUint32(ctx, flags.get(), i, JS_NewBool(connected[i] != 0));
    Rooted self(ctx, at(ctx, rec.get(), kRecInstance));
    if (JS_IsException(invoke(pk.base, fn, self, &flags))) {
        faultFromException(node, "portsChanged");
        return;
    }
    // An entry that ran past its deadline faults whatever it returned (process says why).
    if (pk.base.expired) deadlineFault(node, "portsChanged");
}

// Whether two starts are the same value, bit for bit (a NaN the same NaN), with no floating-point compare.
bool sameStart(double a, double b) {
    std::uint64_t x = 0, y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

// Whether a block brings a node nothing it has not read: no message on any events or bytes input, no change on any
// control input or param, and every such stream starting where the node's last call left it. Read natively, from the
// block and the node alone.
bool idleBlock(const Node& node, const rp_process& b) {
    std::uint32_t k = 0;
    for (std::uint32_t i = 0; i < b.input_count; ++i) {
        const rp_port_view& v = b.inputs[i];
        if ((v.kind == RP_PORT_EVENTS || v.kind == RP_PORT_BYTES) && v.u.queue->count) return false;
        if (v.kind != RP_PORT_CONTROL) continue;
        if (v.u.control->count || k == kMaxPorts || !sameStart(v.u.control->start, node.ends[k])) return false;
        ++k;
    }
    for (std::uint32_t i = 0; i < b.param_count; ++i) {
        if (b.params[i]->count || k == kMaxPorts || !sameStart(b.params[i]->start, node.ends[k])) return false;
        ++k;
    }
    return true;
}

// Where each control input and param stands at the end of the block the node's process is called on (idleBlock).
void keepEnds(Node& node, const rp_process& b) {
    std::uint32_t k = 0;
    for (std::uint32_t i = 0; i < b.input_count && k < kMaxPorts; ++i)
        if (b.inputs[i].kind == RP_PORT_CONTROL) node.ends[k++] = graph::controlEnd(*b.inputs[i].u.control);
    for (std::uint32_t i = 0; i < b.param_count && k < kMaxPorts; ++i) node.ends[k++] = graph::controlEnd(*b.params[i]);
    node.ran = true;
}

// The messages a block hands a node: every one on its events and bytes inputs.
std::uint32_t inputMessages(const rp_process& b) {
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < b.input_count; ++i) {
        const rp_port_view& v = b.inputs[i];
        if (v.kind == RP_PORT_EVENTS || v.kind == RP_PORT_BYTES) n += v.u.queue->count;
    }
    return n;
}

// One block: the instance made and its hooks run if they wait (enter), every port readied for the block
// (preparePorts: at the first block, the port's whole pool), then process. One deadline covers the entry, started again
// once the ports are ready: making a port's pools is the host's work, bounded by the plan, so it never counts against
// the node's process. A node whose definition says idle: "skip" is not entered at all on a block that brings it
// nothing, once its process has run, while no hook waits for its next call (the pack still alive): its process would
// do nothing there (defineNode.ts's host contract), and the executor clears its outputs as ever. What a block brings
// is read against what the node's last call read, never against the plan step it came from: within a plan a control
// stream starts each block where the last one left it, and moves only through its changes, which bring the node
// something, but a new plan sets each start afresh (a rewired or disconnected input's), and a new plan's step may lie
// where an old one's did (a host with no audio thread reclaims a replaced plan at once, so two re-plans on, a kept
// node that ran no block in the plan between can meet its old step's address). So the node keeps where each stream
// stood at the end of its last call's block, and a block that starts one elsewhere runs.
rp_status process(rp_node* n, const rp_process* block) {
    Node& node = *as(n);
    PackState& pk = *node.pack;
    if (node.idle && node.ran && node.state == State::Live && !node.activated && !pk.dead &&
        idleBlock(node, *block)) {
#ifndef RP_DEVICE
        if (!node.settingsChanged) return RP_OK;
#else
        return RP_OK;
#endif
    }
    Armed armed(pk.base);
    if (!enter(node)) return RP_FAULT;
    JSContext* ctx = pk.base.ctx;
    Rooted rec(ctx, record(pk, node.slot));
    Rooted fn(ctx, at(ctx, rec.get(), kRecProcess));
    Rooted self(ctx, at(ctx, rec.get(), kRecInstance));
    Rooted blockObject(ctx, at(ctx, rec.get(), kRecBlock));
    pk.block = block;
    pk.running = &node;
    pk.runningSlot = node.slot;
    const bool ready = preparePorts(ctx, pk);
    if (node.idle) keepEnds(node, *block);
    armed.restart();
    armed.allow(pk.base.perMessage * inputMessages(*block));
    const JSValue r = ready ? invoke(pk.base, fn, self, &blockObject) : JS_EXCEPTION;
    pk.block = nullptr;
    pk.running = nullptr;
    pk.runningSlot = -1;
    if (JS_IsException(r)) {
        faultFromException(node, ready ? "process" : "port pools");
        return RP_FAULT;
    }
    if (!pk.base.expired) {
        if (equals(ctx, r, "fault")) {
            fault(node, RP_SCRIPT_FAULT_REFUSED, "process returned \"fault\"");
            return RP_FAULT;
        }
        const int then = thenable(ctx, r);
        if (then < 0) {
            faultFromException(node, "process");
            return RP_FAULT;
        }
        if (then > 0) {
            fault(node, RP_SCRIPT_FAULT_REFUSED,
                  "process returned a promise or a thenable: a device pack's node runs synchronously");
            return RP_FAULT;
        }
    }
    // An entry that ran past its deadline faults whatever it returned: the interrupt is uncatchable, but a builtin or a
    // getter the host read may have dropped it on the way out, and the deadline is the node's whatever its script made
    // of it (design decision 5: a deadline faults the node, never the box).
    if (pk.base.expired) {
        deadlineFault(node, "process");
        return RP_FAULT;
    }
    return RP_OK;
}

const void* nodeInterface(rp_node* n, const char* id);

#ifndef RP_DEVICE
rp_prepare_result prepareSettings(rp_node* n, const rp_settings* settings, void** prepared, rp_error* err);
void              commitSettings(rp_node* n, void* prepared, void** old) {
    Node& node = *as(n);
    *old = node.settings;
    node.settings = static_cast<std::string*>(prepared);
    node.settingsChanged = node.state == State::Live;
}
void releaseSettings(rp_node*, void* old) { delete static_cast<std::string*>(old); }
constexpr rp_node_vtable kVtable{&activate,        &deactivate,     &portsChanged,    &process,
                                 &prepareSettings, &commitSettings, &releaseSettings, &nodeInterface};
#else
constexpr rp_node_vtable kVtable{&activate, &deactivate, &portsChanged, &process,
                                 nullptr,   nullptr,     nullptr,       &nodeInterface};
#endif

bool heapUse(rp_node* n, bool collect, rp_script_heap_use* out) {
    Node& node = *as(n);
    if (node.state == State::Pending) return false;
    PackState* pk = node.pack;
    JSContext* ctx = pk->base.ctx;
    if (collect) JS_GC(ctx);
    rp_mqjs_memory m{};
    rp_mqjs_memory_of(ctx, &m);
    out->bytes = pk->contextBytes;
    out->fixed = m.fixed;
    out->heap = m.heap;
    out->stack = m.stack;
    return true;
}

constexpr rp_script_heap kHeap{&heapUse};

const char* faultReason(rp_node* n) {
    const Node& node = *as(n);
    return node.state == State::Faulted ? node.fault : nullptr;
}
std::uint32_t faultKind(rp_node* n) { return as(n)->faultKind; }
constexpr rp_script_fault kFault{&faultReason, &faultKind};

#ifndef RP_DEVICE
std::size_t writeSettings(rp_node* n, char* out, std::size_t capacity) {
    const std::string& json = *as(n)->settings;  // create always sets it
    if (json.size() < capacity) {
        std::memcpy(out, json.data(), json.size());
        out[json.size()] = '\0';
    }
    return json.size();
}
constexpr graph::SettingsJsonInterface kSettingsJson{&writeSettings};
#endif

const void* nodeInterface(rp_node*, const char* id) {
    if (std::strcmp(id, RP_SCRIPT_FAULT_INTERFACE) == 0) return &kFault;
    if (std::strcmp(id, RP_SCRIPT_HEAP_INTERFACE) == 0) return &kHeap;
#ifndef RP_DEVICE
    if (std::strcmp(id, graph::kSettingsJsonInterface) == 0) return &kSettingsJson;
#endif
    return nullptr;
}

const TypeInfo& infoOf(const rp_node_type* type) { return *static_cast<const TypeInfo*>(type->ctx); }

// The pack's memory, which the node's host set the pack up in, and the services it takes from the host: the
// deadline's clock and the console's sink, taken by the first node a pack's memory sees.
PackState* packMemory(const rp_node_type* type, const rp_host* host, rp_error* err) {
    const Pack& pack = *infoOf(type).pack;
    const void* found = host && host->get_interface ? host->get_interface(host, RP_HOST_PACKAGES_INTERFACE) : nullptr;
    const auto* service = static_cast<const rp_host_packages*>(found);
    if (!service) {
        writeError(err, "its host gives no package memory (" RP_HOST_PACKAGES_INTERFACE "), where a script pack's "
                        "context lives");
        return nullptr;
    }
    void* memory = service->memory(service->ctx, &pack.package, err);
    if (!memory) return nullptr;
    PackState* pk = stateAt(memory);
    if (!pk->configured) {
        // Once, by the first node the block sees, before any node of it is in a plan the block thread runs.
        pk->configured = true;
        if (const auto* clock = static_cast<const rp_host_clock*>(host->get_interface(host, RP_HOST_CLOCK_INTERFACE))) {
            pk->base.clockCtx = clock->ctx;
            pk->base.clock = clock->micros;
            pk->base.limit = clock->deadline_micros ? clock->deadline_micros : kHostClockDeadlineMicros;
            pk->base.perMessage = kHostClockMessageMicros;
        }
#ifndef RP_DEVICE
        if (!pk->base.clock) {
            pk->base.clock = &threadCpuMicros;
            pk->base.limit = kDesktopDeadlineMicros;
        }
#endif
        if (const auto* log = static_cast<const rp_host_script_log*>(
                host->get_interface(host, RP_HOST_SCRIPT_LOG_INTERFACE))) {
            pk->base.logCtx = log->ctx;
            pk->base.log = log->write;
        }
    }
    return pk;
}

// ---- the desktop's control context ---------------------------------------------------------------------------------

#ifndef RP_DEVICE
// A settings check in the pack's control context: the definition's parseSettings on `settings` (empty: the empty
// object), and the layout they give, both as JSON. False, with `why` filled, for settings it refuses.
bool check(const TypeInfo& info, const rp_settings* settings, std::string& json, std::string& layout,
           std::string& why) {
    Control& c = *info.pack->control;
    const std::lock_guard<std::mutex> lock(c.mutex);
    ContextState& s = c.state;
    const Armed armed(s);
    JSContext* ctx = s.ctx;
    Rooted raw(ctx);
    const std::size_t size = settings ? settings->size : 0;
    if (size == 0) {
        raw.set(JS_NewObject(ctx));
    } else if (settings->encoding == RP_SETTINGS_MSGPACK) {
        raw.set(decodeSettings(ctx, reinterpret_cast<const std::uint8_t*>(settings->data), size));
    } else {
        raw.set(parseJson(ctx, std::string(settings->data, size)));
    }
    // An overrun is said as one, whatever was running: the engine's own words for it are only "interrupted".
    auto overran = [&] {
        char buf[120];
        Text(buf, sizeof buf) << "the settings check ran past its deadline of " << s.limit << " us";
        why = buf;
        return false;
    };
    auto threw = [&](const char* during) {
        if (s.expired) return overran();
        char buf[200];
        Text t(buf, sizeof buf);
        t << during;
        describe(ctx, JS_GetException(ctx), t);
        why = buf;
        return false;
    };
    if (JS_IsException(raw.get())) return threw("the settings do not parse: ");
    Rooted def(ctx, at(ctx, c.defs.val, info.index));
    Rooted fn(ctx, JS_GetPropertyStr(ctx, def.get(), "parseSettings"));
    Rooted parsed(ctx, invoke(s, fn, def, &raw));
    if (JS_IsException(parsed.get())) {
        if (s.expired) return overran();
        // The definition's own words: parseSettings throws "node type "<id>": settings: <field>: <why>".
        char buf[200];
        Text t(buf, sizeof buf);
        const JSValue e = JS_GetException(ctx);
        Rooted ex(ctx, e);
        if (JS_IsError(ctx, ex.get())) {
            JSCStringBuf b;
            std::size_t  n = 0;
            const char*  m = JS_ToCStringLen(ctx, &n, JS_GetPropertyStr(ctx, ex.get(), "message"), &b);
            if (m) t.put(m, n);
        } else {
            describe(ctx, ex.get(), t);
        }
        why = buf;
        return false;
    }
    if (!stringify(s, parsed, json)) return threw("its settings do not write as JSON: ");
    // What the device would read: the msgpack the exporter writes from these settings, decoded as the device's build
    // decodes it, so settings only one build takes (a field name past the decode's length, a nesting past its depth)
    // are refused here, where they are planned, rather than faulting on the box.
    {
        auto bytes = graph::settingsToMsgpack(json);
        if (!bytes) {
            why = std::string("its settings do not write as msgpack: ") + bytes.error().what();
            return false;
        }
        Rooted decoded(ctx, decodeSettings(ctx, bytes->data(), bytes->size()));
        if (JS_IsException(decoded.get())) return threw("its settings are not ones a device reads: ");
    }
    fn.set(JS_GetPropertyStr(ctx, def.get(), "layout"));
    Rooted l(ctx, invoke(s, fn, def, &parsed));
    if (JS_IsException(l.get())) return threw("its layout threw: ");
    if (!stringify(s, l, layout)) return threw("its layout does not write as JSON: ");
    // A check that ran past its deadline is refused whatever it returned (process says why).
    if (s.expired) return overran();
    return true;
}

const char* descriptor(const rp_node_type* type) {
    const TypeInfo& info = infoOf(type);
    return info.pack->control->descriptors[info.index].c_str();
}

std::size_t layoutFor(const rp_node_type* type, const rp_settings* settings, char* out, std::size_t capacity,
                      rp_error* err) {
    std::string json, layout, why;
    if (!check(infoOf(type), settings, json, layout, why)) {
        writeError(err, why.c_str());
        return 0;
    }
    if (layout.size() < capacity) {
        std::memcpy(out, layout.data(), layout.size());
        out[layout.size()] = '\0';
    }
    return layout.size();
}

rp_prepare_result prepareSettings(rp_node* n, const rp_settings* settings, void** prepared, rp_error* err) {
    Node& node = *as(n);
    std::string json, layout, why;
    if (!check(*node.type, settings, json, layout, why)) {
        writeError(err, why.c_str());
        return RP_PREPARE_INVALID;
    }
    // A script node keeps its instance through a change that keeps its layout; one that moves it is rebuilt (this host
    // takes no reshape).
    if (layout != node.layout) return RP_PREPARE_REBUILD;
    *prepared = new std::string(std::move(json));
    return RP_PREPARE_SWAP;
}
#endif

bool queryMemory(const rp_node_type* type, const rp_settings* settings, rp_memory_req* out, rp_error* err) {
#ifdef RP_DEVICE
    (void)type;
    if (settings && settings->size > 0 && settings->encoding != RP_SETTINGS_MSGPACK) {
        writeError(err, "a device's script node reads msgpack settings alone");
        return false;
    }
    out->size = sizeof(Node) + (settings ? settings->size : 0);
#else
    std::string json, layout, why;
    if (!check(infoOf(type), settings, json, layout, why)) {
        writeError(err, why.c_str());
        return false;
    }
    out->size = sizeof(Node);
#endif
    out->align = alignof(Node);
    return true;
}

bool setupPackage(const rp_package* package, void* memory, rp_error* err);
void teardownPackage(const rp_package* package, void* memory);

#ifndef RP_DEVICE
// A dead pack whose last node is gone set up again in its memory, a fresh context with nothing of the old one, so the
// host's next node of the pack runs: the desktop's way back from a heap that ran out, which a re-plan otherwise could
// not take, since the host keeps a package's memory for its life (graph/PackageMemory.hpp). The control thread's, with
// no node of the pack left for the block thread to enter. A setup that fails leaves the pack dead and torn down.
void renew(PackState& pk) {
    const rp_package& package = pk.pack->package;
    void* memory = &pk;
    teardownPackage(&package, memory);
    std::memset(memory, 0, packBytes(*static_cast<const Pack*>(package.ctx)));
    rp_error err{};
    if (setupPackage(&package, memory, &err)) return;
    PackState* gone = new (memory) PackState{};
    gone->dead = true;
    Text(gone->deadReason, sizeof gone->deadReason) << "the pack could not be set up again: " << err.message;
}
#endif

rp_node* create(const rp_node_type* type, const rp_host* host, void* memory, const rp_settings* settings,
                rp_error* err) {
    const TypeInfo& info = infoOf(type);
#ifndef RP_DEVICE
    std::string json, layout, why;
    if (!check(info, settings, json, layout, why)) {
        writeError(err, why.c_str());
        return nullptr;
    }
#else
    if (settings && settings->size > 0 && settings->encoding != RP_SETTINGS_MSGPACK) {
        writeError(err, "a device's script node reads msgpack settings alone");
        return nullptr;
    }
#endif
    PackState* pk = packMemory(type, host, err);
    if (!pk) return nullptr;
    Node* node = new (memory) Node{};
    node->base.vt = &kVtable;
    node->type = &info;
    node->pack = pk;
#ifndef RP_DEVICE
    node->settings = new std::string(std::move(json));
    node->layout = std::move(layout);
    ++pk->nodes;
#else
    node->settingsSize = settings ? static_cast<std::uint32_t>(settings->size) : 0;
    if (node->settingsSize) std::memcpy(node + 1, settings->data, node->settingsSize);
#endif
    return &node->base;
}

void destroy(rp_node* n) {
    Node& node = *as(n);
    if (node.slot >= 0) {
        // The block thread frees the slot at its next entry into the pack (the header comment).
        PackState& pk = *node.pack;
        const std::uint32_t head = pk.releaseHead.load(std::memory_order_relaxed);
        pk.releases[head % kMaxInstances] = static_cast<std::uint8_t>(node.slot);
        pk.releaseHead.store(head + 1, std::memory_order_release);
    }
#ifndef RP_DEVICE
    delete node.settings;
    PackState* pk = node.pack;
    node.~Node();
    if (--pk->nodes == 0 && pk->dead) renew(*pk);
#else
    node.~Node();
#endif
}

// ---- the package's memory -------------------------------------------------------------------------------------------

const Pack& registered(const rp_package* package) { return *static_cast<const Pack*>(package->ctx); }

bool queryPackage(const rp_package* package, rp_memory_req* out, rp_error* err) {
    const Pack& pack = registered(package);
    if (heapOf(pack) < kLeastContextBytes) {
        char buf[48];
        Text(buf, sizeof buf) << std::uint64_t{kLeastContextBytes};
        writeError(err, "its heap is under the least context the engine sets itself up in at this width, ", buf,
                   " bytes");
        return false;
    }
    out->size = packBytes(pack);
    out->align = alignof(PackState);
    return true;
}

// The pack's memory as a build of `pointerBytes` takes it: this build's own, or the box's (4), which a 64-bit build
// (the exporter) sizes a box's plan with.
bool queryTarget(const rp_package* package, std::uint32_t pointerBytes, rp_memory_req* out, rp_error* err) {
    if (pointerBytes == sizeof(void*)) return queryPackage(package, out, err);
    if (pointerBytes != 4) {
        writeError(err, "a script pack is sized for a 32-bit target or for this build's own width");
        return false;
    }
    const rp_mqjs_pack_source& source = *registered(package).source;
    if (source.heap_bytes < kBoxLeastContextBytes) {
        char buf[48];
        Text(buf, sizeof buf) << std::uint64_t{kBoxLeastContextBytes};
        writeError(err, "its heap is under the least context the engine sets itself up in at 32 bits, ", buf,
                   " bytes");
        return false;
    }
    out->size = boxPackageBytes(source);
    out->align = 8;
    return true;
}

bool setupPackage(const rp_package* package, void* memory, rp_error* err) {
    const Pack& pack = registered(package);
    const rp_mqjs_pack_source& source = *pack.source;
    PackState* pk = new (memory) PackState{};
    pk->base.api = &kApi;
    pk->base.kind = Kind::Pack;
    pk->pack = &pack;
    pk->contextBytes = heapOf(pack);
    std::uint8_t* code = static_cast<std::uint8_t*>(memory) + kStateBytes;
    pk->contextMemory = code + aligned8(source.bytecode_size);
    char reason[200];
    Text why(reason, sizeof reason);
    JSContext* ctx = loadPack(pk->base, source.bytecode, source.bytecode_size, pk->contextMemory, pk->contextBytes,
                              code, why);
    if (!ctx) {
        writeError(err, reason);
        pk->~PackState();
        return false;
    }
    bool made = false;
    {
        Rooted defs(ctx, definitions(ctx, source, why));
        Rooted root(ctx, JS_IsException(defs.get()) ? JS_EXCEPTION : JS_NewArray(ctx, kRootLength));
        Rooted table(ctx, JS_IsException(root.get()) ? JS_EXCEPTION : JS_NewArray(ctx, kMaxInstances));
        Rooted transport(ctx, JS_IsException(table.get()) ? JS_EXCEPTION
                                                          : JS_NewObjectClassUser(ctx, RP_MQJS_CLASS_TRANSPORT, 0));
        made = !JS_IsException(transport.get());
        if (made) {
            JS_SetPropertyUint32(ctx, root.get(), kRootDefs, defs.get());
            JS_SetPropertyUint32(ctx, root.get(), kRootInstances, table.get());
            JS_SetPropertyUint32(ctx, root.get(), kRootTransport, transport.get());
            *JS_AddGCRef(ctx, &pk->root) = root.get();
        } else if (!why.length) {
            why << "setting it up threw: ";
            describe(ctx, JS_GetException(ctx), why);
        }
    }
    if (!made) {
        writeError(err, reason);
        JS_FreeContext(ctx);
        pk->~PackState();
        return false;
    }
    for (std::uint32_t i = 0; i < kMaxInstances; ++i)
        pk->freeSlots[i] = static_cast<std::uint8_t>(kMaxInstances - 1 - i);
    pk->freeCount = kMaxInstances;
    return true;
}

void teardownPackage(const rp_package*, void* memory) {
    PackState* pk = stateAt(memory);
    if (pk->base.ctx) {  // none after a renew whose setup failed
        JS_DeleteGCRef(pk->base.ctx, &pk->root);
        JS_FreeContext(pk->base.ctx);
    }
    pk->~PackState();
}

// ---- registration ---------------------------------------------------------------------------------------------------

#ifndef RP_DEVICE
std::mutex gRegistration;
// What a control context's calls are held to (setControlClock), under gRegistration.
std::uint64_t (*gControlClock)(void*) = &threadCpuMicros;
std::uint64_t gControlDeadline = kControlDeadlineMicros;

// The pack's control context made and its descriptors read: false, saying why in `why`, when it cannot be.
bool openControl(Pack& pack, Text& why) {
    auto c = std::make_unique<Control>();
    c->memory.reset(new std::uint64_t[kControlBytes / sizeof(std::uint64_t)]);
    const rp_mqjs_pack_source& source = *pack.source;
    // The whole pack, which registers the types, describes them and checks settings; a source without one (none
    // that a desktop build compiles) runs its nodes' bytecode here too.
    const std::uint8_t* whole = source.control_bytecode ? source.control_bytecode : source.bytecode;
    const std::uint32_t size = source.control_bytecode ? source.control_bytecode_size : source.bytecode_size;
    c->bytecode.reset(new std::uint64_t[aligned8(size) / sizeof(std::uint64_t)]);
    c->state.api = &kApi;
    c->state.kind = Kind::Control;
    c->state.clock = gControlClock;
    c->state.limit = gControlDeadline;
    JSContext* ctx = loadPack(c->state, whole, size, c->memory.get(), kControlBytes,
                              reinterpret_cast<std::uint8_t*>(c->bytecode.get()), why);
    if (ctx) {
        Rooted defs(ctx, definitions(ctx, source, why));
        if (!JS_IsException(defs.get())) {
            *JS_AddGCRef(ctx, &c->defs) = defs.get();
            for (std::uint32_t i = 0; i < source.type_count; ++i) {
                Rooted def(ctx, at(ctx, defs.get(), i));
                Rooted fn(ctx, JS_GetPropertyStr(ctx, def.get(), "descriptor"));
                Rooted d(ctx, invoke(c->state, fn, def));
                std::string json;
                if (JS_IsException(d.get()) || !stringify(c->state, d, json)) {
                    why << source.type_ids[i] << "'s descriptor threw: ";
                    describe(ctx, JS_GetException(ctx), why);
                    break;
                }
                c->descriptors.push_back(std::move(json));
            }
        }
    }
    if (!ctx || c->descriptors.size() != source.type_count) {
        if (ctx) JS_FreeContext(ctx);
        return false;
    }
    pack.control = c.release();
    return true;
}
#endif

}  // namespace

const char* packageRefusal() { return gRefusal; }

std::size_t stateBytes() { return sizeof(PackState); }

std::size_t packageBytes(const rp_mqjs_pack_source& source) {
    return kStateBytes + aligned8(source.bytecode_size) + std::size_t{source.heap_bytes} * kDeviceHalves / 2;
}

std::size_t boxPackageBytes(const rp_mqjs_pack_source& source) {
    return kBoxStateBytes + aligned8(source.box_bytecode_size) + source.heap_bytes;
}

#ifndef RP_DEVICE
std::size_t desktopContextBytes(const rp_mqjs_pack_source& source) {
    const std::uint64_t scaled = std::uint64_t{source.heap_bytes} * kWidthFactor * kDesktopHeapFactor;
    return static_cast<std::size_t>(scaled > kDesktopLeastContextBytes ? scaled : kDesktopLeastContextBytes);
}

void setControlClock(std::uint64_t (*clock)(void* ctx), std::uint64_t deadlineMicros) {
    const std::lock_guard<std::mutex> lock(gRegistration);
    gControlClock = clock;
    gControlDeadline = deadlineMicros;
}
#endif

namespace {

const rp_package* registerPack(const rp_mqjs_pack_source& source, Sizing sizing) {
#ifndef RP_DEVICE
    const std::lock_guard<std::mutex> lock(gRegistration);
#endif
    for (std::uint32_t i = 0; i < gPackCount; ++i)
        if (gPacks[i].source == &source && gPacks[i].sizing == sizing) return &gPacks[i].package;
    Text why(gRefusal, sizeof gRefusal);
    const std::size_t nameLength = std::strlen(source.name);
    bool plainName = nameLength > 0 && nameLength <= kMaxNameLength;
    for (std::size_t i = 0; plainName && i < nameLength; ++i)
        plainName = source.name[i] > ' ' && source.name[i] != '"' && source.name[i] != '\\';
    if (!plainName) {
        why << "the pack's name must be 1 to " << std::uint64_t{kMaxNameLength}
            << " characters, none a space, a quote or a backslash";
    } else if (gPackCount >= kMaxPacks) {
        why << "the build registers " << std::uint64_t{kMaxPacks} << " packs at most (RP_MQJS_MAX_PACKS)";
    } else if (source.type_count > kMaxTypes) {
        why << "it defines " << std::uint64_t{source.type_count} << " node types, past the build's "
            << std::uint64_t{kMaxTypes} << " a pack (RP_MQJS_MAX_TYPES)";
    }
    Pack& pack = gPacks[gPackCount < kMaxPacks ? gPackCount : 0];
#ifndef RP_DEVICE
    if (why.length == 0) {
        pack.source = &source;
        if (!openControl(pack, why)) pack.source = nullptr;
    }
#endif
    if (why.length != 0) {
#ifndef RP_DEVICE
        std::fprintf(stderr, "retroplug: the \"%s\" script pack is not registered: %s\n", source.name, gRefusal);
#endif
        return nullptr;
    }
    pack.source = &source;
    pack.sizing = sizing;
    Text(pack.manifest, sizeof pack.manifest) << "{\"id\":\"rp." << source.name << ".script\",\"version\":\"1.0.0\"}";
    for (std::uint32_t i = 0; i < source.type_count; ++i) {
        pack.infos[i] = TypeInfo{&pack, i};
        rp_node_type& t = pack.types[i];
        t.id = source.type_ids[i];
#ifndef RP_DEVICE
        t.descriptor = &descriptor;
        t.layout_for = &layoutFor;
        t.flags = RP_TYPE_STATELESS_PREPARE;
#endif
        t.query_memory = &queryMemory;
        t.create = &create;
        t.destroy = &destroy;
        t.ctx = &pack.infos[i];
        pack.typeList[i] = &t;
    }
    pack.memory = rp_package_memory{source.name, &queryPackage, &setupPackage, &teardownPackage, &queryTarget};
    pack.package = rp_package{RP_ABI_VERSION, pack.manifest, source.type_count, pack.typeList, &pack.memory, &pack};
    ++gPackCount;
    return &pack.package;
}

}  // namespace

#ifdef RP_DEVICE
const rp_package* package(const rp_mqjs_pack_source& source) { return registerPack(source, Sizing::Device); }
#else
const rp_package* package(const rp_mqjs_pack_source& source) { return registerPack(source, Sizing::Desktop); }
const rp_package* exactPackage(const rp_mqjs_pack_source& source) { return registerPack(source, Sizing::Check); }
#endif

#ifndef RP_DEVICE
std::string runScript(std::uint8_t* bytecode, std::size_t size, std::size_t contextBytes,
                      void (*log)(void* ctx, const char* text, std::size_t size), void* logCtx) {
    if (contextBytes < kLeastContextBytes) return "the context is under the least the engine sets itself up in";
    std::vector<std::uint64_t> memory(contextBytes / sizeof(std::uint64_t));
    ContextState s;
    s.api = &kApi;
    s.kind = Kind::Script;
    s.log = log;
    s.logCtx = logCtx;
    JSContext* ctx = JS_NewContext(memory.data(), memory.size() * sizeof(std::uint64_t), &rp_mqjs_stdlib);
    s.ctx = ctx;
    JS_SetContextOpaque(ctx, &s);
    JS_SetLogFunc(ctx, &writeLog);
    std::string result;
    if (JS_RelocateBytecode(ctx, bytecode, static_cast<std::uint32_t>(size)) != 0) {
        result = "not MicroQuickJS bytecode of this width";
    } else {
        rp_mqjs_keep_bytecode_names(bytecode, static_cast<std::uint32_t>(size));
        Rooted main(ctx, JS_LoadBytecode(ctx, bytecode));
        const JSValue r = JS_IsException(main.get()) ? JS_EXCEPTION : JS_Run(ctx, main.get());
        if (JS_IsException(r)) {
            char buf[512];
            Text t(buf, sizeof buf);
            describe(ctx, JS_GetException(ctx), t);
            result = buf;
        }
    }
    JS_FreeContext(ctx);
    return result;
}
#endif

}  // namespace rp::mqjs
