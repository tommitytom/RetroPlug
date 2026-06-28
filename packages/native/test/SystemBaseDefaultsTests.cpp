// Tests for the default implementations of SystemBase's "menu action"
// virtuals — the set added in the Mesen/GBA parity lift. A backend that
// doesn't override these should still be reachable from the menu without
// crashing; all the no-op defaults must:
//   - return sensible "feature not supported" values (nullopt / false / {})
//   - tolerate setter calls as no-ops
//
// Lives in retroplug-tests (no SameBoy or Mesen linkage); uses a minimal
// FakeSystem identical in spirit to the one in ProjectTests.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/SystemConfig.hpp"
#include "system/SystemTypes.hpp"
#include "system/mesen/MesenNesConfig.hpp"

namespace {

// Pure-default backend: implements only the pure-virtual SystemBase surface
// (kind, onActivate, onSampleRateChanged, onProcess, snapshotConfig), leaves
// every other virtual at its default. Drives the contract assertions below.
class DefaultsOnlySystem final : public SystemBase {
public:
    using SystemBase::SystemBase;
    SystemKind kind() const override { return SystemKind::MesenNes; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    void onProcess(const AudioBlockInfo&, float* const*) override {}
    SystemConfig snapshotConfig() const override { return MesenNesConfig{}; }
};

// Reports a small snapshot size but captures MORE than that, to exercise the
// "capture exceeds the slot — skip, never realloc" guard in publishStateSnapshot.
class OversizedSnapshotSystem final : public SystemBase {
public:
    using SystemBase::SystemBase;
    SystemKind kind() const override { return SystemKind::MesenNes; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    void onProcess(const AudioBlockInfo&, float* const*) override {}
    SystemConfig snapshotConfig() const override { return MesenNesConfig{}; }
protected:
    std::size_t stateSnapshotSize() const override { return 16; }
    bool captureStateSnapshot(std::vector<std::uint8_t>& dst) override {
        dst.assign(1000, 0xAB);   // far larger than the 16-byte slot
        return true;
    }
};

// Records the fused-onProcess dispatch order. Overrides ONLY the triad (leaves
// onProcess at the base default) so we pin that the base entry calls
// prepareForBlock once, stepIfBelowTarget until it returns false, then
// finishBlock once — the contract every real backend now relies on.
class TriadRecordingSystem final : public SystemBase {
public:
    using SystemBase::SystemBase;
    SystemKind kind() const override { return SystemKind::MesenNes; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    SystemConfig snapshotConfig() const override { return MesenNesConfig{}; }

    void prepareForBlock(const AudioBlockInfo&) override { calls.push_back('p'); }
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override {
        calls.push_back('s');
        lastFramesNeeded = framesNeeded;
        return ++steps < stepsBeforeDone;   // true until the Nth call
    }
    void finishBlock(const AudioBlockInfo&, float* const*) override { calls.push_back('f'); }

    int           stepsBeforeDone = 3;
    int           steps = 0;
    std::uint32_t lastFramesNeeded = 0;
    std::string   calls;   // sequence of 'p' / 's' / 'f'
};

} // namespace

TEST_CASE("SystemBase::onProcess fuses the triad: prepare, step-to-done, finish",
          "[SystemBase][defaults][triad]") {
    TriadRecordingSystem sys{1};
    sys.stepsBeforeDone = 3;   // step returns true twice, then false on the 3rd

    float l[8] = {}, r[8] = {};
    float* outs[2] = { l, r };
    const AudioBlockInfo info{ /*frames*/ 8, /*sampleRate*/ 44100.0,
                               /*tempo*/ 120.0, /*ppq*/ 0.0, /*playing*/ false };

    sys.onProcess(info, outs);

    // prepare once, three steps (true, true, false), finish once — in order.
    CHECK(sys.calls == "psssf");
    CHECK(sys.lastFramesNeeded == 8);   // the runner passes info.frames as the target
}

TEST_CASE("SystemBase::isLinked defaults to false", "[SystemBase][defaults][triad]") {
    TriadRecordingSystem sys{1};
    CHECK_FALSE(sys.isLinked());
}

TEST_CASE("SystemBase default fastBoot returns nullopt", "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    CHECK_FALSE(sys.fastBoot().has_value());
    // Setter must not crash; reader stays at nullopt.
    sys.setFastBoot(true);
    CHECK_FALSE(sys.fastBoot().has_value());
    sys.setFastBoot(false);
    CHECK_FALSE(sys.fastBoot().has_value());
}

TEST_CASE("SystemBase default wantsRomReload is false and setter is no-op",
          "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    CHECK_FALSE(sys.wantsRomReload());
    sys.setRomReload(true);
    CHECK_FALSE(sys.wantsRomReload());
}

TEST_CASE("SystemBase default romPath is empty", "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    CHECK(sys.romPath().empty());
}

TEST_CASE("SystemBase default SRAM API returns empty / no-ops",
          "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    CHECK(sys.saveSramBytes().empty());
    sys.clearSram();  // must not throw / crash
    CHECK(sys.saveSramBytes().empty());
}

TEST_CASE("SystemBase default savestate API returns empty / rejects loads",
          "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    CHECK(sys.saveStateBytes().empty());
    CHECK_FALSE(sys.loadStateBytes({}));
    CHECK_FALSE(sys.loadStateBytes(std::vector<std::uint8_t>{1, 2, 3, 4}));
}

TEST_CASE("SystemBase default clone returns nullptr", "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    auto cloned = sys.clone(SystemId{42}, 44100.0);
    CHECK(cloned == nullptr);
}

TEST_CASE("SystemBase default cloneFromState returns nullptr",
          "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    CHECK(sys.cloneFromState(SystemId{42}, 44100.0,
                             std::vector<std::uint8_t>{1, 2, 3, 4}) == nullptr);
}

TEST_CASE("SystemBase default state snapshot is unsupported",
          "[SystemBase][defaults]") {
    DefaultsOnlySystem sys{1};
    // stateSnapshotSize() defaults to 0, so the snapshot can't be enabled and
    // there's nothing to read.
    CHECK_FALSE(sys.enableStateSnapshot());
    std::vector<std::uint8_t> out;
    CHECK_FALSE(sys.readStateSnapshot(out));
}

TEST_CASE("SystemBase state snapshot skips a capture that exceeds the slot",
          "[SystemBase][defaults]") {
    OversizedSnapshotSystem sys{1};
    REQUIRE(sys.enableStateSnapshot());          // sized to 16 bytes (+ prefix)
    // Enable arms an immediate publish; the 1000-byte capture won't fit, so the
    // publisher must skip it (never reallocate) and leave nothing to read.
    sys.publishStateSnapshot(/*frames*/ 4096, /*sampleRate*/ 44100.0);
    std::vector<std::uint8_t> out;
    CHECK_FALSE(sys.readStateSnapshot(out));
}
