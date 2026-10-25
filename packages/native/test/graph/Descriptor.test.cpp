// The descriptor model: layouts and type descriptors survive a write -> parse round trip, and malformed
// JSON from a node (a downloaded pack, say) is refused with a reason.
#include <catch2/catch_test_macros.hpp>

#include "graph/Descriptor.hpp"

using namespace rp::graph;

TEST_CASE("a layout round-trips through its canonical JSON", "[graph][descriptor]") {
    Layout l;
    l.ports.push_back({"in", PortDir::In, PortKind::Audio, ChannelSpec::any(), {}, false});
    l.ports.push_back({"target", PortDir::In, PortKind::Resource, {}, "audio-buffer", true});
    l.ports.push_back({"link", PortDir::Link, PortKind::Link, {}, {}, false});
    l.ports.push_back({"out", PortDir::Out, PortKind::Audio, ChannelSpec::matching("in"), {}, false});
    l.ports.push_back({"pins.pulse", PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
    l.params.push_back({"gain", "Gain", ParamType::Float, -60, 12, 0, {}, "dB"});
    l.params.push_back({"mode", "Mode", ParamType::Enum, 0, 2, 1, {"off", "midi-sync", "master"}, {}});
    l.params.push_back({"mute", "Mute", ParamType::Bool, 0, 1, 1, {}, {}});
    l.exclusive.push_back({"pins.*", "channels.*"});

    const std::string json = layoutJson(l);
    auto back = parseLayout(json);
    REQUIRE(back);
    CHECK(layoutJson(*back) == json);
    CHECK(back->params[1].def == 1.0);  // the enum default travels as its label and comes back as its index
    CHECK(json.find(R"("default":"midi-sync")") != std::string::npos);
    CHECK(json.find(R"("access":"write")") != std::string::npos);
    CHECK(matchesPattern("pins.*", "pins.pulse"));
    CHECK_FALSE(matchesPattern("pins.*", "pinsx"));
    CHECK(matchesPattern("mix", "mix"));
}

TEST_CASE("a type descriptor round-trips, settings schema included", "[graph][descriptor]") {
    TypeDescriptor d;
    d.id = "rp.test.x";
    d.version = "2.0.0";
    d.name = "X";
    d.category = "test";
    d.heap = true;
    d.settingsSchema = R"({"type":"object","properties":{"n":{"type":"integer"}}})";
    const std::string json = descriptorJson(d);
    auto back = parseDescriptor(json);
    REQUIRE(back);
    CHECK(back->heap);
    CHECK_FALSE(back->threads);
    CHECK(descriptorJson(*back) == json);
}

namespace {
std::string layoutReason(const char* json) {
    auto r = parseLayout(json);
    return r ? std::string("parsed") : std::string(r.error().what());
}
std::string descriptorReason(const char* json) {
    auto r = parseDescriptor(json);
    return r ? std::string("parsed") : std::string(r.error().what());
}
}  // namespace

TEST_CASE("malformed layouts are refused with a reason", "[graph][descriptor]") {
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"events"},{"id":"a","dir":"out","kind":"events"}]})") ==
          "\"a\" is used by more than one port or param");
    CHECK(layoutReason(R"({"ports":[{"id":"l","dir":"in","kind":"link"}]})").find("link") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio"}]})") == "port \"a\": an audio port needs \"channels\"");
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"sideways","kind":"events"}]})") == "port \"a\": unknown dir \"sideways\"");
    CHECK(layoutReason(R"({"ports":[{"id":"g","dir":"in","kind":"control"}],"params":[{"id":"g","type":"float"}]})") ==
          "\"g\" is used by more than one port or param");
    CHECK(layoutReason(R"({"params":[{"id":"m","type":"enum","values":[]}]})").find("values") != std::string::npos);
    CHECK(layoutReason("[1,2]") == "layout must be an object");
    CHECK(layoutReason(R"({"ports":[{"id":5,"dir":"in","kind":"events"}]})") == "port #0: \"id\" must be a string");
    CHECK(layoutReason(R"({"ports":[{"id":"a b","dir":"in","kind":"events"}]})").find("whitespace") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":".*","dir":"out","kind":"events"}]})").find("'*'") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"g\u0001","type":"float"}]})").find("control characters") != std::string::npos);
    CHECK_FALSE(matchesPattern(".*", ".x"));  // a wildcard needs a prefix, as the exclusive parser says
}

TEST_CASE("param ranges are validated, so the planner's clamp is always well-defined", "[graph][descriptor]") {
    CHECK(layoutReason(R"({"params":[{"id":"g","type":"float","min":5,"max":1}]})").find("must not exceed") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"g","type":"float","min":0,"max":1,"default":2}]})").find("within") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"n","type":"int","min":0.5,"max":3}]})").find("whole numbers") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"g","type":"float","min":"0"}]})").find("finite number") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"g","type":"float"},{"id":"g","type":"bool"}]})").find("more than one") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"b","type":"bool","default":1}]})").find("true or false") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"m","type":"enum","values":["a","b"],"default":"c"}]})").find("not one of its values") != std::string::npos);
    CHECK(layoutReason(R"({"params":[{"id":"m","type":"enum","values":["a","a"]}]})").find("listed twice") != std::string::npos);
    const auto ok = parseLayout(R"({"params":[{"id":"g","type":"float","min":-1,"max":1,"default":0.5,"unit":""}]})");
    REQUIRE(ok);
    CHECK(ok->params[0].name == "g");  // a missing name is the id
}

TEST_CASE("channel specs are exact", "[graph][descriptor]") {
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"count":4294967297,"layout":"x"}}]})").find("4294967295") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"count":2,"layout":"mono"}}]})").find("is 1 channels") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"any":false}}]})").find("must be true") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"any":true,"count":2}}]})").find("exactly one") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"out","kind":"audio","channels":{"any":true}}]})").find("cannot be \"any\"") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"out","kind":"audio","channels":{"match":"a"}}]})").find("not another audio port") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"e","dir":"in","kind":"events","channels":{"any":true}}]})").find("only audio ports") != std::string::npos);
    CHECK(layoutReason(R"x({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"count":5,"layout":"discrete(03)"}}]})x").find("not a discrete(n)") != std::string::npos);
    const auto two = parseLayout(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"count":2.0,"layout":"stereo"}}]})");
    REQUIRE(two);  // 2.0 is the same number JavaScript writes as 2
    CHECK(two->ports[0].channels.count == 2);
}

TEST_CASE("resource access and exclusive groups are exact", "[graph][descriptor]") {
    CHECK(layoutReason(R"({"ports":[{"id":"r","dir":"in","kind":"resource","resourceType":"x","access":"wirte"}]})").find("can only be") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"e","dir":"in","kind":"events","resourceType":"x"}]})").find("only resource ports") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"r","dir":"in","kind":"resource","resourceType":"audio buffer"}]})").find("whitespace") != std::string::npos);
    // a misspelt field is refused, not ignored: "acess" would otherwise declare a reader where a writer was meant
    CHECK(layoutReason(R"({"ports":[{"id":"r","dir":"in","kind":"resource","resourceType":"x","acess":"write"}]})") ==
          "port #0: unknown field \"acess\"");
    CHECK(layoutReason(R"({"params":[{"id":"g","type":"float","dfault":1}]})").find("unknown field") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"any":true,"cout":2}}]})").find("unknown field") != std::string::npos);
    CHECK(layoutReason(R"({"exclusve":[["a"]]})").find("unknown field") != std::string::npos);
    CHECK(descriptorReason(R"({"id":"x","catgory":"y"})").find("unknown field") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"o","dir":"out","kind":"events"}],"exclusive":[["nope"]]})").find("exclusive member") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"o","dir":"out","kind":"events"}],"exclusive":[[]]})").find("non-empty") != std::string::npos);
    CHECK(parseLayout(R"({"ports":[{"id":"o","dir":"out","kind":"events"}],"exclusive":[["o","pins.*"]]})"));
    CHECK(layoutReason(R"({"ports":[{"id":"o","dir":"out","kind":"events"}],"exclusive":[["o","o"]]})").find("overlap") != std::string::npos);
    CHECK(layoutReason(R"({"ports":[{"id":"pins.a","dir":"out","kind":"events"}],"exclusive":[["pins.*","pins.a"]]})").find("overlap") != std::string::npos);
    CHECK(layoutReason(R"({"exclusive":[["pins.*","pins.x.*"]]})").find("overlap") != std::string::npos);
}

TEST_CASE("a descriptor's requires flags are exact", "[graph][descriptor]") {
    CHECK(descriptorReason(R"({"id":"x","requires":{"heap":1}})").find("true or false") != std::string::npos);
    CHECK(descriptorReason(R"({"id":"x","requires":{"gpu":true}})").find("does not provide") != std::string::npos);
    CHECK(descriptorReason(R"({"id":"has space"})").find("whitespace") != std::string::npos);
    const auto d = parseDescriptor(R"({"id":"x","requires":{"gpu":false}})");
    REQUIRE(d);
    CHECK(d->name == "x");
    CHECK(d->settingsSchema == R"({"type":"object"})");
}

namespace {
std::string capacityReason(const char* kind, const char* capacity) {
    return layoutReason(
        (std::string(R"({"ports":[{"id":"q","dir":"out","kind":")") + kind + R"(","capacity":)" + capacity + "}]}").c_str());
}
}  // namespace

TEST_CASE("an events or bytes port may declare its queue capacity, either part, up to the caps", "[graph][descriptor]") {
    const auto l = parseLayout(R"({"ports":[{"id":"m","dir":"in","kind":"events","capacity":{"messages":2}},)"
                               R"({"id":"s","dir":"out","kind":"bytes","capacity":{"messages":4,"bytes":65536}},)"
                               R"({"id":"b","dir":"out","kind":"bytes","capacity":{"bytes":1}},)"
                               R"({"id":"d","dir":"out","kind":"events"}]})");
    REQUIRE(l);
    CHECK(l->ports[0].capacity.messageCount() == 2);
    CHECK(l->ports[0].capacity.byteCount() == kDefaultQueueBytes);  // the part left undeclared
    CHECK(l->ports[1].capacity.byteCount() == 65536);
    CHECK(l->ports[2].capacity.messageCount() == kDefaultQueueMessages);
    CHECK_FALSE(l->ports[3].capacity.messages);
    CHECK(l->ports[3].capacity.messageCount() == 256);  // today's numbers
    CHECK(l->ports[3].capacity.byteCount() == 4096);
    // Spelt back as declared: a part is written only when it was given, so an undeclared port writes nothing.
    CHECK(parseLayout(layoutJson(*l)));
    CHECK(layoutJson(*parseLayout(layoutJson(*l))) == layoutJson(*l));
    CHECK(layoutJson(*l).find(R"({"capacity":{"bytes":65536,"messages":4},"dir":"out","id":"s","kind":"bytes"})") != std::string::npos);
    CHECK(layoutJson(*l).find(R"({"dir":"out","id":"d","kind":"events"})") != std::string::npos);
    CHECK(capacityReason("events", R"({"messages":1048576,"bytes":16777216})") == "parsed");  // the caps themselves
}

TEST_CASE("a queue capacity is refused past its cap, empty, fractional, misspelt or on another kind", "[graph][descriptor]") {
    CHECK(capacityReason("events", R"({"messages":1048577})") ==
          R"(port "q": "capacity.messages" must be a whole number from 1 to 1048576)");
    CHECK(capacityReason("bytes", R"({"bytes":16777217})") ==
          R"(port "q": "capacity.bytes" must be a whole number from 1 to 16777216)");
    CHECK(capacityReason("events", R"({"messages":0})").find("from 1 to 1048576") != std::string::npos);
    CHECK(capacityReason("events", R"({"messages":2.5})").find("whole number") != std::string::npos);
    CHECK(capacityReason("events", R"({"messages":"2"})").find("whole number") != std::string::npos);
    CHECK(capacityReason("events", R"({"bytes":-1})").find("from 1 to 16777216") != std::string::npos);
    CHECK(capacityReason("events", "{}") == R"(port "q": "capacity" needs "messages", "bytes" or both)");
    CHECK(capacityReason("events", "8") == R"(port "q": "capacity" must be an object)");
    CHECK(capacityReason("events", R"({"mesages":2})") == R"(port "q" capacity: unknown field "mesages")");
    CHECK(capacityReason("control", R"({"messages":2})") == R"(port "q": only events and bytes ports have "capacity")");
    CHECK(layoutReason(R"({"ports":[{"id":"a","dir":"in","kind":"audio","channels":{"any":true},"capacity":{"messages":2}}]})") ==
          R"(port "a": only events and bytes ports have "capacity")");
    // The C++ layer asks the same of a layout it builds, so a capacity adjust_layout set is checked like a parsed one.
    PortDesc p{"q", PortDir::Out, PortKind::Events, {}, {}, false};
    CHECK_FALSE(checkCapacity(p));
    p.capacity.messages = kMaxQueueMessages + 1;
    CHECK(checkCapacity(p) == std::optional<std::string>(R"(port "q": "capacity.messages" must be a whole number from 1 to 1048576)"));
    p.capacity.messages = 2;
    p.kind = PortKind::Audio;
    CHECK(checkCapacity(p) == std::optional<std::string>(R"(port "q": only events and bytes ports have "capacity")"));
}
