#pragma once

#include <memory>
#include <vector>

#include "project/ProjectConfig.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"

// DSP-thread runtime container. Holds:
//  - the polymorphic emulator instances
//  - the authoritative ProjectConfig (the one persisted via DPF state)
//
// Lifetime: one Project per Plugin instance. Build/teardown happens on the DSP
// thread; the UI thread reads through getPluginInstancePointer() and never
// mutates. All mutations come through the command queue (Step 3+).
class Project {
public:
    Project() = default;

    // Append a fresh system built from the variant alternative in `config`.
    // Returns the new SystemId, or 0 on failure. Caller (e.g. setState) is
    // responsible for sequencing onActivate after the host hands us a sample
    // rate.
    SystemId addSystem(const SystemConfig& config);

    void removeSystem(SystemId id);

    // Inline so the UI binary can locate a system via the shared Project*
    // pointer without linking the full Project.cpp (which depends on
    // SameBoySystem and is DSP-side only).
    SystemBase* findSystem(SystemId id) {
        for (auto& s : systems_) {
            if (s && s->id() == id) return s.get();
        }
        return nullptr;
    }

    void onActivate(double sampleRate);
    void onDeactivate();
    void onSampleRateChanged(double sampleRate);

    const std::vector<std::unique_ptr<SystemBase>>& systems() const { return systems_; }
    std::vector<std::unique_ptr<SystemBase>>&       systems()       { return systems_; }

    const ProjectConfig& config() const { return config_; }
    ProjectConfig&       config()       { return config_; }

    // Walk runtime systems, build a fresh ProjectConfig from their snapshots.
    // Used by Plugin::getState.
    ProjectConfig snapshotConfig() const;

private:
    SystemId nextId_ = 1;
    std::vector<std::unique_ptr<SystemBase>> systems_;
    ProjectConfig config_;
};
