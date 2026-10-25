// The no-heap claim: once a plan is built, the block path - draining commands, adopting a plan, running
// every core node, feedback copies, a coupled group and the host output - performs no allocation and frees
// nothing. Global operator new and delete are replaced for this binary; while `forbid` is set on a thread,
// any allocation or free on that thread is counted.
#include <catch2/catch_test_macros.hpp>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // these replacements pair aligned_alloc with free on purpose
#endif

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <string>
#if defined(_WIN32)
#include <malloc.h>
#endif

#include "GraphRig.hpp"

namespace {
thread_local bool forbid = false;
std::atomic<int>  violations{0};  // allocations while forbidden
std::atomic<int>  frees{0};       // frees while forbidden

// Every block is allocated over-aligned through one pair of calls, so any delete form can free any new
// form - the platform's aligned allocator does not mix with plain free on Windows.
void* rawAlloc(std::size_t n, std::size_t align) {
    if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
    if (n > SIZE_MAX - align) return nullptr;  // rounding up would wrap
    const std::size_t size = (n ? n : 1) + align - 1;
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    return std::aligned_alloc(align, size / align * align);
#endif
}

void rawFree(void* p) noexcept {
    if (forbid && p) frees.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* allocate(std::size_t n, std::size_t align) {
    if (forbid) violations.fetch_add(1, std::memory_order_relaxed);
    void* p = rawAlloc(n, align);
    if (!p) throw std::bad_alloc();
    return p;
}

void* allocateOrNull(std::size_t n, std::size_t align) noexcept {
    if (forbid) violations.fetch_add(1, std::memory_order_relaxed);
    return rawAlloc(n, align);
}
}  // namespace

// Every form is replaced, nothrow ones included: a form left to the runtime allocates through it (the
// planner's aligned nothrow new, under ASan), and the replaced delete then frees what it never allocated.
void* operator new(std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocateOrNull(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocateOrNull(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return allocateOrNull(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return allocateOrNull(n, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { rawFree(p); }
void operator delete[](void* p) noexcept { rawFree(p); }
void operator delete(void* p, std::size_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::size_t) noexcept { rawFree(p); }
void operator delete(void* p, std::align_val_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { rawFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { rawFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { rawFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { rawFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawFree(p); }

using rp::graph::test::GraphRig;

namespace {

// A coupled member that allocates nothing (the shared rp.test.linked logs to a vector).
struct QuietLinked {
    static constexpr const char* id = "rp.test.quiet-linked";
    struct { rp::link link; } inputs;
    struct { rp::audio_out<rp::mono> out; } outputs;
    std::uint32_t produced = 0, peerCount = 0;
    void set_peers(std::span<rp_node* const> peers) noexcept { peerCount = static_cast<std::uint32_t>(peers.size()); }
    void prepare(const rp::Block&) noexcept { produced = 0; }
    bool step(std::uint32_t target) noexcept {
        if (produced < target) ++produced;
        return produced >= target;
    }
    void finish(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = static_cast<float>(peerCount);
    }
    void process(const rp::Block&) noexcept {}
};

const rp_package* noAllocPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<QuietLinked>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.noalloc"})", 1, kTypes};
    return &kPackage;
}

// Every rp.core node, a feedback wire of each kind, a coupled pair, a reshaping node and the host output. The
// `swapped` form changes a setting in place (a live swap), a param value, a coupled group's members, and
// connects an output that was not (so ports_changed fires), reshapes a node (a param it adds gets a document
// value), and adds fresh instances and a feedback wire.
std::string graphDoc(bool swapped, rp::graph::ResourceTable::Id buf, rp::graph::ResourceTable::Id rec) {
    const std::string b = std::to_string(buf), r = std::to_string(rec);
    std::string nodes = R"({"id":"src","type":"rp.test.source"},
        {"id":"p","type":"rp.core.buffer-player",)" + std::string(swapped ? R"("settings":{"loop":false},)" : "") +
        R"("resources":{"sample":"res:)" + b + R"("}},
        {"id":"d","type":"rp.core.delay","settings":{"maxSamples":256},"params":{"time":)" + (swapped ? "40" : "32") + R"(}},
        {"id":"k","type":"rp.core.constant","params":{"value":0.5}},
        {"id":"k2","type":"rp.core.constant","params":{"value":0.25}},
        {"id":"addc","type":"rp.core.add","settings":{"kind":"control"}},
        {"id":"cs","type":"rp.test.sink"},
        {"id":"sum","type":"rp.core.add"},
        {"id":"split","type":"rp.core.split"},
        {"id":"dm","type":"rp.core.downmix"},
        {"id":"join","type":"rp.core.join"},
        {"id":"rl","type":"rp.core.relabel","settings":{"channels":2,"layout":"stereo"}},
        {"id":"mg","type":"rp.core.merge"},
        {"id":"es","type":"rp.test.sink"},
        {"id":"x","type":"rp.test.through"},{"id":"y","type":"rp.test.through"},
        {"id":"rec","type":"rp.core.buffer-recorder","resources":{"target":"res:)" + r + R"("}},
        {"id":"la","type":"rp.test.quiet-linked"},{"id":"lb","type":"rp.test.quiet-linked"},{"id":"ls","type":"rp.test.sink"},
        {"id":"sh","type":"rp.test.shape","settings":)" +
        (swapped ? R"({"ins":["p","q"],"outs":["a","b"],"levels":["K","L"],"knobs":["g","h"],"tag":"b"},)"
                   R"("params":{"g":5})"
                 : R"({"ins":["q"],"outs":["b"],"levels":["L"],"knobs":["h"]})") + R"(},
        {"id":"shs","type":"rp.test.sink"},{"id":"shl","type":"rp.test.sink"},
        {"id":"out","type":"rp.host.audio-out"})";
    std::string wires = R"({"from":"p.out","to":"d.in"},{"from":"p.out","to":"sum.in1"},{"from":"d.out","to":"sum.in2"},
        {"from":"sum.out","to":"split.in"},{"from":"sum.out","to":"dm.in"},
        {"from":"split.out1","to":"join.in1"},{"from":"dm.out","to":"join.in2"},
        {"from":"join.out","to":"out.out.1-2"},{"from":"sum.out","to":"rec.in"},
        {"from":"sum.out","to":"rl.in"},{"from":"rl.out","to":"out.out.3-4"},
        {"from":"k.out","to":"addc.in1"},{"from":"k2.out","to":"addc.in2"},{"from":"addc.out","to":"cs.control","feedback":true},
        {"from":"src.events","to":"mg.in1"},{"from":"mg.out","to":"es.events"},
        {"from":"x.out","to":"y.in"},{"from":"y.out","to":"out.out.5-6"},{"from":"y.out","to":"x.in","feedback":true},
        {"from":"la.link","to":"lb.link"},{"from":"la.out","to":"ls.in"},
        {"from":"src.events","to":"sh.q"},{"from":"sh.b","to":"shs.events"},{"from":"sh.L","to":"shl.control"})";
    if (swapped) {
        nodes += R"(,{"id":"rl2","type":"rp.core.relabel","settings":{"channels":2,"layout":"stereo"}},{"id":"s2","type":"rp.test.sink"},
            {"id":"p2","type":"rp.core.buffer-player"},{"id":"s3","type":"rp.test.sink"},{"id":"fbs","type":"rp.test.sink"},
            {"id":"lc","type":"rp.test.quiet-linked"})";
        wires += R"(,{"from":"sum.out","to":"rl2.in"},{"from":"rl2.out","to":"s2.in"},
            {"from":"rec.buffer","to":"p2.sample"},{"from":"p2.out","to":"s3.in"},
            {"from":"src.events","to":"fbs.events","feedback":true},{"from":"lb.link","to":"lc.link"})";
    }
    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
}

std::atomic<int> hostCommandsSeen{0};

// Allocates and frees nothing, as a handler running on the audio thread must not.
void countCommand(void*, const rp::graph::HostCommand&, const void*, bool) {
    hostCommandsSeen.fetch_add(1, std::memory_order_relaxed);
}

struct Measured {
    int allocations = 0, freed = 0;
};

Measured measure(GraphRig& rig, int blocks) {
    violations = 0;
    frees = 0;
    forbid = true;
    rig.run(64, blocks);
    forbid = false;
    return {violations.load(), frees.load()};
}

}  // namespace

TEST_CASE("the block path never allocates or frees", "[graph][noalloc]") {
    GraphRig rig;
    REQUIRE(rig.registry.addPackage(noAllocPackage()));
    const auto buf = rig.resources.createAudioBuffer(2, 512, 48000.0);
    const auto rec = rig.resources.createAudioBuffer(2, 512, 48000.0);
    const auto first = rig.set(graphDoc(false, buf, rec));
    INFO(GraphRig::codes(first));
    REQUIRE(first.ok);
    // No warm-up: a node that allocates lazily on its first block is caught too.
    auto m = measure(rig, 2);
    CHECK(m.allocations == 0);
    CHECK(m.freed == 0);

    // A swap is queued (the audio thread "owns" the drain), so the measured blocks also adopt a plan.
    rig.host->setAudioThreadOwns(true);
    const auto second = rig.set(graphDoc(true, buf, rec));
    INFO(GraphRig::codes(second));
    REQUIRE(second.ok);
    REQUIRE(rig.host->setParam("k", "value", 0.75, 5));
    m = measure(rig, 8);
    CHECK(m.allocations == 0);
    CHECK(m.freed == 0);
    rig.host->setAudioThreadOwns(false);
    CHECK(rp::peer_as<QuietLinked>(rig.host->node("la"))->peerCount == 2);  // the peer change was adopted
    CHECK(rp::peer_as<rp::graph::test::Shape>(rig.host->node("sh"))->ranOutputs == 4);  // and the reshape

    // A host command, and two commit-free plans queued behind it, all applied inside the measured blocks.
    // The same document again changes nothing, so neither plan commits settings and neither waits.
    rig.host->setHostCommandHandler(&countCommand, nullptr);
    rig.host->setAudioThreadOwns(true);
    const std::uint8_t payload = 1;
    REQUIRE(rig.host->pushHostCommand(&payload, sizeof payload));
    REQUIRE(rig.host->setGraph(*rp::graph::parseGraphDocument(graphDoc(true, buf, rec)), std::make_shared<const int>(1)).ok);
    REQUIRE(rig.host->setGraph(*rp::graph::parseGraphDocument(graphDoc(true, buf, rec)), std::make_shared<const int>(2)).ok);
    REQUIRE(rig.host->pushHostCommand(&payload, sizeof payload));
    m = measure(rig, 4);
    CHECK(m.allocations == 0);
    CHECK(m.freed == 0);
    CHECK(hostCommandsSeen.load() == 2);
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 2);
    rig.host->setAudioThreadOwns(false);
}
