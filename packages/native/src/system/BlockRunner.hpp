#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "project/ProjectConfig.hpp"   // AudioRouting
#include "system/SystemTypes.hpp"      // AudioBlockInfo

class Project;
class SystemBase;

// Shared "advance one audio block" runner. Pure C++ — no threads, queues, DPF,
// or JS. Every audio path (the plugin DSP run-loop, the standalone callback, the
// offline / CLI render, the headless UI pump) calls runBlock(); they differ only
// in the driver around it (the clock, any IPC) and the AudioRouter they supply.
//
// Output convention (unchanged from the per-system contract in SystemBase): the
// CALLER owns and ZEROES the output buffers; systems SUM (+=) into them. The
// runner never zeros — in Stereo routing many systems sum into one bus, so a
// per-slot zero would wipe earlier systems.

// A stereo output destination: planar L/R buffers a system sums into. `l == r`
// is legal and means mono — both channels alias one buffer (the OnePerInstance
// "sum L+R into a single channel" trick).
struct AudioBus {
    float* l = nullptr;
    float* r = nullptr;
};

// Maps a system's slot (its index in Project::systems()) to the bus it writes
// into. `streamIndex` is reserved for a future per-channel split (a system
// emitting more than one stream — e.g. the 4 Game Boy channels); it is always 0
// today and routers ignore it. Keeping it in the contract now means per-channel
// support is purely additive later.
struct AudioRouter {
    virtual ~AudioRouter() = default;
    virtual AudioBus bus(std::size_t slot, std::uint32_t streamIndex = 0) const = 0;
};

// Every system sums into one fixed stereo pair (the Stereo routing, plus every
// headless / mix path: Project::onProcess, the CLI mix, the UI pump).
struct StereoRouter final : AudioRouter {
    AudioBus dst;
    StereoRouter(float* l, float* r) : dst{l, r} {}
    AudioBus bus(std::size_t, std::uint32_t = 0) const override { return dst; }
};

// The plugin's AudioRouting policy over a flat output-channel array. `numChannels`
// is passed in so this header stays free of any DPF output-count macro.
//   Stereo:         everyone -> channels[0]/[1].
//   TwoPerInstance: slot i   -> channels[(2i)%N] / channels[(2i+1)%N].
//   OnePerInstance: slot i   -> channels[i%N] for BOTH L and R (mono sum).
struct MultiOutRouter final : AudioRouter {
    float* const* channels;
    std::size_t   numChannels;
    AudioRouting  mode;
    MultiOutRouter(float* const* ch, std::size_t n, AudioRouting m)
        : channels(ch), numChannels(n), mode(m) {}
    AudioBus bus(std::size_t slot, std::uint32_t = 0) const override {
        switch (mode) {
            case AudioRouting::OnePerInstance: {
                float* ch = channels[slot % numChannels];
                return { ch, ch };
            }
            case AudioRouting::TwoPerInstance: {
                const std::size_t p = (2 * slot) % numChannels;
                return { channels[p], channels[(p + 1) % numChannels] };
            }
            case AudioRouting::Stereo:
            default:
                return { channels[0], channels[1] };
        }
    }
};

// Each slot writes into its own L/R buffer (CLI per-system isolation / render).
struct PerSystemRouter final : AudioRouter {
    float* const* ls;   // one L buffer per slot
    float* const* rs;   // one R buffer per slot
    PerSystemRouter(float* const* l, float* const* r) : ls(l), rs(r) {}
    AudioBus bus(std::size_t slot, std::uint32_t = 0) const override {
        return { ls[slot], rs[slot] };
    }
};

// Advance ONE render unit (1..N systems stepped in lockstep) by one block,
// finishing each member into its router-provided bus. A singleton is the size-1
// case; a SameBoy link group is the size-N case (round-robin step so serial bits
// ferry mid-block). `systems` is project.systems(), used to resolve each
// member's router slot. Shared by realtime runBlock (per unit) and the offline
// parallel renderer (per worker thread) — it never owns or zeroes buffers and
// touches only the members' own state, so disjoint units render concurrently.
void runUnit(const AudioBlockInfo& info,
             SystemBase* const* members, std::size_t count,
             const std::vector<std::unique_ptr<SystemBase>>& systems,
             const AudioRouter& router);

// Advance every system in `project` by one block, routing each system's output
// through `router`. Each unlinked system is a singleton unit; each link group is
// a multi-member unit — both driven via runUnit().
void runBlock(const AudioBlockInfo& info, Project& project, const AudioRouter& router);
