// rp.core.midi-filter (docs/modular-graph-phase-e3-design.md section 6): drops the MIDI messages of the kinds its
// `drop` names and passes every other one as it came, frame, flags, bytes and order, through a real plan.
//
// - A stream of every kind, a SysEx, a lone F7 and real-time messages among the channel messages, frames tied and
//   flags set: each kind dropped alone leaves exactly the rest, an empty list (and no settings at all) passes it whole,
//   and BlipToaster's list, the box plan's (both aftertouches), drops those two alone.
// - A kind is the first byte's: a message led by a data byte is of no kind and passes whatever is dropped (no running
//   status is kept across messages), a note-on with velocity 0 is a noteOn, and a message carrying two is its first's.
// - Its output is sized as whatever feeds its input (rp::events_out::matching<"in">), so a stream past the default
//   passes whole where a default-sized output drops; nothing is allocated once the plan runs; a kind named twice or
//   one that is no kind is refused as the settings.
// - Its descriptor is its golden (golden/rp.core.midi-filter.descriptor.json; RP_GRAPH_GOLDEN_UPDATE=1 records it).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "graph/CountingAllocator.hpp"
#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;

namespace {

struct Message {
    std::uint32_t                frame;
    std::uint32_t                flags;
    std::vector<std::uint8_t>    bytes;
    const char*                  kind;  // the filter's label for it, or "" for none
};

// Every kind once, and the edges: frames tied and in order, a flag on some, SysEx in both its forms, a message led by
// a data byte, a velocity-0 note-on, real-time bytes among channel messages, and a message holding two.
const std::vector<Message>& stream() {
    static const std::vector<Message> s = {
        {0, 0, {0x80, 60, 0}, "noteOff"},
        {0, 1, {0x91, 60, 100}, "noteOn"},
        {2, 0, {0xA2, 60, 10}, "polyAftertouch"},
        {2, 0, {0xF8}, "realtime"},
        {3, 0, {0xB3, 7, 100}, "controlChange"},
        {5, 1, {0xC4, 5}, "programChange"},
        {5, 0, {0xD5, 20}, "channelAftertouch"},
        {9, 0, {0xE6, 0, 64}, "pitchBend"},
        {9, 1, {0xF0, 0x7D, 0x50, 0x01, 0xF7}, "sysex"},
        {12, 0, {0xF2, 0x10, 0x00}, "systemCommon"},
        {12, 0, {0xF7}, "sysex"},
        {13, 0, {0x40, 50}, ""},
        {20, 0, {0x90, 61, 0}, "noteOn"},
        {20, 0, {0xFA}, "realtime"},
        {21, 0, {0xF6}, "systemCommon"},
        {30, 0, {0xA0, 61, 3, 0x90, 62, 100}, "polyAftertouch"},
        {31, 0, {0xFF}, "realtime"},
    };
    return s;
}

const std::vector<std::string> kKinds = {"noteOff",           "noteOn",     "polyAftertouch", "controlChange",
                                         "programChange",     "channelAftertouch", "pitchBend", "sysex",
                                         "systemCommon",      "realtime"};

// The stream, `repeat` times a block, on an events output whose capacity its settings declare (0: the default).
struct ScriptSettings {
    std::uint32_t repeat = 1;
    std::uint32_t messages = 0;
};

struct Script {
    static constexpr const char* id = "rp.test.midi-script";
    using Settings = ScriptSettings;
    struct { rp::events_out out; } outputs;
    static void adjust_layout(Layout& l, const Settings& s) {
        if (s.messages) l.ports[0].capacity.messages = s.messages;
    }
    std::uint32_t repeat = 1;
    void on_settings(const Settings& s) noexcept { repeat = s.repeat; }
    void process(const rp::Block&) noexcept {
        for (std::uint32_t r = 0; r < repeat; ++r)
            for (const Message& m : stream()) outputs.out.push(m.frame, m.bytes, m.flags);
    }
};

// What reached its input in the last block, and the queue it came on; reserved, so a block allocates nothing.
struct Seen {
    std::uint32_t                frame = 0;
    std::uint32_t                flags = 0;
    std::uint32_t                size = 0;
    std::array<std::uint8_t, 8>  bytes{};
};

struct Log {
    static constexpr const char* id = "rp.test.midi-log";
    struct { rp::events_in in; } inputs;
    std::vector<Seen> seen;
    std::uint32_t     count = 0;  // every message, kept or not
    std::uint32_t     capacity = 0, bytesCapacity = 0;
    Log() { seen.reserve(1024); }
    void process(const rp::Block&) noexcept {
        seen.clear();
        count = 0;
        capacity = inputs.in.queue->capacity;
        bytesCapacity = inputs.in.queue->bytes_capacity;
        for (const rp_message& m : inputs.in.messages()) {
            ++count;
            if (seen.size() == seen.capacity()) continue;
            Seen s;
            s.frame = m.frame;
            s.flags = m.flags;
            s.size = m.size;
            const auto data = inputs.in.data(m);
            for (std::size_t i = 0; i < data.size() && i < s.bytes.size(); ++i) s.bytes[i] = data[i];
            seen.push_back(s);
        }
    }
};

const rp_package* filterTestPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Script>(), rp::node_type<Log>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.midi-filter"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

struct FilterRig : GraphRig {
    FilterRig() { registry.addPackage(filterTestPackage()); }
};

// The script through a filter of these settings (none: the node carries no "settings") into the log.
std::string doc(const std::string& filter, const std::string& script = "{}") {
    const std::string settings = filter.empty() ? "" : R"(,"settings":)" + filter;
    return R"({"nodes":[{"id":"s","type":"rp.test.midi-script","settings":)" + script +
           R"(},{"id":"f","type":"rp.core.midi-filter")" + settings + R"(},{"id":"log","type":"rp.test.midi-log"}],)"
           R"("wires":[{"from":"s.out","to":"f.in"},{"from":"f.out","to":"log.in"}]})";
}

std::string dropList(std::initializer_list<std::string> kinds) {
    std::string out = R"({"drop":[)";
    bool first = true;
    for (const auto& k : kinds) {
        out += (first ? "\"" : ",\"") + k + "\"";
        first = false;
    }
    return out + "]}";
}

// One line a message: frame, flags, bytes.
std::string line(std::uint32_t frame, std::uint32_t flags, const std::uint8_t* bytes, std::size_t size) {
    std::ostringstream o;
    o << frame << " " << flags << ":";
    for (std::size_t i = 0; i < size; ++i) o << " " << std::hex << static_cast<int>(bytes[i]) << std::dec;
    return o.str() + "\n";
}
std::string expected(const std::vector<std::string>& dropped) {
    std::string out;
    for (const Message& m : stream()) {
        bool drop = false;
        for (const auto& k : dropped) drop = drop || k == m.kind;
        if (!drop) out += line(m.frame, m.flags, m.bytes.data(), m.bytes.size());
    }
    return out;
}
std::string got(const Log& log) {
    std::string out;
    for (const Seen& s : log.seen) out += line(s.frame, s.flags, s.bytes.data(), s.size);
    return out;
}

// The golden's text, its line ending dropped as DspNodes.test.cpp drops it.
std::string readFile(const std::string& path) {
    std::ifstream     in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("the MIDI filter drops each kind alone, and passes the rest as it came", "[graph][core][midi-filter]") {
    // Every kind the stream holds is one of the filter's, and each of the filter's is in it.
    for (const auto& k : kKinds) {
        bool held = false;
        for (const Message& m : stream()) held = held || k == m.kind;
        INFO(k);
        CHECK(held);
    }
    FilterRig rig;
    for (const auto& kind : kKinds) {
        INFO(kind);
        const SetGraphResult r = rig.set(doc(dropList({kind})));
        INFO(GraphRig::codes(r));
        REQUIRE(r.ok);
        rig.run();
        const Log& log = rig.node<Log>("log");
        CHECK(got(log) == expected({kind}));
        CHECK(log.count < stream().size());
        CHECK(rig.host->droppedMessages() == 0);
    }
}

TEST_CASE("an empty drop list, or none, passes every message, its frame, flags and order kept",
          "[graph][core][midi-filter]") {
    FilterRig rig;
    for (const std::string& settings : {std::string(), std::string("{}"), std::string(R"({"drop":[]})")}) {
        INFO(settings);
        REQUIRE(rig.set(doc(settings)).ok);
        rig.run(64, 2);
        const Log& log = rig.node<Log>("log");
        CHECK(log.count == stream().size());
        CHECK(got(log) == expected({}));
    }
}

TEST_CASE("the box plan's list drops both aftertouches from among channel, SysEx and real-time messages",
          "[graph][core][midi-filter]") {
    FilterRig rig;
    REQUIRE(rig.set(doc(dropList({"polyAftertouch", "channelAftertouch"}))).ok);
    rig.run();
    const Log& log = rig.node<Log>("log");
    CHECK(got(log) == expected({"polyAftertouch", "channelAftertouch"}));
    // What is left still holds its SysEx, its real-time bytes and its channel messages, in their places.
    CHECK(log.count == stream().size() - 3);

    // Everything dropped: what is left is the one message of no kind, a run led by a data byte.
    std::string all = R"({"drop":[)";
    for (std::size_t i = 0; i < kKinds.size(); ++i) all += (i ? ",\"" : "\"") + kKinds[i] + "\"";
    REQUIRE(rig.set(doc(all + "]}")).ok);
    rig.run();
    CHECK(got(rig.node<Log>("log")) == "13 0: 40 32\n");
}

TEST_CASE("a message's kind is its first byte's", "[graph][core][midi-filter]") {
    FilterRig rig;
    // A velocity-0 note-on is a noteOn: dropping note-offs leaves it, dropping note-ons takes it.
    REQUIRE(rig.set(doc(dropList({"noteOff"}))).ok);
    rig.run();
    CHECK(got(rig.node<Log>("log")).find("20 0: 90 3d 0\n") != std::string::npos);
    REQUIRE(rig.set(doc(dropList({"noteOn"}))).ok);
    rig.run();
    CHECK(got(rig.node<Log>("log")).find("20 0: 90 3d 0\n") == std::string::npos);
    // A message holding a poly aftertouch then a note-on is a polyAftertouch, whole; a lone F7 is a sysex, and the
    // data-led run passes even with every channel kind dropped.
    REQUIRE(rig.set(doc(dropList({"polyAftertouch", "sysex", "noteOff", "noteOn", "controlChange", "programChange",
                                  "channelAftertouch", "pitchBend"})))
                .ok);
    rig.run();
    CHECK(got(rig.node<Log>("log")) == expected({"polyAftertouch", "sysex", "noteOff", "noteOn", "controlChange",
                                                 "programChange", "channelAftertouch", "pitchBend"}));
    CHECK(got(rig.node<Log>("log")).find("30 0:") == std::string::npos);
    CHECK(got(rig.node<Log>("log")).find("12 0: f7") == std::string::npos);
    CHECK(got(rig.node<Log>("log")).find("13 0: 40 32\n") != std::string::npos);
}

TEST_CASE("the MIDI filter's output is sized as what feeds it, so a stream past the default passes whole",
          "[graph][core][midi-filter]") {
    FilterRig rig;
    // 40 copies of the stream, 680 messages and 1360 bytes, on a source that declares 1024 messages: the filter's
    // queue is the source's, and nothing is dropped anywhere.
    const std::uint32_t copies = 40;
    REQUIRE(rig.set(doc("{}", R"({"repeat":40,"messages":1024})")).ok);
    rig.run();
    const Log& log = rig.node<Log>("log");
    CHECK(log.capacity == 1024);
    CHECK(log.bytesCapacity == kDefaultQueueBytes);
    CHECK(log.count == copies * stream().size());
    CHECK(rig.host->droppedMessages() == 0);
    // Fed from a default-sized source, it is default-sized itself.
    REQUIRE(rig.set(doc("{}")).ok);
    rig.run();
    CHECK(rig.node<Log>("log").capacity == kDefaultQueueMessages);
    CHECK(rig.node<Log>("log").bytesCapacity == kDefaultQueueBytes);
    // Where an output of the default size would drop what passes 256, charged to that output's node.
    REQUIRE(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.midi-script","settings":{"repeat":40,"messages":1024}},
                                  {"id":"m","type":"rp.core.merge","settings":{"inputs":1}},
                                  {"id":"log","type":"rp.test.midi-log"}],
                         "wires":[{"from":"s.out","to":"m.in1"},{"from":"m.out","to":"log.in"}]})")
                .ok);
    rig.run();
    CHECK(rig.node<Log>("log").count == kDefaultQueueMessages);
    CHECK(rig.host->droppedMessages("m") == copies * stream().size() - kDefaultQueueMessages);
}

TEST_CASE("the MIDI filter allocates nothing once its plan runs", "[graph][core][midi-filter]") {
    FilterRig rig;
    REQUIRE(rig.set(doc(dropList({"polyAftertouch", "realtime"}), R"({"repeat":8})")).ok);
    // The script's stream is a function-local static, made before the window (this case may run first).
    const std::size_t messages = stream().size();
    // Counted from the plan's first block.
    rp::test::beginCountingAllocations();
    rig.run(64, 6);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
    CHECK(rig.node<Log>("log").count == 8 * (messages - 5));
}

TEST_CASE("the MIDI filter refuses a kind named twice, or one that is no kind, as its settings",
          "[graph][core][midi-filter]") {
    FilterRig rig;
    SetGraphResult r = rig.set(doc(dropList({"noteOn", "sysex", "noteOn"})));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].nodeId == "f");
    CHECK(r.errors[0].message.find("drop names noteOn twice") != std::string::npos);
    r = rig.set(doc(dropList({"aftertouch"})));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(rig.set(doc(R"({"drop":["noteOff","noteOn","polyAftertouch","controlChange","programChange",)"
                      R"("channelAftertouch","pitchBend","sysex","systemCommon","realtime"]})"))
              .ok);  // every kind, each once: the list's bound
}

TEST_CASE("rp.core.midi-filter's descriptor is its golden", "[graph][core][midi-filter]") {
    const rp_package* pkg = corePackage();
    const rp_node_type* type = nullptr;
    for (std::uint32_t i = 0; i < pkg->node_type_count; ++i)
        if (std::string_view(pkg->node_types[i]->id) == "rp.core.midi-filter") type = pkg->node_types[i];
    REQUIRE(type);
    const std::string json = type->descriptor(type);
    const std::string path = std::string(RP_GRAPH_GOLDEN_DIR) + "/rp.core.midi-filter.descriptor.json";
    if (std::getenv("RP_GRAPH_GOLDEN_UPDATE")) std::ofstream(path) << json << "\n";
    CHECK(json == readFile(path));
    const auto parsed = parseDescriptor(json);
    REQUIRE(parsed);  // the strict parser takes it unchanged
    REQUIRE(parsed->layout.ports.size() == 2);
    CHECK(parsed->layout.ports[1].capacity.match == "in");
    CHECK_FALSE(parsed->heap);
    CHECK_FALSE(parsed->threads);
}
