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

TEST_CASE("malformed layouts are refused with a reason", "[graph][descriptor]") {
    auto reason = [](const char* json) {
        auto r = parseLayout(json);
        return r ? std::string("parsed") : std::string(r.error().what());
    };
    CHECK(reason(R"({"ports":[{"id":"a","dir":"in","kind":"events"},{"id":"a","dir":"out","kind":"events"}]})").find("duplicate") != std::string::npos);
    CHECK(reason(R"({"ports":[{"id":"l","dir":"in","kind":"link"}]})").find("link") != std::string::npos);
    CHECK(reason(R"({"ports":[{"id":"a","dir":"in","kind":"audio"}]})").find("channels") != std::string::npos);
    CHECK(reason(R"({"ports":[{"id":"a","dir":"sideways","kind":"events"}]})").find("dir") != std::string::npos);
    CHECK(reason(R"({"ports":[{"id":"g","dir":"in","kind":"control"}],"params":[{"id":"g","type":"float"}]})").find("shares an id") != std::string::npos);
    CHECK(reason(R"({"params":[{"id":"m","type":"enum","values":[]}]})").find("values") != std::string::npos);
    CHECK(reason("[1,2]").find("object") != std::string::npos);
}
