// The no-heap claim: once a plan is built, the block path - draining commands, adopting a plan, running
// every core node and the host output - performs no allocation. Global operator new is replaced for this
// binary; while `forbid` is set on a thread, any allocation on that thread is counted as a violation.
#include <catch2/catch_test_macros.hpp>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // these replacements pair aligned_alloc with free on purpose
#endif

#include <atomic>
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

#include "GraphRig.hpp"

namespace {
thread_local bool    forbid = false;
std::atomic<int>     violations{0};

// Every block is allocated over-aligned through one pair of calls, so any delete form can free any new
// form - the platform's aligned allocator does not mix with plain free on Windows.
void* rawAlloc(std::size_t n, std::size_t align) {
    if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
    const std::size_t size = (n ? n : 1) + align - 1;
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    return std::aligned_alloc(align, size / align * align);
#endif
}

void rawFree(void* p) noexcept {
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
}  // namespace

void* operator new(std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return allocate(n, alignof(std::max_align_t));
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p) noexcept { rawFree(p); }
void operator delete[](void* p) noexcept { rawFree(p); }
void operator delete(void* p, std::size_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::size_t) noexcept { rawFree(p); }
void operator delete(void* p, std::align_val_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { rawFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { rawFree(p); }

using rp::graph::test::GraphRig;

TEST_CASE("the block path never allocates", "[graph][noalloc]") {
    GraphRig rig;
    const auto buf = rig.resources.createAudioBuffer(2, 512, 48000.0);
    const auto rec = rig.resources.createAudioBuffer(2, 512, 48000.0);
    const std::string doc = R"({"nodes":[
        {"id":"p","type":"rp.core.buffer-player","resources":{"sample":"res:)" + std::to_string(buf) + R"("}},
        {"id":"d","type":"rp.core.delay","settings":{"maxSamples":256},"params":{"time":32}},
        {"id":"k","type":"rp.core.constant","params":{"value":0.5}},
        {"id":"sum","type":"rp.core.add"},
        {"id":"split","type":"rp.core.split"},
        {"id":"dm","type":"rp.core.downmix"},
        {"id":"join","type":"rp.core.join"},
        {"id":"rec","type":"rp.core.buffer-recorder","resources":{"target":"res:)" + std::to_string(rec) + R"("}},
        {"id":"out","type":"rp.host.audio-out"}],
      "wires":[{"from":"p.out","to":"d.in"},{"from":"p.out","to":"sum.in1"},{"from":"d.out","to":"sum.in2"},
               {"from":"sum.out","to":"split.in"},{"from":"sum.out","to":"dm.in"},
               {"from":"split.out1","to":"join.in1"},{"from":"dm.out","to":"join.in2"},
               {"from":"join.out","to":"out.out.1-2"},{"from":"sum.out","to":"rec.in"}]})";
    REQUIRE(rig.set(doc).ok);
    rig.run(64, 2);  // warm up outside the check

    // A swap is queued (audio thread "owns" the drain), so the measured blocks also adopt a plan.
    rig.host->setAudioThreadOwns(true);
    std::string swapped = doc;
    swapped.replace(swapped.find(R"("type":"rp.core.buffer-player",)"), 31, R"("type":"rp.core.buffer-player","settings":{"loop":false},)");
    REQUIRE(rig.set(swapped).ok);  // a live settings swap: adopted by the measured blocks
    REQUIRE(rig.host->setParam("k", "value", 0.25, 5));

    violations = 0;
    forbid = true;
    rig.run(64, 8);
    forbid = false;
    CHECK(violations.load() == 0);
    rig.host->setAudioThreadOwns(false);
}
