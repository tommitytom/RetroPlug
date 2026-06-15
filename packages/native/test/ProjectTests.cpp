#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

#include "project/Project.hpp"
#include "project/ProjectConfig.hpp"
#include "project/ProjectMissingFiles.hpp"
#include "project/ProjectSerialization.hpp"
#include "util/MinizZip.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemConfig.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
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

TEST_CASE("ProjectConfig round-trips an empty project through zip", "[ProjectSerialization]") {
    ProjectConfig cfg;
    const auto blob = projectConfigToZip(cfg);
    REQUIRE(!blob.empty());
    // PKZIP local file header magic: 0x50 0x4B 0x03 0x04
    REQUIRE(blob.size() >= 4);
    REQUIRE(blob[0] == 0x50);
    REQUIRE(blob[1] == 0x4B);

    auto parsed = projectConfigFromZip(blob);
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
    sb.romBytes = {
        0x00, 0xFF, 0x10, 0x20, 0x42, 0x99, 0xAB, 0xCD,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    sb.savestate = {0x01, 0x02, 0x03};

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const auto blob = projectConfigToZip(cfg);
    REQUIRE(!blob.empty());

    auto parsed = projectConfigFromZip(blob);
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
    MesenGbaConfig gb;
    gb.embedRom        = true;
    gb.skipBootScreen  = true;
    gb.gainDb          = -1.5f;
    gb.romPath         = "/path/to/nanoloop.gba";
    gb.biosPath        = "/some/firmware/gba_bios.bin";
    gb.romBytes = {
        0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21,
        0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
        0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21};
    gb.sram      = {0xCA, 0xFE};
    gb.savestate = {0x01, 0x02, 0x03};

    ProjectConfig cfg;
    cfg.systems.push_back(gb);

    const auto blob = projectConfigToZip(cfg);
    REQUIRE(!blob.empty());

    auto parsed = projectConfigFromZip(blob);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->systems.size() == 1);

    const auto* roundtripped = rfl::get_if<MesenGbaConfig>(&parsed->systems.front().variant());
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

TEST_CASE("projectConfigFromZip reports failure for malformed input", "[ProjectSerialization]") {
    REQUIRE_FALSE(projectConfigFromZip(std::span<const std::uint8_t>{}).has_value());
    const std::uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    REQUIRE_FALSE(projectConfigFromZip(std::span<const std::uint8_t>(garbage, sizeof(garbage))).has_value());
}

// Tag with `[.diag]` so this is hidden from the default test run (Catch2
// treats tags beginning with a dot as hidden). Invoke explicitly to dump a
// representative project to /tmp/sanity.rplg + /tmp/sanity.json so an
// external `unzip -l` can confirm the on-disk format.
TEST_CASE("zip vs JSON size for a representative project",
          "[.diag-zip-write]") {
    SameBoyConfig sb;
    sb.romPath   = "/path/to/lsdj.gb";
    sb.romBytes  = std::vector<std::uint8_t>(1024 * 1024, 0x42);
    sb.sram      = std::vector<std::uint8_t>(32 * 1024, 0xAA);
    sb.savestate = std::vector<std::uint8_t>(64 * 1024, 0x33);

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const auto json = projectConfigToJson(cfg);
    const auto zip  = projectConfigToZip(cfg);

    {
        std::ofstream f("/tmp/sanity.rplg", std::ios::binary);
        f.write(reinterpret_cast<const char*>(zip.data()), zip.size());
    }
    {
        std::ofstream f("/tmp/sanity.json", std::ios::binary);
        f << json;
    }

    UNSCOPED_INFO("json size: " << json.size() << " bytes");
    UNSCOPED_INFO("zip  size: " << zip.size()  << " bytes");
    INFO("ratio: " << (100.0 * zip.size() / json.size()) << "%");
    CHECK(zip.size() < json.size() / 2); // expect >2x shrinkage
}

TEST_CASE("projectConfigToZip writes binaries to per-system zip entries",
          "[ProjectSerialization][zip]") {
    // Pin down the on-disk entry contract: project.json plus
    // systems/{i}/{rom,sram,state} per system that supplies bytes. The
    // JSON entry must NOT contain the raw blobs (stripped before write).
    SameBoyConfig sb;
    sb.romPath  = "/r.gb";
    sb.romBytes = {0xAA, 0xBB, 0xCC, 0xDD};
    sb.sram     = {0x11, 0x22};
    // savestate intentionally empty — must NOT produce an entry.

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const auto blob = projectConfigToZip(cfg);
    REQUIRE(!blob.empty());

    MinizReader zip(blob);
    REQUIRE(zip.valid());
    REQUIRE(zip.has("project.json"));
    REQUIRE(zip.has("systems/0/rom"));
    REQUIRE(zip.has("systems/0/sram"));
    REQUIRE_FALSE(zip.has("systems/0/state"));

    const auto rom = zip.read("systems/0/rom");
    REQUIRE(rom == std::vector<std::uint8_t>{0xAA, 0xBB, 0xCC, 0xDD});
    const auto sram = zip.read("systems/0/sram");
    REQUIRE(sram == std::vector<std::uint8_t>{0x11, 0x22});

    // project.json carries metadata only — binary fields serialize as
    // empty arrays after the stripping pass.
    const std::string json = zip.readString("project.json");
    REQUIRE(json.find("\"romBytes\":[]") != std::string::npos);
}

TEST_CASE("projectConfigToJsonFile drops binaries but keeps romPath",
          "[ProjectSerialization][json]") {
    // Path-only disk save: config + romPath only, no embedded blobs. ROM/SRAM
    // are re-read from disk on load; savestate/kit bytes are dropped.
    SameBoyConfig sb;
    sb.romPath   = "/r.gb";
    sb.romBytes  = {0xAA, 0xBB, 0xCC, 0xDD};
    sb.sram      = {0x11, 0x22};
    sb.savestate = {0x33, 0x44};

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const std::string json = projectConfigToJsonFile(cfg);
    REQUIRE(!json.empty());
    // No raw blobs in the JSON — every binary field serializes as `[]`.
    CHECK(json.find("\"romBytes\":[]")  != std::string::npos);
    CHECK(json.find("\"sram\":[]")      != std::string::npos);
    CHECK(json.find("\"savestate\":[]") != std::string::npos);
    // ...but the path survives so the ROM can be re-read on load.
    CHECK(json.find("/r.gb") != std::string::npos);
    // The source config is untouched (clear operates on a copy).
    CHECK(cfg.systems.size() == 1);

    // Round-trips through the autodetecting loader as JSON (no PK magic).
    const std::vector<std::uint8_t> bytes(json.begin(), json.end());
    auto parsed = projectConfigFromBytes(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->systems.size() == 1);
    const auto* rt = rfl::get_if<SameBoyConfig>(&parsed->systems.front().variant());
    REQUIRE(rt != nullptr);
    CHECK(rt->romPath == "/r.gb");
    CHECK(rt->romBytes.empty());
    CHECK(rt->sram.empty());
    CHECK(rt->savestate.empty());
}

TEST_CASE("projectConfigFromBytes autodetects zip vs JSON",
          "[ProjectSerialization][json]") {
    SameBoyConfig sb;
    sb.romPath  = "/r.gb";
    sb.romBytes = {0xAA, 0xBB, 0xCC, 0xDD};
    sb.sram     = {0x11, 0x22};

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    // Zip blob (PK magic) still restores the embedded binaries.
    const auto zip = projectConfigToZip(cfg);
    REQUIRE(zip.size() >= 2);
    REQUIRE(zip[0] == 'P');
    REQUIRE(zip[1] == 'K');
    auto fromZip = projectConfigFromBytes(zip);
    REQUIRE(fromZip.has_value());
    const auto* z = rfl::get_if<SameBoyConfig>(&fromZip->systems.front().variant());
    REQUIRE(z != nullptr);
    CHECK(z->romBytes == std::vector<std::uint8_t>{0xAA, 0xBB, 0xCC, 0xDD});
    CHECK(z->sram == std::vector<std::uint8_t>{0x11, 0x22});
}

TEST_CASE("SameBoyConfig round-trips an attached MgbRoleConfig role",
          "[ProjectSerialization][role]") {
    SameBoyConfig sb;
    sb.romPath = "/some/path/mGB.gb";
    sb.roles.emplace_back(MgbRoleConfig{});

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const auto blob = projectConfigToZip(cfg);
    REQUIRE(!blob.empty());

    auto parsed = projectConfigFromZip(blob);
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
    a.sram        = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

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

    const auto blob = projectConfigToZip(cfg);
    auto parsed = projectConfigFromZip(blob);
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

// ----- LsdjKitPatchConfig round-trip -----------------------------------------

TEST_CASE("SameBoyConfig round-trips an attached LsdjKitPatchConfig role",
          "[ProjectSerialization][role][kit]") {
    // Fabricate a 16 KB kit bank as if compileKit had produced it. Real
    // kit bytes are arbitrary; we only need the round-trip to preserve them
    // bit-for-bit so the runtime role can re-apply on project load.
    std::vector<std::uint8_t> bank(0x4000, 0);
    for (std::size_t i = 0; i < bank.size(); ++i) {
        bank[i] = static_cast<std::uint8_t>((i * 31u) & 0xFF);
    }

    rp::lsdj::LsdjKitPatchConfig kit;
    rp::lsdj::LsdjKitConfig slot0;
    slot0.slot          = 0;
    slot0.name          = "DRUMS";
    slot0.compiledBytes = bank;
    slot0.compiledHash  = 0xDEADBEEFCAFEBABEULL;
    rp::lsdj::LsdjSampleConfig kick;
    kick.path       = "/some/path/kick.wav";
    kick.name       = "KIK";
    kick.pitch      = 0x7F;
    kick.volume     = 0xFF;
    kick.sourceHash = 0xAA55AA55ULL;
    slot0.samples.push_back(kick);
    kit.kits.push_back(slot0);

    SameBoyConfig sb;
    sb.romPath = "/some/path/lsdj.gb";
    sb.roles.emplace_back(std::move(kit));

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const auto blob = projectConfigToZip(cfg);
    REQUIRE(!blob.empty());
    auto parsed = projectConfigFromZip(blob);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->systems.size() == 1);

    const auto* sbOut = rfl::get_if<SameBoyConfig>(&parsed->systems.front().variant());
    REQUIRE(sbOut != nullptr);
    REQUIRE(sbOut->roles.size() == 1);
    const auto* kitOut = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&sbOut->roles.front().variant());
    REQUIRE(kitOut != nullptr);
    REQUIRE(kitOut->kits.size() == 1);

    const auto& s0 = kitOut->kits.front();
    REQUIRE(s0.slot          == 0);
    REQUIRE(s0.name          == "DRUMS");
    REQUIRE(s0.compiledHash  == 0xDEADBEEFCAFEBABEULL);
    REQUIRE(s0.compiledBytes.size() == bank.size());
    REQUIRE(s0.compiledBytes == bank);  // bit-for-bit

    REQUIRE(s0.samples.size() == 1);
    REQUIRE(s0.samples[0].path       == "/some/path/kick.wav");
    REQUIRE(s0.samples[0].name       == "KIK");
    REQUIRE(s0.samples[0].pitch      == 0x7F);
    REQUIRE(s0.samples[0].volume     == 0xFF);
    REQUIRE(s0.samples[0].sourceHash == 0xAA55AA55ULL);
}

TEST_CASE("path-only JSON drops kit bytes but keeps recompile inputs",
          "[ProjectSerialization][json][kit]") {
    // The path-only save must drop compiledBytes/hash (kits are recompiled on
    // load) yet keep every input needed to recompile: sample path/name and the
    // offset/length/effects.
    rp::lsdj::LsdjKitConfig slot0;
    slot0.slot          = 0;
    slot0.name          = "DRUMS";
    slot0.compiledBytes = std::vector<std::uint8_t>(0x4000, 0xAB);
    slot0.compiledHash  = 0xDEADBEEFCAFEBABEULL;
    rp::lsdj::LsdjSampleConfig kick;
    kick.path   = "/some/path/kick.wav";
    kick.name   = "KIK";
    kick.offset = 128;
    kick.length = 4096;
    kick.effects.push_back(rp::lsdj::GainEffect{/*normalize*/ true, /*gain*/ 1.5f});
    kick.effects.push_back(rp::lsdj::DitherEffect{rp::lsdj::DitherType::ShapedTPDF});
    slot0.samples.push_back(kick);

    rp::lsdj::LsdjKitPatchConfig kit;
    kit.kits.push_back(slot0);

    SameBoyConfig sb;
    sb.romPath = "/some/path/lsdj.gb";
    sb.roles.emplace_back(std::move(kit));

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    const std::string json = projectConfigToJsonFile(cfg);
    REQUIRE(!json.empty());
    CHECK(json.find("\"compiledBytes\":[]") != std::string::npos); // bytes dropped
    CHECK(json.find("kick.wav") != std::string::npos);             // sample link kept

    auto parsed = projectConfigFromBytes(
        std::vector<std::uint8_t>(json.begin(), json.end()));
    REQUIRE(parsed.has_value());
    const auto* sbOut = rfl::get_if<SameBoyConfig>(&parsed->systems.front().variant());
    REQUIRE(sbOut != nullptr);
    const auto* kitOut = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&sbOut->roles.front().variant());
    REQUIRE(kitOut != nullptr);
    REQUIRE(kitOut->kits.size() == 1);
    const auto& s0 = kitOut->kits.front();
    CHECK(s0.compiledBytes.empty());
    CHECK(s0.compiledHash == 0);
    REQUIRE(s0.samples.size() == 1);
    CHECK(s0.samples[0].path   == "/some/path/kick.wav");
    CHECK(s0.samples[0].offset == 128);
    CHECK(s0.samples[0].length == 4096);
    REQUIRE(s0.samples[0].effects.size() == 2);
    const auto* gain = rfl::get_if<rp::lsdj::GainEffect>(&s0.samples[0].effects[0].variant());
    REQUIRE(gain != nullptr);
    CHECK(gain->normalize == true);
}

TEST_CASE("path-only JSON is thin and round-trips idempotently",
          "[ProjectSerialization][json]") {
    // A 1 MB ROM + battery RAM must NOT bloat the JSON (binaries are dropped),
    // and re-serializing a loaded path-only project must reproduce byte-identical
    // JSON (no lossy / non-deterministic fields).
    SameBoyConfig a;
    a.romPath   = "/roms/a.gb";
    a.romBytes  = std::vector<std::uint8_t>(1024 * 1024, 0xCD); // 1 MB
    a.sram      = std::vector<std::uint8_t>(0x20000, 0x11);
    a.savestate = std::vector<std::uint8_t>(0x4000, 0x22);
    a.gainDb    = -3.5f;
    a.linkGroupId = 2;

    SameBoyConfig b;
    b.romPath     = "/roms/b.gb";
    b.linkGroupId = 2;

    ProjectConfig cfg;
    cfg.settings.layout       = SystemLayout::Grid;
    cfg.settings.midiRouting  = MidiRouting::FourChannelsPerInstance;
    cfg.settings.audioRouting = AudioRouting::TwoPerInstance;
    cfg.settings.zoom         = 3;
    cfg.systems.push_back(a);
    cfg.systems.push_back(b);

    const std::string json = projectConfigToJsonFile(cfg);
    // Thin: ~2 MB of binaries collapse to well under 4 KB of metadata.
    INFO("json size: " << json.size());
    CHECK(json.size() < 4096);

    // Idempotent: load the thin JSON, re-serialize, and the text matches.
    auto parsed = projectConfigFromBytes(
        std::vector<std::uint8_t>(json.begin(), json.end()));
    REQUIRE(parsed.has_value());
    const std::string json2 = projectConfigToJsonFile(*parsed);
    CHECK(json == json2);

    // Settings + per-system fields survived the round-trip.
    CHECK(parsed->settings.layout == SystemLayout::Grid);
    CHECK(parsed->settings.zoom   == 3);
    REQUIRE(parsed->systems.size() == 2);
    const auto* a2 = rfl::get_if<SameBoyConfig>(&parsed->systems[0].variant());
    REQUIRE(a2 != nullptr);
    CHECK(a2->romPath     == "/roms/a.gb");
    CHECK(a2->linkGroupId == 2);
    CHECK(a2->gainDb      == -3.5f);
    CHECK(a2->romBytes.empty());
    CHECK(a2->sram.empty());
    CHECK(a2->savestate.empty());
}

TEST_CASE("LsdjKitPatchConfig coexists with LsdjSyncConfig on the same system",
          "[ProjectSerialization][role][kit]") {
    // The sniffer attaches BOTH roles when an LSDJ ROM is loaded — they're
    // orthogonal. Verify the variant carries them through JSON intact.
    SameBoyConfig sb;
    sb.romPath = "/some/path/lsdj.gb";
    sb.roles.emplace_back(LsdjSyncConfig{LsdjSyncMode::MidiSync, 1, false});
    sb.roles.emplace_back(rp::lsdj::LsdjKitPatchConfig{});  // empty kits is valid

    ProjectConfig cfg;
    cfg.systems.push_back(sb);

    auto parsed = projectConfigFromZip(projectConfigToZip(cfg));
    REQUIRE(parsed.has_value());
    const auto* sbOut = rfl::get_if<SameBoyConfig>(&parsed->systems.front().variant());
    REQUIRE(sbOut != nullptr);
    REQUIRE(sbOut->roles.size() == 2);

    bool sawSync = false, sawKit = false;
    for (const auto& rc : sbOut->roles) {
        if (rfl::get_if<LsdjSyncConfig>(&rc.variant()))                    sawSync = true;
        if (rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant()))      sawKit  = true;
    }
    REQUIRE(sawSync);
    REQUIRE(sawKit);
}

// --- Missing-file detection / relink (ProjectMissingFiles.hpp) --------------

namespace {
std::string touchTemp(const std::string& name) {
    const auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream(p, std::ios::binary) << 'x';
    return p.string();
}
const rp::MissingFile* findByKind(const std::vector<rp::MissingFile>& v, const char* kind) {
    for (const auto& m : v) if (m.itemKind == kind) return &m;
    return nullptr;
}
} // namespace

TEST_CASE("scanMissingFiles flags only absent, needed ROMs", "[MissingFiles]") {
    const std::string realRom = touchTemp("rp_mf_rom.gb");

    SameBoyConfig embedded; embedded.romPath = "/nope/x.gb"; embedded.romBytes = {1, 2, 3};
    SameBoyConfig present;  present.romPath  = realRom;       // path-only, exists
    SameBoyConfig absent;   absent.romPath   = "/nope/c.gb";  // path-only, gone

    ProjectConfig cfg;
    cfg.systems.push_back(embedded);
    cfg.systems.push_back(present);
    cfg.systems.push_back(absent);

    const auto missing = rp::scanMissingFiles(cfg);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].systemIndex == 2);
    CHECK(missing[0].itemKind == "rom");
    CHECK(missing[0].path == "/nope/c.gb");

    std::filesystem::remove(realRom);
}

TEST_CASE("scanMissingFiles checks kit samples only when recompiling", "[MissingFiles]") {
    auto makeCfg = [](bool withCompiledBytes) {
        SameBoyConfig sb; sb.romBytes = {1}; // ROM present, not under test
        rp::lsdj::LsdjKitConfig k; k.slot = 3;
        if (withCompiledBytes) k.compiledBytes = std::vector<std::uint8_t>(0x4000, 0);
        rp::lsdj::LsdjSampleConfig s; s.path = "/nope/kick.wav";
        k.samples.push_back(s);
        rp::lsdj::LsdjKitPatchConfig kit; kit.kits.push_back(k);
        sb.roles.emplace_back(kit);
        ProjectConfig cfg; cfg.systems.push_back(sb);
        return cfg;
    };

    // JSON load (no compiled bytes) → the missing WAV is flagged.
    const auto needs = rp::scanMissingFiles(makeCfg(false));
    REQUIRE(needs.size() == 1);
    CHECK(needs[0].itemKind == "sample");
    CHECK(needs[0].kitSlot == 3);
    CHECK(needs[0].sampleIndex == 0);

    // Zip kit (compiled bytes present) → self-sufficient, WAV not required.
    CHECK(rp::scanMissingFiles(makeCfg(true)).empty());
}

TEST_CASE("relinkInConfig + autoFindSiblings repair a moved folder", "[MissingFiles]") {
    const auto dir = std::filesystem::temp_directory_path() / "rp_mf_relink";
    std::filesystem::create_directories(dir);
    const auto romNew = (dir / "song.gb").string();
    const auto wavNew = (dir / "kick.wav").string();
    std::ofstream(romNew, std::ios::binary) << 'r';
    std::ofstream(wavNew, std::ios::binary) << 'w';

    SameBoyConfig sb; sb.romPath = "/old/song.gb"; // both old paths gone
    rp::lsdj::LsdjKitConfig k; k.slot = 0;
    rp::lsdj::LsdjSampleConfig s; s.path = "/old/kick.wav";
    k.samples.push_back(s);
    rp::lsdj::LsdjKitPatchConfig kit; kit.kits.push_back(k);
    sb.roles.emplace_back(kit);
    ProjectConfig cfg; cfg.systems.push_back(sb);

    auto missing = rp::scanMissingFiles(cfg);
    REQUIRE(missing.size() == 2); // rom + sample

    // Locate the ROM; auto-find resolves the sibling WAV from the same folder.
    const rp::MissingFile* romItem = findByKind(missing, "rom");
    REQUIRE(romItem != nullptr);
    REQUIRE(rp::relinkInConfig(cfg, *romItem, romNew));
    CHECK(rp::autoFindSiblings(cfg, dir.string()) == 1);
    CHECK(rp::scanMissingFiles(cfg).empty());

    const auto* sbOut = rfl::get_if<SameBoyConfig>(&cfg.systems[0].variant());
    REQUIRE(sbOut != nullptr);
    CHECK(sbOut->romPath == romNew);
    CHECK(sbOut->romBytes.empty());
    const auto* kc = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&sbOut->roles[0].variant());
    REQUIRE(kc != nullptr);
    CHECK(kc->kits[0].samples[0].path == wavNew);

    std::filesystem::remove_all(dir);
}
