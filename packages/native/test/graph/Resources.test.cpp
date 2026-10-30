// A node that reads a resource its SETTINGS name (docs/modular-graph-phase-c3-design.md section 4, and section 2.5 of
// the main design for the lifetime rule): the authoring layer pins it at create through rp.host.resources/1 and drops
// the pin when the instance is destroyed, a ref that moves rebuilds the node, and a document naming a missing, dropped
// or wrong-typed resource is refused with the field's name in the reason. Driven on a quiescent GraphHost except where
// a case keeps the audio side owning the drain, which is how a pin is watched across a reclaim.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "GraphRig.hpp"
#include "graph/node.hpp"

using namespace rp::graph;
using namespace rp::graph::test;

namespace {

// The toy: its one resource field is a rom, `model` stands in for a construct-time field that only a rebuild changes,
// and `level` is a knob a commit applies to the instance it has, so a case can see the instance's pin reach the
// settings object a commit installs.
struct ResourceNodeSettings {
    rp::resource_ref<"rom"> rom;
    std::uint32_t           model = 0;  // the rebuild key
    double                  level = 1.0;
};

struct ResourceNode {
    static constexpr const char* id = "rp.test.resource";
    using Settings = ResourceNodeSettings;
    struct { rp::audio_out<rp::mono> out; } outputs;

    // A keyed type whose prepare is stateless, as a system node is: a new `model` rebuilds, while `level` alone does
    // not, and the prepare reads only what create fixed (the key and the resource pins).
    static std::uint32_t rebuild_key(const Settings& s) { return s.model; }
    static constexpr bool stateless_prepare = true;

    const Settings* settings = nullptr;
    bool            sawBytes = false;  // the settings object on_settings last saw carried a handle
    std::uint32_t   bytes = 0;         // and the payload was this many bytes
    std::string     ref;               // the ref it named
    float           level = 0.0f;
    std::uint32_t   blocks = 0;        // state a rebuild loses

    void on_settings(const Settings& s) noexcept {
        settings = &s;
        const rp_bytes* b = s.rom.bytes();
        sawBytes = b != nullptr;
        bytes = b ? static_cast<std::uint32_t>(b->size) : 0;
        ref = s.rom.ref;
        level = static_cast<float>(s.level);
    }
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = level;
        ++blocks;
    }
};

// Two resource fields, so a case can watch a pin that names none keep its place in the settings' field order.
struct PairNodeSettings {
    rp::resource_ref<"rom">  rom;
    rp::resource_ref<"sram"> seed;  // absent in most documents
    double                   level = 1.0;
};

struct PairNode {
    static constexpr const char* id = "rp.test.resources";
    using Settings = PairNodeSettings;
    struct { rp::audio_out<rp::mono> out; } outputs;

    std::uint32_t romBytes = 0;
    std::uint32_t seedBytes = 0;  // 0 when the document names none
    bool          seedData = false;  // the seed's rp_bytes carries a data pointer (non-null even when empty)
    std::uint32_t blocks = 0;     // state a rebuild loses
    void on_settings(const Settings& s) noexcept {
        const rp_bytes* r = s.rom.bytes();
        const rp_bytes* b = s.seed.bytes();
        romBytes = r ? static_cast<std::uint32_t>(r->size) : 0;
        seedBytes = b ? static_cast<std::uint32_t>(b->size) : 0;
        seedData = b && b->data != nullptr;
    }
    void process(const rp::Block&) noexcept { ++blocks; }
};

// A node whose rebuild is decided by needs_rebuild, which reads a resource handle: both settings objects it compares
// carry this instance's pin, and the hook is skipped when a ref moved (that is a rebuild already), so it never sees a
// null handle. `calls` and `nullTo` are what the case watches to see both halves.
struct NeedsNodeSettings {
    rp::resource_ref<"rom"> rom;
    double                  level = 1.0;
};

struct NeedsNode {
    static constexpr const char* id = "rp.test.needs";
    using Settings = NeedsNodeSettings;
    struct { rp::audio_out<rp::mono> out; } outputs;

    static inline int calls = 0;
    static inline int nullTo = 0;
    static bool needs_rebuild(const Settings& from, const Settings& to) {
        ++calls;
        if (!to.rom.bytes()) ++nullTo;
        return !from.rom.bytes() || !to.rom.bytes() || from.rom.bytes()->size != to.rom.bytes()->size;
    }

    std::uint32_t blocks = 0;  // state a rebuild loses
    void process(const rp::Block&) noexcept { ++blocks; }
};

const rp_package* resourcePackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<ResourceNode>(), rp::node_type<PairNode>(),
                                                 rp::node_type<NeedsNode>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.resource","version":"1.0.0"})", 3, kTypes};
    return &kPackage;
}

// One node, `r`, whose settings name `ref` (a rom); a document of just it, and one that also carries a node whose rom
// names no live resource, which fails its CREATE after `r` has pinned.
std::string doc(const std::string& ref, double level = 1.0, std::uint32_t model = 0) {
    return R"({"nodes":[{"id":"r","type":"rp.test.resource","settings":{"rom":")" + ref + R"(","level":)" +
           std::to_string(level) + R"(,"model":)" + std::to_string(model) + R"(}}],"wires":[]})";
}
std::string docWithRefusedNode(const std::string& ref) {
    return R"({"nodes":[{"id":"r","type":"rp.test.resource","settings":{"rom":")" + ref +
           R"("}},{"id":"bad","type":"rp.test.resource","settings":{"rom":"res:999"}}],"wires":[]})";
}
std::string resRef(ResourceTable::Id id) { return "res:" + std::to_string(id); }

std::string needsDoc(const std::string& ref, double level = 1.0) {
    return R"({"nodes":[{"id":"r","type":"rp.test.needs","settings":{"rom":")" + ref + R"(","level":)" +
           std::to_string(level) + R"(}}],"wires":[]})";
}

std::string pairDoc(const std::string& rom, const std::string& seed, double level = 1.0) {
    std::string settings = R"({"rom":")" + rom + R"(")";
    if (!seed.empty()) settings += R"(,"seed":")" + seed + R"(")";
    settings += R"(,"level":)" + std::to_string(level) + "}";
    return R"({"nodes":[{"id":"r","type":"rp.test.resources","settings":)" + settings + R"(}],"wires":[]})";
}

struct Rig : GraphRig {
    Rig() : GraphRig(HostConfig{48000.0, 64, Target::Desktop}, 8, resourcePackage()) {}
    ResourceNode& toy() { return node<ResourceNode>("r"); }
    PairNode&     pair() { return node<PairNode>("r"); }
    NeedsNode&    needs() { return node<NeedsNode>("r"); }
};

}  // namespace

TEST_CASE("a node pins the resource its settings name, and the pin goes when the instance does", "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {0x52, 0x50, 0x4f, 0x4d});
    REQUIRE(rig.set(doc(resRef(rom))).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.toy().sawBytes);
    CHECK(rig.toy().bytes == 4);
    CHECK(rig.toy().ref == resRef(rom));

    rig.run();
    CHECK(rig.toy().blocks == 1);
    CHECK(rig.resources.pinCount(rom) == 1);  // a block neither pins nor drops

    // The document drops the node: quiescent, its plan is reclaimed in the same call, and the pin goes with it.
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    CHECK(rig.resources.pinCount(rom) == 0);
    CHECK(rig.resources.liveCount() == 1);  // the owner still holds it
}

TEST_CASE("a settings change that keeps the resource keeps the pin, and the new settings object carries the handle",
          "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {1, 2, 3});
    REQUIRE(rig.set(doc(resRef(rom))).ok);
    rig.run();
    rig.run();
    CHECK(rig.toy().blocks == 2);

    // `level` moves, the ref does not: the instance is kept (a swap), and the settings object prepare parsed carries
    // this instance's pin, so the node reads its bytes from the committed settings too.
    REQUIRE(rig.set(doc(resRef(rom), 0.5)).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    rig.run();
    CHECK(rig.toy().blocks == 3);  // the same instance
    CHECK(rig.toy().level == 0.5f);
    CHECK(rig.toy().sawBytes);
    CHECK(rig.toy().bytes == 3);
}

TEST_CASE("a resource a node's settings name must be a live resource of the field's type", "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {1, 2, 3});
    const auto sram = rig.resources.createBytes("sram", {4, 5});
    const auto gone = rig.resources.createBytes("rom", {6});
    REQUIRE(rig.set(doc(resRef(rom))).ok);
    REQUIRE(rig.resources.drop(gone));

    const auto refused = [&](const std::string& document) {
        const SetGraphResult r = rig.set(document);
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "create-failed");
        return r.errors[0].message;
    };
    const std::string unknown = refused(doc(resRef(999)));
    CHECK(unknown.find("rom:") != std::string::npos);
    CHECK(unknown.find("not a live resource") != std::string::npos);
    const std::string dropped = refused(doc(resRef(gone)));
    CHECK(dropped.find("not a live resource") != std::string::npos);
    const std::string wrongType = refused(doc(resRef(sram)));
    CHECK(wrongType.find("sram resource, not a rom") != std::string::npos);
    const std::string malformed = refused(doc("res:007"));
    CHECK(malformed.find("is not a resource reference") != std::string::npos);

    // Every one of them left the running plan as it was: the node still runs, its own pin untouched.
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.host->node("r") != nullptr);
    rig.run();
    CHECK(rig.toy().blocks == 1);
    CHECK(rig.toy().bytes == 3);
}

TEST_CASE("a create that fails on a later resource field drops the fields it already pinned", "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {1, 2, 3});
    const auto gone = rig.resources.createBytes("sram", {4});
    REQUIRE(rig.resources.drop(gone));  // its owner is gone, and nothing pins it
    CHECK(rig.resources.liveCount() == 1);

    // The pair node pins `rom` first and is refused on its second field. Reaching that second field at all is what
    // says the first was acquired: a create that could not pin `rom` would have stopped there, naming `rom`.
    for (const std::string& seed : {resRef(gone), resRef(999)}) {
        const SetGraphResult refused = rig.set(pairDoc(resRef(rom), seed));
        REQUIRE_FALSE(refused.ok);
        REQUIRE(refused.errors.size() == 1);
        CHECK(refused.errors[0].code == "create-failed");
        CHECK(refused.errors[0].message.find("seed:") != std::string::npos);
        CHECK(rig.resources.pinCount(rom) == 0);  // `rom`'s pin went with the instance that took it
    }

    // A leaked first pin would keep `rom` resident after its owner lets go.
    CHECK(rig.resources.drop(rom));
    CHECK(rig.resources.liveCount() == 0);
}

TEST_CASE("a change of the named resource rebuilds the node, and the old pin drops at reclaim", "[graph][resource]") {
    Rig rig;
    const auto first = rig.resources.createBytes("rom", {1, 2, 3});
    const auto second = rig.resources.createBytes("rom", {1, 2, 3, 4, 5});
    REQUIRE(rig.set(doc(resRef(first))).ok);
    rig.run();
    rig.run();
    REQUIRE(rig.toy().blocks == 2);

    // The audio side owns the drain, so the plan that ran the first instance is not reclaimed until it is told to.
    rig.host->setAudioThreadOwns(true);
    REQUIRE(rig.set(doc(resRef(second))).ok);
    CHECK(rig.resources.pinCount(first) == 1);   // the plan still running it pins it
    CHECK(rig.resources.pinCount(second) == 1);  // and the fresh instance pinned what it names
    rig.host->setAudioThreadOwns(false);         // flush: adopt, then reclaim what the swap replaced
    CHECK(rig.resources.pinCount(first) == 0);
    CHECK(rig.resources.pinCount(second) == 1);

    rig.run();
    CHECK(rig.toy().blocks == 1);  // a fresh instance, its state not carried over
    CHECK(rig.toy().bytes == 5);
    CHECK(rig.toy().ref == resRef(second));
}

TEST_CASE("a keyed stateless node rebuilds on its key, swaps a knob, and rebuilds on a moved ref",
          "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {1, 2, 3});
    const auto other = rig.resources.createBytes("rom", {4, 5});
    REQUIRE(rig.set(doc(resRef(rom), 1.0, 0)).ok);
    rig.run();
    rig.run();
    REQUIRE(rig.toy().blocks == 2);

    // `level` alone: the instance is kept, so its block count continues and its pin never moves.
    REQUIRE(rig.set(doc(resRef(rom), 0.5, 0)).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.toy().blocks == 2);
    rig.run();
    CHECK(rig.toy().blocks == 3);
    CHECK(rig.toy().level == 0.5f);
    CHECK(rig.toy().sawBytes);

    // The key moves: a fresh instance, whose state starts over, pinning the same resource.
    REQUIRE(rig.set(doc(resRef(rom), 0.5, 1)).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.toy().blocks == 0);
    rig.run();
    CHECK(rig.toy().blocks == 1);

    // The ref moves with the key unchanged: a rebuild all the same, because node.hpp decides it, not the key.
    REQUIRE(rig.set(doc(resRef(other), 0.5, 1)).ok);
    CHECK(rig.resources.pinCount(other) == 1);
    CHECK(rig.resources.pinCount(rom) == 0);
    CHECK(rig.toy().blocks == 0);
    rig.run();
    CHECK(rig.toy().blocks == 1);
    CHECK(rig.toy().ref == resRef(other));
    CHECK(rig.toy().bytes == 2);
}

TEST_CASE("a refused plan unwinds the pins its new instances took", "[graph][resource]") {
    Rig rig;
    const auto held = rig.resources.createBytes("rom", {1});
    const auto fresh = rig.resources.createBytes("rom", {2, 2});
    // A plan running `r` on `fresh` first proves a create pins what it names; switching to `held` drops that pin, so
    // the refusal below starts with no pin on `fresh` but its owner still holding it.
    REQUIRE(rig.set(doc(resRef(fresh))).ok);
    CHECK(rig.resources.pinCount(fresh) == 1);
    REQUIRE(rig.set(doc(resRef(held))).ok);
    CHECK(rig.resources.pinCount(fresh) == 0);

    const SetGraphResult refused = rig.set(docWithRefusedNode(resRef(fresh)));
    REQUIRE_FALSE(refused.ok);
    // One error, and it names `bad`: `r`'s create succeeded, and only its success puts the pin on `fresh` there.
    REQUIRE(refused.errors.size() == 1);
    CHECK(refused.errors[0].code == "create-failed");
    CHECK(refused.errors[0].nodeId == "bad");
    CHECK(rig.resources.pinCount(fresh) == 0);  // dropped as the refused plan's instances unwound
    CHECK(rig.resources.pinCount(held) == 1);   // the running plan's untouched
    CHECK(rig.host->node("r") != nullptr);
    rig.run();
    CHECK(rig.toy().blocks == 1);
    CHECK(rig.toy().ref == resRef(held));

    // Only pins keep an entry alive after its owner lets go, so a leaked pin would leave it here.
    CHECK(rig.resources.drop(fresh));
    CHECK(rig.resources.liveCount() == 1);  // `held` alone
}

TEST_CASE("a resource the owner drops while a plan pins it stays readable, and is freed after that plan is",
          "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {7, 7});
    REQUIRE(rig.set(doc(resRef(rom))).ok);
    CHECK(rig.resources.drop(rom));        // the owner lets go; the node's pin keeps it alive
    CHECK_FALSE(rig.resources.drop(rom));  // and a duplicate drop cannot take the pin with it
    CHECK(rig.resources.liveCount() == 1);

    rig.run();
    CHECK(rig.toy().sawBytes);
    CHECK(rig.toy().bytes == 2);

    // The instance holding the pin plans on, so a later document naming the same resource does not depend on when the
    // last plan happened to be reclaimed.
    REQUIRE(rig.set(doc(resRef(rom), 2.0)).ok);
    rig.run();
    CHECK(rig.toy().bytes == 2);
    CHECK(rig.toy().level == 2.0f);

    // The plan that reads it goes, and with the last pin the resource is freed. A document naming it now refuses at
    // create, where a plan after the reclaim would.
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    CHECK(rig.resources.pinCount(rom) == 0);
    CHECK(rig.resources.liveCount() == 0);
    const SetGraphResult refused = rig.set(doc(resRef(rom)));
    REQUIRE_FALSE(refused.ok);
    CHECK(GraphRig::codes(refused) == "create-failed");
    CHECK(refused.errors[0].message.find("not a live resource") != std::string::npos);
}

TEST_CASE("a node with a resource field that names none keeps its other fields' pins in place", "[graph][resource]") {
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {1, 2, 3});
    const auto seed = rig.resources.createBytes("sram", {9, 9});
    REQUIRE(rig.set(pairDoc(resRef(rom), "")).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.resources.pinCount(seed) == 0);
    CHECK(rig.pair().romBytes == 3);
    CHECK(rig.pair().seedBytes == 0);
    rig.run();
    rig.run();
    REQUIRE(rig.pair().blocks == 2);

    // A knob alone while the seed is ABSENT keeps the instance: the placeholder hold keeps the pins aligned with the
    // fields, so a rebuild does not start the node over every time an optional seed is not named.
    REQUIRE(rig.set(pairDoc(resRef(rom), "", 0.5)).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.pair().blocks == 2);
    rig.run();
    CHECK(rig.pair().blocks == 3);

    // Naming the second resource moves the refs, so it is a rebuild: the fresh instance pins both.
    REQUIRE(rig.set(pairDoc(resRef(rom), resRef(seed))).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.resources.pinCount(seed) == 1);
    CHECK(rig.pair().romBytes == 3);
    CHECK(rig.pair().seedBytes == 2);
    CHECK(rig.pair().blocks == 0);
    rig.run();
    rig.run();
    REQUIRE(rig.pair().blocks == 2);

    // A setting that is not a resource keeps the instance, and both handles are carried into the new settings object.
    REQUIRE(rig.set(pairDoc(resRef(rom), resRef(seed), 0.25)).ok);
    CHECK(rig.resources.pinCount(rom) == 1);
    CHECK(rig.resources.pinCount(seed) == 1);
    CHECK(rig.pair().seedBytes == 2);
    CHECK(rig.pair().blocks == 2);
    rig.run();
    REQUIRE(rig.pair().blocks == 3);

    // Dropping the seed's ref is a rebuild too: the counter starts over, and the pin goes with the instance.
    REQUIRE(rig.set(pairDoc(resRef(rom), "")).ok);
    CHECK(rig.pair().blocks == 0);
    CHECK(rig.resources.pinCount(seed) == 0);
    CHECK(rig.pair().seedBytes == 0);
    CHECK(rig.pair().romBytes == 3);
}

TEST_CASE("an empty resource hands a node bytes of size 0 and a data pointer that is not null", "[graph][resource]") {
    // A blank battery is a legal seed (a "new SRAM" is one), and a consumer forms its vector from (data, data + size):
    // pointer arithmetic on a null data pointer is undefined even for size 0.
    Rig rig;
    const auto rom = rig.resources.createBytes("rom", {1, 2, 3});
    const auto blank = rig.resources.createBytes("sram", {});
    REQUIRE(rig.set(pairDoc(resRef(rom), resRef(blank))).ok);
    CHECK(rig.pair().romBytes == 3);
    CHECK(rig.pair().seedBytes == 0);
    CHECK(rig.pair().seedData);
    CHECK(rig.resources.pinCount(blank) == 1);
    rig.run();
    CHECK(rig.pair().blocks == 1);
}

TEST_CASE("needs_rebuild reads a resource through a resolved handle, and is skipped for a ref that moved",
          "[graph][resource]") {
    // The hook compares two settings objects; the instance's committed settings carry this instance's pin and the
    // prepared one is resolved before the hook runs. A moved ref is a rebuild node.hpp decides, so the hook is not
    // called then: it must never see a null handle on the object it is asked about.
    Rig rig;
    const auto first = rig.resources.createBytes("rom", {1, 2, 3});
    const auto second = rig.resources.createBytes("rom", {4, 5, 6, 7});
    REQUIRE(rig.set(needsDoc(resRef(first))).ok);
    rig.run();
    rig.run();
    REQUIRE(rig.needs().blocks == 2);

    // An unmoved ref: the hook runs once, with a handle on both sides, and the instance is kept.
    NeedsNode::calls = 0;
    NeedsNode::nullTo = 0;
    REQUIRE(rig.set(needsDoc(resRef(first), 0.5)).ok);
    CHECK(NeedsNode::calls == 1);
    CHECK(NeedsNode::nullTo == 0);
    CHECK(rig.resources.pinCount(first) == 1);
    rig.run();
    CHECK(rig.needs().blocks == 3);

    // A ref that moved: no hook call, and a fresh instance that pinned what it names.
    NeedsNode::calls = 0;
    NeedsNode::nullTo = 0;
    REQUIRE(rig.set(needsDoc(resRef(second), 0.5)).ok);
    CHECK(NeedsNode::calls == 0);
    CHECK(NeedsNode::nullTo == 0);
    CHECK(rig.resources.pinCount(second) == 1);
    CHECK(rig.resources.pinCount(first) == 0);
    CHECK(rig.needs().blocks == 0);
    rig.run();
    CHECK(rig.needs().blocks == 1);
}
