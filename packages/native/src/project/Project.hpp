#pragma once

#include <memory>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/LinkGroup.hpp"
#include "transport/MidiTypes.hpp"

// DSP-thread runtime container. Holds the polymorphic emulator instances and
// their resolved link groups. Project state is persisted via TS (DPF state
// carries an opaque blob); this class is the runtime container only.
//
// Lifetime: one Project per Plugin instance. Build/teardown happens on the DSP
// thread; the UI thread reads through getPluginInstancePointer() and never
// mutates. All mutations come through the command queue.
class Project {
public:
    Project() = default;

    // Removes the system identified by `id`. Returns the displaced raw
    // pointer (caller-owned, dispose off the audio thread) or nullptr if
    // not found.
    SystemBase* removeSystemAndRelease(SystemId id);

    // Convenience that destroys the system in-place (not used on the audio
    // thread).
    void removeSystem(SystemId id);

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
    // mutation that changes membership (removeSystem, swapSystem, adoptSystem).
    // Realtime-safe under the pre-reserved capacities of the relevant vectors.
    void rebuildLinkGroups();

    const std::vector<std::unique_ptr<SystemBase>>& systems() const { return systems_; }
    std::vector<std::unique_ptr<SystemBase>>&       systems()       { return systems_; }

    // Read access to the resolved link-group set. Used by PluginDSP's
    // multi-out audio path to drive each linked member's finishBlock against
    // its routed output channels (something LinkGroup::onProcess can't do
    // because it hard-codes outs[0]/[1] for every member).
    const std::vector<LinkGroup>& linkGroups() const { return linkGroups_; }

private:
    SystemId nextId_ = 1;
    std::vector<std::unique_ptr<SystemBase>> systems_;
    std::vector<LinkGroup>                   linkGroups_;
};
