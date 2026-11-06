#include "graph/PlanLoader.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>

#include "graph/PlanBlob.hpp"
#include "graph/Text.hpp"
#include "graph/noinline.hpp"

#if defined(RP_DEVICE) && defined(__GLIBCXX__)
// What a device build of the loader must not carry, as graph/node.hpp checks a node's translation unit: the standard
// library's heap and exception headers (each name is its libstdc++ include guard). The device image's link trap is the
// guarantee; this is the early, readable error.
#if defined(_GLIBCXX_STRING) || defined(_GLIBCXX_VECTOR) || defined(_GLIBCXX_MAP) || defined(_GLIBCXX_OPTIONAL) || \
    defined(_GLIBCXX_STDEXCEPT) || defined(__EXCEPTION__) || defined(_GLIBCXX_IOSTREAM) ||                      \
    defined(_GLIBCXX_FUNCTIONAL) || defined(_GLIBCXX_MEMORY)
#error "the plan loader's device build includes a heap or exception header"
#endif
#endif

namespace rp::graph {

namespace {

using namespace blob;

// ---- error text ---------------------------------------------------------------------------------------------------

// A node's rp_error text, bounded (a node may leave it unterminated), or the fallback when it gave none.
std::string_view reason(const rp_error& e, const char* fallback) {
    std::size_t n = 0;
    while (n < sizeof e.message && e.message[n]) ++n;
    return n ? std::string_view(e.message, n) : std::string_view(fallback);
}

// Starts an error: the code and the node at fault (empty for none), and the sentence to write.
Text fail(PlanLoadError* e, const char* code, std::string_view node = {}) {
    Text(e->code, sizeof e->code) << code;
    Text(e->node, sizeof e->node) << node;
    return Text(e->message, sizeof e->message);
}

// ---- the blob, read in place --------------------------------------------------------------------------------------

template <class T>
T readAt(const std::uint8_t* data, std::uint64_t at) {
    T t;
    std::memcpy(&t, data + at, sizeof t);  // a blob in flash or a byte array need not be aligned for T
    return t;
}

// The blob once its header and section table have been checked: every record read from here is in bounds.
struct Blob {
    const std::uint8_t* data = nullptr;
    std::size_t         size = 0;
    Header              h{};

    std::uint32_t count(Section s) const { return h.sections[static_cast<std::uint32_t>(s)].count; }
    std::uint64_t offset(Section s) const { return h.sections[static_cast<std::uint32_t>(s)].offset; }
    template <class T>
    T record(Section s, std::uint32_t i) const { return readAt<T>(data, offset(s) + std::uint64_t{i} * sizeof(T)); }
    std::uint32_t word(Section s, std::uint32_t i) const { return record<std::uint32_t>(s, i); }
    const std::uint8_t* bytes(Section s, std::uint32_t at) const { return data + offset(s) + at; }
    std::string_view text(const Bytes& b) const {
        return {reinterpret_cast<const char*>(bytes(Section::Strings, b.offset)), b.size};
    }

    NodeRecord node(std::uint32_t i) const { return record<NodeRecord>(Section::Nodes, i); }
    StepRecord step(std::uint32_t i) const { return record<StepRecord>(Section::Steps, i); }
    std::string_view nodeId(std::uint32_t i) const { return text(node(i).id); }
    double sampleRate() const { return joinDouble(h.sampleRate); }
};

// The type registered under `id`, and the package it came from (`package`, when asked), or null.
const rp_node_type* findType(std::span<const rp_package* const> types, std::string_view id,
                             const rp_package** package = nullptr) {
    for (const rp_package* p : types) {
        if (!p || p->abi_version != RP_ABI_VERSION) continue;
        for (std::uint32_t i = 0; i < p->node_type_count; ++i) {
            const rp_node_type* t = p->node_types[i];
            if (t && t->id && std::strlen(t->id) == id.size() && std::memcmp(t->id, id.data(), id.size()) == 0) {
                if (package) *package = p;
                return t;
            }
        }
    }
    return nullptr;
}

// What a package's memory hook calls it, for what is said of it.
std::string_view packageName(const rp_package* p) {
    return p && p->memory && p->memory->name ? std::string_view(p->memory->name) : std::string_view("unnamed");
}

const char* sectionName(Section s) {
    switch (s) {
        case Section::Strings: return "strings";
        case Section::Settings: return "settings";
        case Section::Nodes: return "nodes";
        case Section::Steps: return "steps";
        case Section::Views: return "views";
        case Section::ParamViews: return "param views";
        case Section::Channels: return "channel entries";
        case Section::Queues: return "queues";
        case Section::Streams: return "streams";
        case Section::Clears: return "queues to clear";
        case Section::Rolls: return "streams to roll";
        case Section::Feedback: return "feedback copies";
        case Section::Masks: return "masks";
        case Section::MaskBytes: return "mask bytes";
        case Section::Peers: return "peer lists";
        case Section::PeerNodes: return "peer entries";
        case Section::ParamStarts: return "param starts";
        case Section::Count: break;
    }
    return "?";
}

// The checks that every index and offset a record holds is in bounds, each refusal naming the record and the bound.
struct Checker {
    const Blob&    b;
    PlanLoadError* err;

    // [first, first + count) within `limit` of `what`, for the `field` of record `index` of `section`.
    bool inRange(Section section, std::uint32_t at, const char* field, std::uint64_t first, std::uint64_t count,
                 std::uint64_t limit, const char* what) {
        if (first <= limit && count <= limit - first) return true;
        fail(err, "plan-malformed") << sectionName(section) << " record " << std::uint64_t{at} << "'s " << field
                                    << " [" << first << ", " << first + count << ") runs past the plan's " << limit
                                    << " " << what;
        return false;
    }
    bool isIndex(Section section, std::uint32_t at, const char* field, std::uint64_t value, std::uint64_t limit,
                 const char* what) {
        if (value < limit) return true;
        fail(err, "plan-malformed") << sectionName(section) << " record " << std::uint64_t{at} << " names " << field
                                    << " " << value << ", past the plan's " << limit << " " << what;
        return false;
    }
    bool badKind(Section section, std::uint32_t at, std::uint32_t kind) {
        fail(err, "plan-malformed") << sectionName(section) << " record " << std::uint64_t{at} << " has port kind "
                                    << std::uint64_t{kind} << ", which it cannot have";
        return false;
    }
    std::uint32_t n(Section s) const { return b.count(s); }

    // A run of the channel table, a queue, a stream or a cell, by port kind: what a view or a feedback copy holds.
    bool port(Section section, std::uint32_t at, std::uint32_t kind, std::uint32_t which, std::uint32_t count) {
        switch (kind) {
            case RP_PORT_AUDIO:
                return inRange(section, at, "channels", which, count, n(Section::Channels), "channel entries");
            case RP_PORT_EVENTS:
            case RP_PORT_BYTES: return isIndex(section, at, "queue", which, n(Section::Queues), "queues");
            case RP_PORT_CONTROL: return isIndex(section, at, "stream", which, n(Section::Streams), "streams");
            case RP_PORT_RESOURCE: return isIndex(section, at, "cell", which, b.h.cells, "resource cells");
            case RP_PORT_LINK: return true;
            default: return badKind(section, at, kind);
        }
    }

    bool all() {
        const std::uint32_t nodes = n(Section::Nodes), steps = n(Section::Steps);
        for (std::uint32_t i = 0; i < nodes; ++i) {
            const NodeRecord r = b.node(i);
            if (!inRange(Section::Nodes, i, "id", r.id.offset, r.id.size, n(Section::Strings), "string bytes") ||
                !inRange(Section::Nodes, i, "type", r.type.offset, r.type.size, n(Section::Strings), "string bytes") ||
                !inRange(Section::Nodes, i, "settings", r.settings.offset, r.settings.size, n(Section::Settings),
                     "settings bytes") ||
                !isIndex(Section::Nodes, i, "group", r.group, nodes, "nodes") ||
                !isIndex(Section::Nodes, i, "step", r.step, steps, "steps"))
                return false;
        }
        // One step per node, each the step its node names, so every node runs exactly once.
        if (steps != nodes) {
            fail(err, "plan-malformed") << "the plan has " << std::uint64_t{nodes} << " nodes and "
                                        << std::uint64_t{steps} << " steps, one per node";
            return false;
        }
        std::uint32_t groupStart = 0, groupEnd = 0;  // the coupled group a step is in: [groupStart, groupEnd)
        for (std::uint32_t i = 0; i < steps; ++i) {
            const StepRecord s = b.step(i);
            if (!isIndex(Section::Steps, i, "node", s.node, nodes, "nodes") ||
                !inRange(Section::Steps, i, "inputs", s.firstInput, s.inputCount, n(Section::Views), "views") ||
                !inRange(Section::Steps, i, "outputs", s.firstOutput, s.outputCount, n(Section::Views), "views") ||
                !inRange(Section::Steps, i, "params", s.firstParam, s.paramCount, n(Section::ParamViews),
                         "param views"))
                return false;
            if (b.node(s.node).step != i) {
                fail(err, "plan-malformed") << "step " << std::uint64_t{i} << " runs node " << std::uint64_t{s.node}
                                            << ", whose step is " << std::uint64_t{b.node(s.node).step};
                return false;
            }
            // A group's members follow its first, which alone carries its size, and share its group.
            const bool inGroup = i < groupEnd;
            if (s.groupSize == 1 || (s.groupSize > 1 && (inGroup || s.groupSize > steps - i)) ||
                (inGroup && b.node(s.node).group != b.node(b.step(groupStart).node).group)) {
                fail(err, "plan-malformed") << "step " << std::uint64_t{i} << "'s coupled group of "
                                            << std::uint64_t{s.groupSize} << " does not fit the schedule";
                return false;
            }
            if (s.groupSize > 1) {
                groupStart = i;
                groupEnd = i + s.groupSize;
            }
        }
        for (std::uint32_t i = 0; i < n(Section::Views); ++i) {
            const auto v = b.record<ViewRecord>(Section::Views, i);
            if (!port(Section::Views, i, v.kind, v.index, v.count)) return false;
        }
        for (std::uint32_t i = 0; i < n(Section::ParamViews); ++i) {
            const auto v = b.record<ParamViewRecord>(Section::ParamViews, i);
            if (!isIndex(Section::ParamViews, i, "stream", v.stream, n(Section::Streams), "streams")) return false;
            if (v.wired > 1) {
                fail(err, "plan-malformed") << "param views record " << std::uint64_t{i} << " has a wired flag of "
                                            << std::uint64_t{v.wired} << ", not 0 or 1";
                return false;
            }
        }
        for (std::uint32_t i = 0; i < n(Section::Channels); ++i)
            if (!isIndex(Section::Channels, i, "buffer", b.word(Section::Channels, i), b.h.buffers, "audio buffers"))
                return false;
        for (std::uint32_t i = 0; i < n(Section::Clears); ++i) {
            const auto c = b.record<ClearRecord>(Section::Clears, i);
            if (!isIndex(Section::Clears, i, "queue", c.queue, n(Section::Queues), "queues") ||
                (c.node != kNoNode && !isIndex(Section::Clears, i, "node", c.node, nodes, "nodes")))
                return false;
        }
        for (std::uint32_t i = 0; i < n(Section::Rolls); ++i)
            if (!isIndex(Section::Rolls, i, "stream", b.word(Section::Rolls, i), n(Section::Streams), "streams"))
                return false;
        for (std::uint32_t i = 0; i < n(Section::Feedback); ++i) {
            const auto f = b.record<FeedbackRecord>(Section::Feedback, i);
            if (f.kind == RP_PORT_LINK || f.kind == RP_PORT_RESOURCE) return badKind(Section::Feedback, i, f.kind);
            if (!port(Section::Feedback, i, f.kind, f.back, f.channels) ||
                !port(Section::Feedback, i, f.kind, f.front, f.channels))
                return false;
            if (f.kind == RP_PORT_AUDIO && f.frames > b.h.maxFrames) {
                fail(err, "plan-malformed") << "feedback copy " << std::uint64_t{i} << " holds "
                                            << std::uint64_t{f.frames} << " frames, past the plan's maxFrames of "
                                            << std::uint64_t{b.h.maxFrames};
                return false;
            }
        }
        for (std::uint32_t i = 0; i < n(Section::Masks); ++i) {
            const auto m = b.record<MaskRecord>(Section::Masks, i);
            if (!isIndex(Section::Masks, i, "node", m.node, nodes, "nodes") ||
                !inRange(Section::Masks, i, "flags", m.offset, m.count, n(Section::MaskBytes), "mask bytes"))
                return false;
            if (m.count != b.step(b.node(m.node).step).outputCount) {
                fail(err, "plan-malformed") << "the mask for " << std::uint64_t{m.node} << " has "
                                            << std::uint64_t{m.count} << " flags, for a node of "
                                            << std::uint64_t{b.step(b.node(m.node).step).outputCount} << " outputs";
                return false;
            }
        }
        for (std::uint32_t i = 0; i < n(Section::Peers); ++i) {
            const auto p = b.record<PeerRecord>(Section::Peers, i);
            if (!isIndex(Section::Peers, i, "node", p.node, nodes, "nodes") ||
                !inRange(Section::Peers, i, "peers", p.first, p.count, n(Section::PeerNodes), "peer entries"))
                return false;
        }
        for (std::uint32_t i = 0; i < n(Section::PeerNodes); ++i)
            if (!isIndex(Section::PeerNodes, i, "node", b.word(Section::PeerNodes, i), nodes, "nodes")) return false;
        for (std::uint32_t i = 0; i < n(Section::ParamStarts); ++i) {
            const auto s = b.record<ParamStartRecord>(Section::ParamStarts, i);
            if (!isIndex(Section::ParamStarts, i, "stream", s.stream, n(Section::Streams), "streams")) return false;
        }
        return true;
    }
};

// The blob's header, its length, its CRC, its section table, then every record and every type id.
bool check(std::span<const std::uint8_t> data, std::span<const rp_package* const> types, Blob& b,
           PlanLoadError* err) {
    b.data = data.data();
    b.size = data.size();
    if (b.size < 8 || std::memcmp(b.data, kMagic, sizeof kMagic) != 0) {
        fail(err, "plan-format") << "not a plan blob: it does not start with \"RPPL\"";
        return false;
    }
    const auto version = readAt<std::uint32_t>(b.data, 4);
    if (version != kVersion) {
        fail(err, "plan-version") << "plan format " << std::uint64_t{version} << ", this loader reads "
                                  << std::uint64_t{kVersion};
        return false;
    }
    if (b.size < sizeof(Header) + sizeof(std::uint32_t)) {
        fail(err, "plan-truncated") << "the blob is " << std::uint64_t{b.size} << " bytes, shorter than its header";
        return false;
    }
    b.h = readAt<Header>(b.data, 0);
    if (b.h.size != b.size) {
        fail(err, "plan-truncated") << "the blob is " << std::uint64_t{b.size} << " bytes, its header says "
                                    << std::uint64_t{b.h.size};
        return false;
    }
    const std::uint64_t body = b.size - sizeof(std::uint32_t);
    const auto stored = readAt<std::uint32_t>(b.data, body);
    const std::uint32_t computed = crc32(b.data, static_cast<std::size_t>(body));
    if (stored != computed) {
        Text t = fail(err, "plan-crc");
        t << "the blob's CRC is ";
        t.hex(stored) << ", its bytes give ";
        t.hex(computed);
        return false;
    }
    if (b.h.target != kTargetRp2350) {
        fail(err, "plan-target") << "the plan is for target " << std::uint64_t{b.h.target} << ", this loader runs "
                                 << kTargetName << " plans";
        return false;
    }
    const double rate = b.sampleRate();
    if (b.h.maxFrames == 0 || !std::isfinite(rate) || rate <= 0.0) {
        fail(err, "plan-malformed") << "the plan's sample rate and maxFrames must be positive";
        return false;
    }
    if (b.h.sectionCount != kSectionCount) {
        fail(err, "plan-malformed") << "the plan has " << std::uint64_t{b.h.sectionCount} << " sections, not "
                                    << std::uint64_t{kSectionCount};
        return false;
    }
    for (std::uint32_t i = 0; i < kSectionCount; ++i) {
        const SectionEntry& s = b.h.sections[i];
        const auto section = static_cast<Section>(i);
        if (s.recordBytes != recordBytes(section)) {
            fail(err, "plan-malformed") << "the " << sectionName(section) << " section's records are "
                                        << std::uint64_t{s.recordBytes} << " bytes, not "
                                        << std::uint64_t{recordBytes(section)};
            return false;
        }
        const std::uint64_t bytes = std::uint64_t{s.count} * s.recordBytes;
        if (s.offset % 4 != 0 || s.offset < sizeof(Header) || s.offset > body || bytes > body - s.offset) {
            fail(err, "plan-malformed") << "the " << sectionName(section) << " section [" << std::uint64_t{s.offset}
                                        << ", " << s.offset + bytes << ") is not within the blob's "
                                        << body << " bytes";
            return false;
        }
    }
    if (!Checker{b, err}.all()) return false;
    for (std::uint32_t i = 0; i < b.count(Section::Nodes); ++i) {
        const std::string_view type = b.text(b.node(i).type);
        if (!findType(types, type)) {
            Text t = fail(err, "unknown-type", b.nodeId(i));
            t << "no node type ";
            t.quoted(type) << " is registered";
            return false;
        }
    }
    return true;
}

// ---- the layout ---------------------------------------------------------------------------------------------------

constexpr std::uint64_t kUnlimited = ~std::uint64_t{0};

// Bump allocation from offset 0, over at most `limit` bytes of `base`. With no base it only counts (the exporter's
// sizing pass); with one, it hands out zeroed memory while the request ends within the limit, and past it keeps
// counting and hands out nothing, so one pass both lays a plan out and finds how far past the arena it would run.
struct Bump {
    std::byte*    base = nullptr;  // null: counting only
    std::uint64_t limit = kUnlimited;
    std::uint64_t used = 0;
    bool          overflow = false;

    // The offset of `bytes` aligned to `align`, or past the end (nothing to hand out) when the sums would wrap.
    std::uint64_t take(std::uint64_t bytes, std::uint64_t align) {
        const std::uint64_t at = (used + (align - 1)) & ~(align - 1);
        // Past 2^48 bytes no arena is that large, and the sums below cannot wrap.
        if (at < used || bytes > (std::uint64_t{1} << 48) || at + bytes > (std::uint64_t{1} << 48)) {
            overflow = true;
            return used;
        }
        used = at + bytes;
        return at;
    }
    std::byte* give(std::uint64_t at) {
        if (!base || overflow || used > limit) return nullptr;
        std::memset(base + at, 0, static_cast<std::size_t>(used - at));
        return base + at;
    }
    // a * b, or a size no arena holds where that would wrap.
    static std::uint64_t product(std::uint64_t a, std::uint64_t b) {
        return a && b > (std::uint64_t{1} << 62) / a ? std::uint64_t{1} << 62 : a * b;
    }
    template <class T>
    T* array(std::uint64_t count) {
        return reinterpret_cast<T*>(give(take(product(count, sizeof(T)), alignof(T))));
    }
    std::byte* raw(std::uint64_t bytes, std::uint64_t align) { return give(take(bytes, align)); }
};

// Where layout() put everything (placing), or nulls (measuring).
struct Placed {
    LoadedPlan*        plan = nullptr;
    LoadedNode*        nodes = nullptr;
    PlanStep*          steps = nullptr;
    rp_port_view*      views = nullptr;
    rp_control_queue** paramViews = nullptr;
    std::uint8_t*      wired = nullptr;  // per param view
    float**            channels = nullptr;
    std::byte*         buffers = nullptr;
    std::uint64_t      bufferStride = 0;
    rp_queue*          queues = nullptr;
    rp_control_queue*  streams = nullptr;
    rp_resource**      cells = nullptr;
    OutputQueue*       clears = nullptr;
    rp_control_queue** rolls = nullptr;
    FeedbackCopy*      feedback = nullptr;
    rp_node**          peers = nullptr;
    LoadedPackage*     packages = nullptr;
    std::uint32_t      packageCount = 0;
};

// Node `i`'s package when it has memory of its own and no earlier node's type comes from it, else null: what places
// each such package once, where the plan first names it. Out of line, as placePackages is: a device's loader keeps one
// copy of each.
RP_NOINLINE const rp_package* newPackage(const Blob& b, std::span<const rp_package* const> types, std::uint32_t i) {
    const rp_package* package = nullptr;
    findType(types, b.text(b.node(i).type), &package);  // check() found every one
    if (!package || !package->memory) return nullptr;
    for (std::uint32_t j = 0; j < i; ++j) {
        const rp_package* earlier = nullptr;
        findType(types, b.text(b.node(j).type), &earlier);
        if (earlier == package) return nullptr;
    }
    return package;
}

std::uint32_t countPackages(const Blob& b, std::span<const rp_package* const> types) {
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < b.count(Section::Nodes); ++i)
        if (newPackage(b, types, i)) ++n;
    return n;
}

rp_settings settingsOf(const Blob& b, const NodeRecord& r) {
    return {reinterpret_cast<const char*>(b.bytes(Section::Settings, r.settings.offset)), r.settings.size,
            RP_SETTINGS_MSGPACK};
}

// What placePackages placed: the packages' memory in all, and the first package whose memory ends past the arena.
struct Placement {
    std::uint64_t     bytes = 0;
    const rp_package* tip = nullptr;
    std::uint64_t     tipBytes = 0;
};

// The plan's packages with memory of their own (layout's), each queried and placed: false, with `err` filled, for one
// that cannot be sized or asks an alignment the target does not give. A `pointerBytes` other than 0 sizes each for a
// target of that width (rp_package_memory's query_target, where a package has one): the exporter's measure, never a
// placement.
RP_NOINLINE bool placePackages(const Blob& b, std::span<const rp_package* const> types, Bump& bump, Placed& p,
                               std::uint64_t limit, std::uint32_t pointerBytes, Placement& out, PlanLoadError* err) {
    const std::uint32_t nodes = b.count(Section::Nodes);
    p.packageCount = countPackages(b, types);
    p.packages = bump.array<LoadedPackage>(p.packageCount);
    for (std::uint32_t i = 0, k = 0; i < nodes; ++i) {
        const rp_package* package = newPackage(b, types, i);
        if (!package) continue;
        const rp_package_memory& hooks = *package->memory;
        rp_memory_req req{};
        rp_error packageErr{};
        if (!hooks.query || !hooks.setup || !hooks.teardown) {
            (fail(err, "package-memory") << "the ").quoted(packageName(package))
                << " pack has package memory without its query, setup and teardown";
            return false;
        }
        const bool sized = pointerBytes && hooks.query_target
                               ? hooks.query_target(package, pointerBytes, &req, &packageErr)
                               : hooks.query(package, &req, &packageErr);
        if (!sized) {
            (fail(err, "package-memory") << "the ").quoted(packageName(package))
                << " pack's memory cannot be sized: " << reason(packageErr, "the package refused");
            return false;
        }
        const std::uint64_t align = req.align ? req.align : kTargetAlign;
        if ((align & (align - 1)) != 0 || align > kTargetAlign) {
            (fail(err, "package-memory") << "the ").quoted(packageName(package))
                << " pack asked for an alignment of " << align << ", where an " << kTargetName
                << " arena gives a power of two up to " << std::uint64_t{kTargetAlign};
            return false;
        }
        std::byte* memory = bump.raw(req.size ? req.size : 1, align);
        out.bytes += req.size;
        if (!out.tip && bump.used > limit) {
            out.tip = package;
            out.tipBytes = req.size;
        }
        if (p.packages) new (&p.packages[k]) LoadedPackage{package, memory, req.size, false};
        ++k;
    }
    return true;
}

// The plan's whole layout in the arena: its own structures and pools first, then each node's instance in the
// document's order, placed while they fit the bump's base (when it has one). It fills `size` and, past `limit` bytes,
// refuses naming the first node whose instance does not fit, or the plan's own structures when they alone do not.
bool layout(const Blob& b, std::span<const rp_package* const> types, Bump& bump, Placed& p, std::uint64_t limit,
            std::uint32_t pointerBytes, PlanSize* size, PlanLoadError* err) {
    const std::uint32_t nodes = b.count(Section::Nodes);
    p.plan = bump.array<LoadedPlan>(1);
    p.nodes = bump.array<LoadedNode>(nodes);
    p.steps = bump.array<PlanStep>(b.count(Section::Steps));
    p.views = bump.array<rp_port_view>(b.count(Section::Views));
    p.paramViews = bump.array<rp_control_queue*>(b.count(Section::ParamViews));
    p.wired = bump.array<std::uint8_t>(b.count(Section::ParamViews));
    p.channels = bump.array<float*>(b.count(Section::Channels));
    p.bufferStride = (std::uint64_t{b.h.maxFrames} * sizeof(float) + (kArenaAlign - 1)) & ~(kArenaAlign - 1);
    p.buffers = bump.raw(Bump::product(p.bufferStride, b.h.buffers), kArenaAlign);
    p.queues = bump.array<rp_queue>(b.count(Section::Queues));
    for (std::uint32_t i = 0; i < b.count(Section::Queues); ++i) {
        const auto q = b.record<QueueRecord>(Section::Queues, i);
        auto* items = bump.array<rp_message>(q.messages);
        auto* bytes = bump.array<std::uint8_t>(q.bytes);
        if (p.queues) p.queues[i] = rp_queue{items, 0, q.messages, bytes, 0, q.bytes, 0};
    }
    p.streams = bump.array<rp_control_queue>(b.count(Section::Streams));
    for (std::uint32_t i = 0; i < b.count(Section::Streams); ++i) {
        const auto s = b.record<StreamRecord>(Section::Streams, i);
        auto* changes = bump.array<rp_control_change>(s.changes);
        if (p.streams) p.streams[i] = rp_control_queue{joinDouble(s.start), changes, 0, s.changes, 0};
    }
    p.cells = bump.array<rp_resource*>(b.h.cells);
    p.clears = bump.array<OutputQueue>(b.count(Section::Clears));
    p.rolls = bump.array<rp_control_queue*>(b.count(Section::Rolls));
    p.feedback = bump.array<FeedbackCopy>(b.count(Section::Feedback));
    p.peers = bump.array<rp_node*>(b.count(Section::PeerNodes));
    const std::uint64_t planBytes = bump.used;

    // The packages with memory of their own: each once, in the order the plan first names one of its types, after the
    // plan's own structures and before every node, since each is set up before its first node is created.
    Placement packages;
    if (!placePackages(b, types, bump, p, limit, pointerBytes, packages, err)) return false;
    const std::uint64_t packageBytes = packages.bytes;
    const std::uint32_t packageCount = p.packageCount;
    const rp_package*   packageTip = packages.tip;
    const std::uint64_t packageTipBytes = packages.tipBytes;

    std::uint64_t instanceBytes = 0;
    std::uint32_t tip = kNoNode;  // the first node whose instance ends past the arena
    for (std::uint32_t i = 0; i < nodes; ++i) {
        const NodeRecord r = b.node(i);
        const rp_node_type* type = findType(types, b.text(r.type));  // check() found every one
        const rp_settings settings = settingsOf(b, r);
        rp_memory_req req{};
        rp_error nodeErr{};
        if (!type->query_memory(type, &settings, &req, &nodeErr)) {
            fail(err, "settings-invalid", b.nodeId(i)) << reason(nodeErr, "the node refused its settings");
            return false;
        }
        const std::uint64_t align = req.align ? req.align : kTargetAlign;  // 0 asks for nothing in particular
        if ((align & (align - 1)) != 0 || align > kTargetAlign) {
            fail(err, "node-memory", b.nodeId(i))
                << "node asked for an alignment of " << align << ", where an " << kTargetName
                << " arena gives a power of two up to " << std::uint64_t{kTargetAlign};
            return false;
        }
        std::byte* memory = bump.raw(req.size ? req.size : 1, align);
        instanceBytes += req.size;
        if (tip == kNoNode && bump.used > limit) tip = i;
        if (p.nodes) {
            LoadedNode& n = *new (&p.nodes[i]) LoadedNode{};
            const std::string_view id = b.nodeId(i);
            n.id = id.data();
            n.idSize = static_cast<std::uint32_t>(id.size());
            n.type = type;
            n.memory = memory;
            n.memoryBytes = req.size;
            n.step = r.step;
        }
    }
    if (bump.overflow) {
        fail(err, "arena") << "the plan's sizes overflow what any arena holds";
        return false;
    }
    if (size) {
        // Saturated, where a 32-bit size_t cannot hold the total (a plan no device arena holds either).
        constexpr std::uint64_t kMaxSize = std::numeric_limits<std::size_t>::max();
        size->bytes = static_cast<std::size_t>(bump.used < kMaxSize ? bump.used : kMaxSize);
        size->instanceBytes = static_cast<std::size_t>(instanceBytes < kMaxSize ? instanceBytes : kMaxSize);
        size->packageBytes = static_cast<std::size_t>(packageBytes < kMaxSize ? packageBytes : kMaxSize);
        size->structureBytes = static_cast<std::size_t>(planBytes < kMaxSize ? planBytes : kMaxSize);
        size->nodes = nodes;
        size->packages = packageCount;
    }
    if (bump.used > limit) {
        const std::uint64_t over = bump.used - limit;
        if (planBytes > limit) {
            fail(err, "arena") << "the plan's buffers, queues and steps take " << planBytes << " bytes, past the "
                               << limit << "-byte arena by " << planBytes - limit
                               << " before any node; the whole plan needs " << bump.used;
        } else if (packageTip) {
            (fail(err, "arena") << "the ").quoted(packageName(packageTip))
                << " pack's memory (" << packageTipBytes << " bytes) does not fit: the plan needs " << bump.used
                << " bytes, " << over << " past the " << limit << "-byte arena";
        } else {
            const NodeRecord r = b.node(tip);
            fail(err, "arena", b.nodeId(tip))
                .quoted(b.nodeId(tip)) << " (" << b.text(r.type) << ") does not fit: the plan needs " << bump.used
                << " bytes, " << over << " past the " << limit << "-byte arena";
        }
        return false;
    }
    return true;
}

// Every pool's contents and every pointer between them, from the records: nothing here calls a node.
void bind(const Blob& b, const Placed& p) {
    for (std::uint32_t i = 0; i < b.count(Section::Channels); ++i)
        p.channels[i] = reinterpret_cast<float*>(p.buffers + p.bufferStride * b.word(Section::Channels, i));
    for (std::uint32_t i = 0; i < b.count(Section::Views); ++i) {
        const auto r = b.record<ViewRecord>(Section::Views, i);
        rp_port_view& v = p.views[i];
        v.kind = r.kind;
        switch (r.kind) {
            case RP_PORT_AUDIO: v.u.audio = rp_audio_view{p.channels + r.index, r.count}; break;
            case RP_PORT_EVENTS:
            case RP_PORT_BYTES: v.u.queue = &p.queues[r.index]; break;
            case RP_PORT_CONTROL: v.u.control = &p.streams[r.index]; break;
            case RP_PORT_RESOURCE: v.u.resource = &p.cells[r.index]; break;
            default: break;
        }
    }
    for (std::uint32_t i = 0; i < b.count(Section::ParamViews); ++i) {
        const auto v = b.record<ParamViewRecord>(Section::ParamViews, i);
        p.paramViews[i] = &p.streams[v.stream];
        p.wired[i] = static_cast<std::uint8_t>(v.wired);
    }
    for (std::uint32_t i = 0; i < b.count(Section::Steps); ++i) {
        const StepRecord s = b.step(i);
        PlanStep& step = *new (&p.steps[i]) PlanStep{};
        step.process.inputs = p.views + s.firstInput;
        step.process.input_count = s.inputCount;
        step.process.outputs = p.views + s.firstOutput;
        step.process.output_count = s.outputCount;
        step.process.params = p.paramViews + s.firstParam;
        step.process.param_count = s.paramCount;
        step.groupSize = s.groupSize;
        step.faulted = &p.nodes[s.node].faulted;
    }
    for (std::uint32_t i = 0; i < b.count(Section::Clears); ++i) {
        const auto c = b.record<ClearRecord>(Section::Clears, i);
        p.clears[i] = OutputQueue{&p.queues[c.queue], c.node == kNoNode ? nullptr : &p.nodes[c.node].drops};
    }
    for (std::uint32_t i = 0; i < b.count(Section::Rolls); ++i) p.rolls[i] = &p.streams[b.word(Section::Rolls, i)];
    for (std::uint32_t i = 0; i < b.count(Section::Feedback); ++i) {
        const auto r = b.record<FeedbackRecord>(Section::Feedback, i);
        FeedbackCopy& f = p.feedback[i];
        f = FeedbackCopy{};
        f.kind = static_cast<rp_port_kind>(r.kind);
        if (r.kind == RP_PORT_AUDIO) {
            f.backChannels = p.channels + r.back;
            f.frontChannels = p.channels + r.front;
            f.channels = r.channels;
            f.capacity = r.frames;
        } else if (r.kind == RP_PORT_CONTROL) {
            f.backControl = &p.streams[r.back];
            f.frontControl = &p.streams[r.front];
        } else {
            f.backQueue = &p.queues[r.back];
            f.frontQueue = &p.queues[r.front];
        }
    }
}

// Tears down every package set up, the last first.
void tearDown(LoadedPackage* packages, std::uint32_t count) {
    while (count-- > 0) {
        LoadedPackage& pk = packages[count];
        if (!pk.setUp) continue;
        pk.package->memory->teardown(pk.package, pk.memory);
        pk.setUp = false;
    }
}

// Deactivates and destroys nodes [0, made), the last first, then tears the packages down: what a plan unwinds to.
void unwind(LoadedNode* nodes, std::uint32_t made, LoadedPackage* packages, std::uint32_t packageCount) {
    while (made-- > 0) {
        LoadedNode& n = nodes[made];
        if (!n.node) continue;
        if (n.active) n.node->vt->deactivate(n.node);
        n.type->destroy(n.node);
        n.node = nullptr;
        n.active = false;
    }
    tearDown(packages, packageCount);
}

// The loader's own host, which a loaded plan's nodes are created under: rp.host.packages/1 from the plan's packages,
// everything else the caller's host's (LoadedPlan::outer).
const void* loaderInterface(const rp_host* host, const char* id) {
    const auto* plan = static_cast<const LoadedPlan*>(host->ctx);
    if (std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0) return &plan->packageService;
    return plan->outer && plan->outer->get_interface ? plan->outer->get_interface(plan->outer, id) : nullptr;
}
void* loaderAlloc(void* ctx, std::size_t size, std::size_t align) {
    const rp_host* outer = static_cast<const LoadedPlan*>(ctx)->outer;
    return outer->alloc(outer->ctx, size, align);
}
void loaderFree(void* ctx, void* ptr) {
    const rp_host* outer = static_cast<const LoadedPlan*>(ctx)->outer;
    outer->free(outer->ctx, ptr);
}
void loaderLog(void* ctx, int level, const char* message) {
    const rp_host* outer = static_cast<const LoadedPlan*>(ctx)->outer;
    if (outer && outer->log) outer->log(outer->ctx, level, message);
}
void* packageMemory(void* ctx, const rp_package* package, rp_error* err) {
    for (LoadedPackage& pk : static_cast<LoadedPlan*>(ctx)->packages)
        if (pk.package == package && pk.setUp) return pk.memory;
    if (err) Text(err->message, sizeof err->message) << "the plan sets up no memory for its package";
    return nullptr;
}

// Set up every package, then create and activate every node, bind the steps to them, then what PlanStorage::adopt
// applies: peers, connected outputs, param starts.
bool runUp(const Blob& b, const Placed& p, const rp_host* host, PlanLoadError* err) {
    const std::uint32_t nodes = b.count(Section::Nodes);
    const double rate = b.sampleRate();
    for (std::uint32_t k = 0; k < p.packageCount; ++k) {
        LoadedPackage& pk = p.packages[k];
        rp_error packageErr{};
        if (!pk.package->memory->setup(pk.package, pk.memory, &packageErr)) {
            (fail(err, "package-setup") << "the ").quoted(pk.name())
                << " pack would not set up in its memory: " << reason(packageErr, "the package refused");
            tearDown(p.packages, k);
            return false;
        }
        pk.setUp = true;
    }
    for (std::uint32_t i = 0; i < nodes; ++i) {
        LoadedNode& n = p.nodes[i];
        const rp_settings settings = settingsOf(b, b.node(i));
        rp_error nodeErr{};
        n.node = n.type->create(n.type, host, n.memory, &settings, &nodeErr);
        if (!n.node) {
            fail(err, "create-failed", n.name()) << reason(nodeErr, "the node could not be created");
            unwind(p.nodes, i, p.packages, p.packageCount);
            return false;
        }
        if (!n.node->vt->activate(n.node, rate, b.h.maxFrames, &nodeErr)) {
            fail(err, "activate-failed", n.name()) << reason(nodeErr, "the node could not be activated");
            unwind(p.nodes, i + 1, p.packages, p.packageCount);
            return false;
        }
        n.active = true;
    }
    for (std::uint32_t i = 0, groupEnd = 0; i < b.count(Section::Steps); ++i) {
        PlanStep& step = p.steps[i];
        LoadedNode& n = p.nodes[b.step(i).node];
        step.node = n.node;
        if (step.groupSize > 1) groupEnd = i + step.groupSize;
        if (i < groupEnd) {
            step.step = static_cast<const rp_step_interface*>(n.node->vt->get_interface(n.node, RP_STEP_INTERFACE));
            if (!step.step) {
                fail(err, "link-without-step", n.name())
                    .quoted(n.name()) << " is coupled but does not implement " RP_STEP_INTERFACE;
                unwind(p.nodes, nodes, p.packages, p.packageCount);
                return false;
            }
        }
    }
    const std::uint32_t peerLists = b.count(Section::Peers);
    for (std::uint32_t i = 0; i < peerLists; ++i) {
        const LoadedNode& n = p.nodes[b.record<PeerRecord>(Section::Peers, i).node];
        if (!n.node->vt->get_interface(n.node, RP_LINK_INTERFACE)) {
            fail(err, "peers-without-link", n.name()).quoted(n.name()) << " has peers but does not implement "
                                                                          RP_LINK_INTERFACE;
            unwind(p.nodes, nodes, p.packages, p.packageCount);
            return false;
        }
    }
    for (std::uint32_t i = 0; i < peerLists; ++i) {
        const auto r = b.record<PeerRecord>(Section::Peers, i);
        rp_node* const node = p.nodes[r.node].node;
        for (std::uint32_t k = 0; k < r.count; ++k)
            p.peers[r.first + k] = p.nodes[b.word(Section::PeerNodes, r.first + k)].node;
        const auto* link = static_cast<const rp_link_interface*>(node->vt->get_interface(node, RP_LINK_INTERFACE));
        link->set_peers(node, p.peers + r.first, r.count);
    }
    for (std::uint32_t i = 0; i < b.count(Section::Masks); ++i) {
        const auto m = b.record<MaskRecord>(Section::Masks, i);
        rp_node* const node = p.nodes[m.node].node;
        node->vt->ports_changed(node, b.bytes(Section::MaskBytes, m.offset), m.count);
    }
    for (std::uint32_t i = 0; i < b.count(Section::ParamStarts); ++i) {
        const auto s = b.record<ParamStartRecord>(Section::ParamStarts, i);
        p.streams[s.stream].start = joinDouble(s.value);
        p.streams[s.stream].count = 0;
    }
    return true;
}

}  // namespace

std::string_view LoadedPackage::name() const { return packageName(package); }

LoadedNode* LoadedPlan::find(std::string_view id) const {
    for (LoadedNode& n : nodes)
        if (n.name() == id) return &n;
    return nullptr;
}

rp_control_queue* LoadedPlan::param(std::uint32_t n, std::uint32_t k) const {
    if (n >= nodes.size()) return nullptr;
    const rp_process& p = view.steps[nodes[n].step].process;
    if (k >= p.param_count) return nullptr;
    const auto at = static_cast<std::size_t>(p.params + k - paramViews);
    return wired[at] ? nullptr : p.params[k];
}

namespace {

// A caller that wants no error gets one of these, in a frame of its own, so a caller that passes one (a device, whose
// stack the error's 352 bytes would otherwise sit on under the whole load) carries none of it: -Wstack-usage holds a
// device's frames to cmake/device-image.cmake's limit.
RP_NOINLINE bool sizePlanIgnoring(std::span<const std::uint8_t> blob, std::span<const rp_package* const> types,
                                  std::size_t arenaBytes, PlanSize* size, std::uint32_t targetPointerBytes) {
    PlanLoadError ignored;
    return sizePlan(blob, types, arenaBytes, size, &ignored, targetPointerBytes);
}
RP_NOINLINE LoadedPlan* loadPlanIgnoring(std::span<const std::uint8_t> blob, std::span<std::byte> arena,
                                         std::span<const rp_package* const> types, const rp_host* host) {
    PlanLoadError ignored;
    return loadPlan(blob, arena, types, host, &ignored);
}

}  // namespace

bool sizePlan(std::span<const std::uint8_t> blob, std::span<const rp_package* const> types, std::size_t arenaBytes,
              PlanSize* size, PlanLoadError* error, std::uint32_t targetPointerBytes) {
    if (!error) return sizePlanIgnoring(blob, types, arenaBytes, size, targetPointerBytes);
    Blob b;
    if (!check(blob, types, b, error)) return false;
    Bump measure;
    Placed none;
    return layout(b, types, measure, none, arenaBytes ? arenaBytes : kUnlimited, targetPointerBytes, size, error);
}

LoadedPlan* loadPlan(std::span<const std::uint8_t> blob, std::span<std::byte> arena,
                     std::span<const rp_package* const> types, const rp_host* host, PlanLoadError* error) {
    if (!error) return loadPlanIgnoring(blob, arena, types, host);
    if (reinterpret_cast<std::uintptr_t>(arena.data()) % kArenaAlign != 0) {
        fail(error, "arena") << "the arena must be aligned to " << kArenaAlign << " bytes";
        return nullptr;
    }
    Blob b;
    if (!check(blob, types, b, error)) return nullptr;
    // One pass lays the plan out and sizes it: each node's query_memory is asked once, so what is placed is what was
    // measured. A plan past the arena is refused here, having written only into the arena, and created nothing.
    Bump place{arena.data(), arena.size()};
    Placed p;
    PlanSize size;
    if (!layout(b, types, place, p, arena.size(), 0, &size, error)) return nullptr;
    LoadedPlan& plan = *new (p.plan) LoadedPlan{};
    plan.sampleRate = b.sampleRate();
    plan.maxFrames = b.h.maxFrames;
    plan.nodes = std::span<LoadedNode>(p.nodes, b.count(Section::Nodes));
    plan.bytes = size.bytes;
    plan.paramViews = p.paramViews;
    plan.wired = p.wired;
    plan.packages = std::span<LoadedPackage>(p.packages, p.packageCount);
    plan.outer = host;
    plan.packageService = rp_host_packages{&plan, &packageMemory};
    plan.host = rp_host{RP_ABI_VERSION,
                        &plan,
                        host && host->alloc ? &loaderAlloc : nullptr,
                        host && host->free ? &loaderFree : nullptr,
                        &loaderLog,
                        &loaderInterface};
    bind(b, p);
    plan.view = PlanView{std::span<PlanStep>(p.steps, b.count(Section::Steps)),
                         std::span<const OutputQueue>(p.clears, b.count(Section::Clears)),
                         std::span<rp_control_queue* const>(p.rolls, b.count(Section::Rolls)),
                         std::span<const FeedbackCopy>(p.feedback, b.count(Section::Feedback)), &plan.faults,
                         &plan.drops};
    if (!runUp(b, p, &plan.host, error)) return nullptr;
    return &plan;
}

void unloadPlan(LoadedPlan* plan) {
    if (!plan) return;
    unwind(plan->nodes.data(), static_cast<std::uint32_t>(plan->nodes.size()), plan->packages.data(),
           static_cast<std::uint32_t>(plan->packages.size()));
}

}  // namespace rp::graph
