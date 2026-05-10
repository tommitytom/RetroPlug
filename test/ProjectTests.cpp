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
#include "system/sameboy/SameBoyConfig.hpp"

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
    SystemConfig snapshotConfig() const override { return SameBoyConfig{}; }

private:
    std::atomic<int>* aliveCounter_;
};

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
    sb.model    = GameboyModel::DmgB;
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
    CHECK(roundtripped->model    == GameboyModel::DmgB);
    CHECK(roundtripped->fastBoot == false);
    CHECK(roundtripped->embedRom == true);
    CHECK(roundtripped->romPath  == "/path/to/lsdj.gb");
    CHECK(roundtripped->romBytes == sb.romBytes);
    CHECK(roundtripped->savestate == sb.savestate);
}

TEST_CASE("projectConfigFromJson reports failure for malformed JSON", "[ProjectSerialization]") {
    REQUIRE_FALSE(projectConfigFromJson("{ this is not json").has_value());
    REQUIRE_FALSE(projectConfigFromJson("").has_value());
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
