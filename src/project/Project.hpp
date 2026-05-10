#pragma once

#include <memory>
#include <vector>

#include "project/ProjectConfig.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/LinkGroup.hpp"

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

    // Removes the system identified by `id`. Returns the displaced raw
    // pointer (caller-owned, dispose off the audio thread) or nullptr if
    // not found. The internal `config_.systems` mirror is also updated.
    SystemBase* removeSystemAndRelease(SystemId id);

    // Pre-Step-5 convenience that destroys the system in-place. Kept for
    // the existing callers in tests and Project's own setState path; not
    // used on the audio thread.
    void removeSystem(SystemId id);

    // Wipe all systems (after onDeactivate). Used by setState before
    // rebuilding from a saved project.
    void clearSystems();

    // Realtime-safe swap. Caller passes a fully-built SystemBase* (already
    // onActivate'd on a non-realtime thread). The DSP slot identified by
    // `id` is updated to own the new pointer; the old pointer is RETURNED
    // for the caller to dispose off the audio thread (typically via
    // EventQueue → uiIdle).
    //
    // Internally this is just a std::unique_ptr::release() + reset() pair —
    // no allocation, no free. If `id` is not found, returns the input pointer
    // unchanged so the caller can route it back as well.
    SystemBase* swapSystem(SystemId id, SystemBase* newSystem) {
        for (auto& s : systems_) {
            if (s && s->id() == id) {
                SystemBase* old = s.release();
                s.reset(newSystem);
                return old;
            }
        }
        return newSystem;
    }

    // Realtime-safe append. Caller passes a fully-built SystemBase*. Returns
    // the assigned id (zero on failure — currently never fails, but reserved
    // for future capacity limits). No allocation in this call itself; the
    // vector may grow if it lacks capacity, so reserve in advance for
    // strict realtime use.
    SystemId adoptSystem(SystemBase* newSystem) {
        if (!newSystem) return 0;
        systems_.emplace_back(newSystem);
        return newSystem->id();
    }

    // Reserve capacity in `systems_` so adoptSystem doesn't allocate.
    // Called once at construction; bumped if multi-instance grows beyond the
    // initial reserve.
    void reserve(std::size_t n) { systems_.reserve(n); }

    // Allocate a fresh SystemId. Used by UI-thread construction so the
    // SameBoySystem can be built with a stable id before being shipped to
    // the DSP. Increments the internal counter — must be called from a
    // single thread (in practice, the UI thread for new systems built off
    // the audio thread, or DSP for legacy bootstrap).
    SystemId nextSystemId() { return nextId_++; }

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

    // Audio-thread per-block dispatcher. Calls per-system onProcess on
    // unlinked systems and per-group onProcess on each LinkGroup. Replaces
    // the bare loop that lived in PluginDSP::run.
    void onProcess(const AudioBlockInfo& info, float* const* outs);

    // Rebuild `linkGroups_` from current systems' linkGroupId. Updates each
    // SameBoySystem's linkPeers_ cache. Single member groups are dissolved
    // (linkPeers_ left empty so the standalone path runs). Call after any
    // mutation that changes membership: addSystem, removeSystem, swapSystem,
    // and after setState repopulates the project. Realtime-safe under the
    // pre-reserved capacities of the relevant vectors.
    void rebuildLinkGroups();

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
    std::vector<LinkGroup>                   linkGroups_;
    ProjectConfig                            config_;
};
