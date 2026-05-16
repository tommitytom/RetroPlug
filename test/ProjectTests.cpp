#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include "project/Project.hpp"
#include "project/ProjectConfig.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemConfig.hpp"
#include "system/mesen/GbaConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/MgbPassthroughRole.hpp"
#include "transport/MidiTypes.hpp"

namespace {

// Test double for SystemBase. Tracks construction/destruction so we can
// verify Project's pointer-swap semantics never accidentally double-free
// or leak. Mirrors the surface that swapSystem / adoptSystem actually use
// (just id() and the destructor).
class FakeSystem final : public SystemBase {
public:
    explicit FakeSystem(SystemId id, std::atomic<int>* aliveCounter = nullptr)
        : SystemBase(id), aliveCounter_(aliveCounter) {
        if (aliveCounter_) aliveCounter_->fetch_add(1, std::memory_order_relaxed);
    }
    ~FakeSystem() override {
        if (aliveCounter_) aliveCounter_->fetch_sub(1, std::memory_order_relaxed);
    }

    SystemKind kind() const override { return SystemKind::SameBoy; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    void onProcess(const AudioBlockInfo&, float* const*) override {}
    void onMidi(const MidiEvent* events, std::uint32_t count) override {
        for (std::uint32_t i = 0; i < count; ++i)
            received.push_back(events[i]);
    }
    SystemConfig snapshotConfig() const override { return SameBoyConfig{}; }

    std::vector<MidiEvent> received;

private:
    std::atomic<int>* aliveCounter_;
};

// Minimal channel-message helper: status nibble + channel (0-15) + two data
// bytes, encoded as a 3-byte event with no offset.
MidiEvent makeChannelMsg(std::uint8_t statusNibble, std::uint8_t chan,
                         std::uint8_t d1 = 0, std::uint8_t d2 = 0) {
    MidiEvent ev;
    ev.frame   = 0;
    ev.size    = 3;
    ev.data[0] = static_cast<std::uint8_t>((statusNibble & 0xF0) | (chan & 0x0F));
    ev.data[1] = d1;
    ev.data[2] = d2;
    return ev;
}

} // namespace

TEST_CASE("Project starts empty", "[Project]") {
    Project proj;
    REQUIRE(proj.systems().empty());
    REQUIRE(proj.findSystem(0) == nullptr);
    REQUIRE(proj.findSystem(1) == nullptr);
}

TEST_CASE("Project::nextSystemId returns increasing ids starting at 1", "[Project]") {
    Project proj;
    REQUIRE(proj.nextSystemId() == 1);
    REQUIRE(proj.nextSystemId() == 2);
    REQUIRE(proj.nextSystemId() == 3);
}

TEST_CASE("Project::adoptSystem installs and findSystem locates by id", "[Project]") {
    Project proj;
    std::atomic<int> alive{0};

    SystemId id = proj.nextSystemId();
    auto sys = std::make_unique<FakeSystem>(id, &alive);
    auto* raw = sys.get();

    REQUIRE(proj.adoptSystem(sys.release()) == id);
    REQUIRE(proj.systems().size() == 1);
    REQUIRE(proj.findSystem(id) == raw);
    REQUIRE(proj.findSystem(id + 100) == nullptr);
    REQUIRE(alive.load() == 1);
}

TEST_CASE("Project destruction frees adopted systems", "[Project]") {
    std::atomic<int> alive{0};
    {
        Project proj;
        proj.adoptSystem(new FakeSystem(proj.nextSystemId(), &alive));
        proj.adoptSystem(new FakeSystem(proj.nextSystemId(), &alive));
        REQUIRE(alive.load() == 2);
    }
    REQUIRE(alive.load() == 0);
}

TEST_CASE("Project::swapSystem replaces by id and returns the displaced pointer", "[Project][realtime]") {
    Project proj;
    std::atomic<int> alive{0};

    auto* original = new FakeSystem(proj.nextSystemId(), &alive);
    proj.adoptSystem(original);
    const SystemId originalId = original->id();

    auto* replacement = new FakeSystem(proj.nextSystemId(), &alive);

    REQUIRE(alive.load() == 2); // both alive — swap must NOT free either

    SystemBase* displaced = proj.swapSystem(originalId, replacement);

    REQUIRE(displaced == original);
    REQUIRE(proj.systems().size() == 1);
    REQUIRE(proj.systems().front().get() == replacement);
    REQUIRE(alive.load() == 2); // swap is pure pointer rotation, no free

    // Caller is responsible for disposing the displaced system off the audio
    // thread; deleting it manually here mirrors what the EventQueue path does.
    delete displaced;
    REQUIRE(alive.load() == 1);
}

TEST_CASE("Project::swapSystem with unknown id returns the input pointer untouched", "[Project]") {
    Project proj;
    std::atomic<int> alive{0};

    auto* incoming = new FakeSystem(proj.nextSystemId(), &alive);
    SystemBase* result = proj.swapSystem(/*id=*/9999, incoming);

    REQUIRE(result == incoming); // bridge can route back through EventQueue
    REQUIRE(proj.systems().empty());
    REQUIRE(alive.load() == 1);

    delete incoming;
    REQUIRE(alive.load() == 0);
}

TEST_CASE("Project::adoptSystem returns 0 for null input", "[Project]") {
    Project proj;
    REQUIRE(proj.adoptSystem(nullptr) == 0);
    REQUIRE(proj.systems().empty());
}

TEST_CASE("ProjectConfig round-trips an empty project through JSON", "[ProjectSerialization]") {
    ProjectConfig cfg;
    const std::string json = projectConfigToJson(cfg);
    REQUIRE(!json.empty());

    auto parsed = projectConfigFromJson(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->schemaVersion == "1.0");
    REQUIRE(parsed->systems.empty());
}

TEST_CASE("ProjectConfig round-trips a SameBoy system with embedded ROM bytes", "[ProjectSerialization]") {
    SameBoyConfig sb;
    sb.model    = SameBoyModel::DmgB;
    sb.fastBoot = false;
    sb.embedRom = true;
    sb.romPath  = "/path/to/lsdj.gb";
    sb.romBytes = Base64Bytes(std::vector<std::uint8_t>{
        0x00, 0xFF, 0x10, 0x20, 0x42, 0x99, 0xAB, 0xCD,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});
    sb.savestate = Base64Bytes(std::vector<std::uint8_t>{0x01, 0x02, 0x03});

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const std::string json = projectConfigToJson(cfg);
    INFO(json);

    auto parsed = projectConfigFromJson(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->systems.size() == 1);

    const auto* roundtripped = rfl::get_if<SameBoyConfig>(&parsed->systems.front().variant());
    REQUIRE(roundtripped != nullptr);
    CHECK(roundtripped->model    == SameBoyModel::DmgB);
    CHECK(roundtripped->fastBoot == false);
    CHECK(roundtripped->embedRom == true);
    CHECK(roundtripped->romPath  == "/path/to/lsdj.gb");
    CHECK(roundtripped->romBytes == sb.romBytes);
    CHECK(roundtripped->savestate == sb.savestate);
}

TEST_CASE("ProjectConfig round-trips a GBA system with embedded ROM bytes + biosPath", "[ProjectSerialization]") {
    GbaSystemConfig gb;
    gb.embedRom        = true;
    gb.skipBootScreen  = true;
    gb.gainDb          = -1.5f;
    gb.romPath         = "/path/to/nanoloop.gba";
    gb.biosPath        = "/some/firmware/gba_bios.bin";
    gb.romBytes = Base64Bytes(std::vector<std::uint8_t>{
        0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21,
        0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
        0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21});
    gb.sram      = Base64Bytes(std::vector<std::uint8_t>{0xCA, 0xFE});
    gb.savestate = Base64Bytes(std::vector<std::uint8_t>{0x01, 0x02, 0x03});

    ProjectConfig cfg;
    cfg.systems.push_back(gb);

    const std::string json = projectConfigToJson(cfg);
    INFO(json);

    auto parsed = projectConfigFromJson(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->systems.size() == 1);

    const auto* roundtripped = rfl::get_if<GbaSystemConfig>(&parsed->systems.front().variant());
    REQUIRE(roundtripped != nullptr);
    CHECK(roundtripped->embedRom       == true);
    CHECK(roundtripped->skipBootScreen == true);
    CHECK(roundtripped->gainDb         == -1.5f);
    CHECK(roundtripped->romPath        == "/path/to/nanoloop.gba");
    CHECK(roundtripped->biosPath       == "/some/firmware/gba_bios.bin");
    CHECK(roundtripped->romBytes       == gb.romBytes);
    CHECK(roundtripped->sram           == gb.sram);
    CHECK(roundtripped->savestate      == gb.savestate);
    CHECK(roundtripped->roles.empty());
}

TEST_CASE("projectConfigFromJson reports failure for malformed JSON", "[ProjectSerialization]") {
    REQUIRE_FALSE(projectConfigFromJson("{ this is not json").has_value());
    REQUIRE_FALSE(projectConfigFromJson("").has_value());
}

TEST_CASE("SameBoyConfig round-trips an attached MgbRoleConfig role",
          "[ProjectSerialization][role]") {
    SameBoyConfig sb;
    sb.romPath = "/some/path/mGB.gb";
    sb.roles.emplace_back(MgbRoleConfig{});

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const std::string json = projectConfigToJson(cfg);
    INFO(json);

    auto parsed = projectConfigFromJson(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->systems.size() == 1);

    const auto* roundtripped = rfl::get_if<SameBoyConfig>(&parsed->systems.front().variant());
    REQUIRE(roundtripped != nullptr);
    REQUIRE(roundtripped->roles.size() == 1);
    REQUIRE(rfl::get_if<MgbRoleConfig>(&roundtripped->roles.front().variant()) != nullptr);
}

TEST_CASE("ProjectConfig round-trips multi-instance fields (layout, gainDb, linkGroupId)",
          "[ProjectSerialization][multi]") {
    SameBoyConfig a;
    a.romPath     = "/a.gb";
    a.gainDb      = -3.5f;
    a.linkGroupId = 1;
    a.sram        = Base64Bytes(std::vector<std::uint8_t>{
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80});

    SameBoyConfig b;
    b.romPath     = "/b.gb";
    b.gainDb      =  2.0f;
    b.linkGroupId = 1;

    SameBoyConfig c;
    c.romPath     = "/c.gb";
    c.linkGroupId = 0; // standalone

    ProjectConfig cfg;
    cfg.settings.layout = SystemLayout::Grid;
    cfg.systems.push_back(a);
    cfg.systems.push_back(b);
    cfg.systems.push_back(c);

    const std::string json = projectConfigToJson(cfg);
    auto parsed = projectConfigFromJson(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->settings.layout == SystemLayout::Grid);
    REQUIRE(parsed->systems.size() == 3);

    auto getSb = [](const SystemConfig& s) {
        return rfl::get_if<SameBoyConfig>(&s.variant());
    };
    const auto* sa = getSb(parsed->systems[0]);
    const auto* sb = getSb(parsed->systems[1]);
    const auto* sc = getSb(parsed->systems[2]);
    REQUIRE(sa); REQUIRE(sb); REQUIRE(sc);
    CHECK(sa->gainDb      == -3.5f);
    CHECK(sb->gainDb      ==  2.0f);
    CHECK(sa->linkGroupId == 1);
    CHECK(sb->linkGroupId == 1);
    CHECK(sc->linkGroupId == 0);
    CHECK(sa->sram        == a.sram);
    CHECK(sb->sram.empty());
    CHECK(sc->sram.empty());
}

TEST_CASE("dispatchMidi SendToAll broadcasts every event to every system",
          "[Project][midi]") {
    Project proj;
    auto* a = new FakeSystem(proj.nextSystemId());
    auto* b = new FakeSystem(proj.nextSystemId());
    proj.adoptSystem(a);
    proj.adoptSystem(b);

    MidiEvent ev = makeChannelMsg(0x90, /*chan=*/5, 60, 100);
    proj.dispatchMidi(&ev, 1, MidiRouting::SendToAll);

    REQUIRE(a->received.size() == 1);
    REQUIRE(b->received.size() == 1);
    CHECK(a->received[0].data[0] == ev.data[0]); // channel preserved
    CHECK(b->received[0].data[0] == ev.data[0]);
}

TEST_CASE("dispatchMidi FourChannelsPerInstance maps channel band to instance, channel preserved",
          "[Project][midi]") {
    Project proj;
    auto* a = new FakeSystem(proj.nextSystemId()); // channels 1-4
    auto* b = new FakeSystem(proj.nextSystemId()); // channels 5-8
    auto* c = new FakeSystem(proj.nextSystemId()); // channels 9-12
    proj.adoptSystem(a);
    proj.adoptSystem(b);
    proj.adoptSystem(c);

    // chan=0 (logical ch1) → band 0 → instance 0
    MidiEvent ev0 = makeChannelMsg(0x90, 0,  60, 100);
    // chan=6 (logical ch7) → band 1 → instance 1
    MidiEvent ev1 = makeChannelMsg(0x90, 6,  61, 100);
    // chan=11 (logical ch12) → band 2 → instance 2
    MidiEvent ev2 = makeChannelMsg(0x90, 11, 62, 100);

    proj.dispatchMidi(&ev0, 1, MidiRouting::FourChannelsPerInstance);
    proj.dispatchMidi(&ev1, 1, MidiRouting::FourChannelsPerInstance);
    proj.dispatchMidi(&ev2, 1, MidiRouting::FourChannelsPerInstance);

    REQUIRE(a->received.size() == 1);
    REQUIRE(b->received.size() == 1);
    REQUIRE(c->received.size() == 1);
    CHECK(a->received[0].data[0] == ev0.data[0]);
    CHECK(b->received[0].data[0] == ev1.data[0]);
    CHECK(c->received[0].data[0] == ev2.data[0]);
}

TEST_CASE("dispatchMidi FourChannelsPerInstance wraps for 5+ instance bands",
          "[Project][midi]") {
    Project proj;
    auto* a = new FakeSystem(proj.nextSystemId());
    auto* b = new FakeSystem(proj.nextSystemId());
    proj.adoptSystem(a);
    proj.adoptSystem(b);

    // chan=8 (band 2) with N=2 wraps to instance 0; chan=15 (band 3) wraps to b.
    MidiEvent toA = makeChannelMsg(0x90, 8);
    MidiEvent toB = makeChannelMsg(0x90, 15);
    proj.dispatchMidi(&toA, 1, MidiRouting::FourChannelsPerInstance);
    proj.dispatchMidi(&toB, 1, MidiRouting::FourChannelsPerInstance);

    REQUIRE(a->received.size() == 1);
    REQUIRE(b->received.size() == 1);
}

TEST_CASE("dispatchMidi OneChannelPerInstance routes by channel index, preserves channel",
          "[Project][midi]") {
    Project proj;
    auto* a = new FakeSystem(proj.nextSystemId());
    auto* b = new FakeSystem(proj.nextSystemId());
    proj.adoptSystem(a);
    proj.adoptSystem(b);

    MidiEvent toA = makeChannelMsg(0x90, 0); // chan=0 -> instance 0
    MidiEvent toB = makeChannelMsg(0x90, 1); // chan=1 -> instance 1
    MidiEvent wrapA = makeChannelMsg(0x90, 2); // chan=2 wraps -> instance 0
    proj.dispatchMidi(&toA,   1, MidiRouting::OneChannelPerInstance);
    proj.dispatchMidi(&toB,   1, MidiRouting::OneChannelPerInstance);
    proj.dispatchMidi(&wrapA, 1, MidiRouting::OneChannelPerInstance);

    REQUIRE(a->received.size() == 2);
    REQUIRE(b->received.size() == 1);
    CHECK(a->received[0].data[0] == toA.data[0]);   // unchanged
    CHECK(b->received[0].data[0] == toB.data[0]);
    CHECK(a->received[1].data[0] == wrapA.data[0]); // unchanged
}

TEST_CASE("dispatchMidi MidiChannelToInstance routes by channel and rewrites to ch1",
          "[Project][midi]") {
    Project proj;
    auto* a = new FakeSystem(proj.nextSystemId());
    auto* b = new FakeSystem(proj.nextSystemId());
    proj.adoptSystem(a);
    proj.adoptSystem(b);

    MidiEvent ev = makeChannelMsg(0x90, /*chan=*/1, 60, 100);
    proj.dispatchMidi(&ev, 1, MidiRouting::MidiChannelToInstance);

    REQUIRE(a->received.empty());
    REQUIRE(b->received.size() == 1);
    // Status nibble preserved (0x90), channel rewritten to 0 (= MIDI ch1).
    CHECK(b->received[0].data[0] == 0x90);
    // Data bytes untouched.
    CHECK(b->received[0].data[1] == 60);
    CHECK(b->received[0].data[2] == 100);
}

TEST_CASE("dispatchMidi broadcasts system messages (status >= 0xF0) regardless of routing",
          "[Project][midi]") {
    Project proj;
    auto* a = new FakeSystem(proj.nextSystemId());
    auto* b = new FakeSystem(proj.nextSystemId());
    proj.adoptSystem(a);
    proj.adoptSystem(b);

    MidiEvent clock; // 0xF8 — MIDI clock, no channel
    clock.frame   = 0;
    clock.size    = 1;
    clock.data[0] = 0xF8;

    proj.dispatchMidi(&clock, 1, MidiRouting::OneChannelPerInstance);

    REQUIRE(a->received.size() == 1);
    REQUIRE(b->received.size() == 1);
    CHECK(a->received[0].data[0] == 0xF8);
    CHECK(b->received[0].data[0] == 0xF8);
}

TEST_CASE("dispatchMidi is a no-op for empty arrays / empty projects", "[Project][midi]") {
    Project proj;
    MidiEvent ev = makeChannelMsg(0x90, 0);

    // Empty project — must not crash.
    proj.dispatchMidi(&ev, 1, MidiRouting::SendToAll);

    auto* a = new FakeSystem(proj.nextSystemId());
    proj.adoptSystem(a);

    // Null pointer / zero count — no-ops.
    proj.dispatchMidi(nullptr, 1, MidiRouting::SendToAll);
    proj.dispatchMidi(&ev, 0, MidiRouting::SendToAll);
    REQUIRE(a->received.empty());
}

TEST_CASE("Project::reserve does not create systems", "[Project]") {
    Project proj;
    proj.reserve(16);
    REQUIRE(proj.systems().empty());
    // After reserve, adoptSystem should not need to grow the vector. We
    // can't directly test the absence of allocation in std::vector, but we
    // can verify pointer stability — the vector slot's address shouldn't
    // change as we add up to the reserved count.
    proj.adoptSystem(new FakeSystem(proj.nextSystemId()));
    auto* slotAddr = &proj.systems()[0];
    for (int i = 0; i < 8; ++i) {
        proj.adoptSystem(new FakeSystem(proj.nextSystemId()));
    }
    REQUIRE(&proj.systems()[0] == slotAddr);
}
