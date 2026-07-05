#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/SystemConfig.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

// A minimal SystemBase for the native-greenfield host: no real emulator, just enough to
// satisfy the greenfield Backend's flat/numeric emulator seam (constructSystem / readState /
// readSram + duplicate / reload / remove). It masquerades as a SameBoy system so it fits the
// closed SystemKind / SystemConfig types with zero edits to them; the greenfield path never
// reads kind() / snapshotConfig() (TS owns project.json), so the masquerade is invisible.
//
// SRAM and savestate are opaque byte buffers the host seeds (on construct / import) and the
// pump reads back — a faithful stand-in for the DSP-published state. There is no audio: the
// greenfield Backend has no render method yet, so the block triad stays inert (silence).
class StubSystem final : public SystemBase {
public:
    StubSystem(SystemId id, std::string romPath, std::string embeddedRom, std::string savPath)
        : SystemBase(id),
          romPath_(std::move(romPath)),
          embeddedRom_(std::move(embeddedRom)),
          savPath_(std::move(savPath)) {}

    SystemKind kind() const override { return SystemKind::SameBoy; }

    void onActivate(double sampleRate) override { sampleRate_ = sampleRate; }
    void onSampleRateChanged(double sampleRate) override { sampleRate_ = sampleRate; }

    // Dead for the greenfield path (TS owns the config); only satisfies the pure virtual.
    SystemConfig snapshotConfig() const override { return SameBoyConfig{}; }

    const std::string& romPath() const override { return romPath_; }
    const std::string& savPath() const override { return savPath_; }
    void               setSavPath(const std::string& path) override { savPath_ = path; }

    // Stub-only: lets duplicate / reload carry the embedded-ROM marker forward.
    const std::string& embeddedRom() const { return embeddedRom_; }

    std::vector<std::uint8_t> saveSramBytes() const override { return sram_; }
    bool loadSramBytes(const std::vector<std::uint8_t>& bytes) override {
        sram_ = bytes;
        return true;
    }

    std::vector<std::uint8_t> saveStateBytes() const override { return state_; }
    bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override {
        state_ = bytes;
        return true;
    }

private:
    std::string               romPath_;
    std::string               embeddedRom_;
    std::string               savPath_;
    std::vector<std::uint8_t> sram_;
    std::vector<std::uint8_t> state_;
    double                    sampleRate_ = 0.0;
};
