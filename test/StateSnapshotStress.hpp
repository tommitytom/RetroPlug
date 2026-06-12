#pragma once

// Backend-agnostic concurrency stress for the per-system state-snapshot path.
//
// Runs two threads against a single live emulator, mirroring the plugin's
// thread split:
//   * Writer ("audio"): drains a CommandQueue (applying LoadState / LoadSram
//     exactly as PluginDSP does) and steps onProcess(), which publishes the
//     whole-savestate snapshot into its triple-buffer.
//   * Reader ("UI"): pulls snapshots via readStateSnapshot(), sanity-checks the
//     region table, and occasionally pushes LoadState / LoadSram commands.
//
// The only sanctioned cross-thread channels (the snapshot triple-buffer and the
// SPSC CommandQueue) are exercised under real, allocating, variable-size
// capture. The live emulator's gb_/emu_ is only ever touched by the writer
// thread — the reader never reaches into it — which is exactly the invariant
// the snapshot design exists to uphold.
//
// Heavy "is this a valid savestate?" validation runs POST-JOIN on the main
// thread (load each sampled snapshot into a CALLER-OWNED scratch system via the
// `validate` callback), so two emulator instances are never touched
// concurrently — important for Mesen's process-global state. Catch2 REQUIRE is
// likewise only used after both threads join.
//
// Included header-only by retroplug-sameboy-tests and retroplug-mesen-tests.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/MemoryType.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "transport/CommandApply.hpp"
#include "transport/CommandQueue.hpp"

namespace rp::test {

// `live` must already be onActivate'd and have enableStateSnapshot() == true.
// `validate` loads a captured savestate into a caller-owned scratch system and
// returns whether the load succeeded; it is only ever called after join.
inline void runStateSnapshotStress(
        SystemBase& live,
        SystemId liveId,
        CommandQueue& commands,
        double sampleRate,
        const std::function<bool(const std::vector<std::uint8_t>&)>& validate) {

    // Stress knobs. The writer runs a fixed block budget (guarantees many
    // publishes + real emulator work); the reader hammers reads until the
    // writer signals done. A largish block crosses the ~0.5 s publish interval
    // every few iterations, maximizing read-vs-publish overlap.
    constexpr std::uint32_t kBlockSize = 2048;
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    // Under TSan/ASan emulation is ~10x slower and a race is found from a
    // handful of overlapping accesses, not millions — keep the run short.
    constexpr int           kBlocks    = 150;
#else
    constexpr int           kBlocks    = 2000;
#endif
    constexpr std::size_t   kMaxSamples = 32;

    std::atomic<bool>          stop{false};
    std::atomic<std::uint64_t> goodReads{0};
    std::atomic<std::uint64_t> boundsFails{0};
    std::atomic<std::uint64_t> commandsApplied{0};

    // Written only by the reader during the run, read only by main after join
    // (join is a happens-before edge), so no synchronization is needed.
    std::vector<std::vector<std::uint8_t>> samples;

    std::thread writer([&] {
        std::vector<float> l(kBlockSize), r(kBlockSize);
        float* outs[2] = { l.data(), r.data() };
        AudioBlockInfo info{};
        info.frames           = kBlockSize;
        info.sampleRate       = sampleRate;
        info.tempo            = 120.0;
        info.ppqPosBlockStart = 0.0;
        info.transportPlaying = false;

        for (int b = 0; b < kBlocks; ++b) {
            // Drain the command queue through the SAME handler the DSP run loop
            // uses (transport/CommandApply.hpp) — no divergent copy.
            Command cmd;
            while (commands.tryPop(cmd)) {
                bool mutated = false;
                applySystemCommand(&live, cmd, mutated);
                commandsApplied.fetch_add(1, std::memory_order_relaxed);
            }

            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            live.onProcess(info, outs);  // publishes the snapshot internally
        }
        stop.store(true, std::memory_order_release);
    });

    std::thread reader([&] {
        std::vector<std::uint8_t> buf;
        for (std::uint64_t it = 0; !stop.load(std::memory_order_acquire); ++it) {
            if (!live.readStateSnapshot(buf)) continue;
            goodReads.fetch_add(1, std::memory_order_relaxed);

            // The region table must point inside the snapshot for every region
            // the backend reports (validates the offset math is in range).
            const auto& regions = live.stateRegions();
            for (const auto& rg : regions) {
                if (rg.size > 0 &&
                    static_cast<std::size_t>(rg.offset) + rg.size > buf.size()) {
                    boundsFails.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // Stash a bounded set of full snapshots for post-join validation.
            if ((it % 1000) == 0 && samples.size() < kMaxSamples)
                samples.push_back(buf);

            // Occasionally push load commands so the write path runs
            // concurrently with capture + read. Ownership transfers to the
            // writer (which frees it); free here if the queue is full.
            if ((it % 4000) == 1) {
                auto* v = new std::vector<std::uint8_t>(buf);
                if (!commands.tryPush(Command::makeLoadState(liveId, v))) delete v;
            } else if ((it % 4000) == 2001) {
                const auto& sram = regions[static_cast<std::size_t>(rp::MemoryType::Sram)];
                if (sram.size > 0 &&
                    static_cast<std::size_t>(sram.offset) + sram.size <= buf.size()) {
                    auto* v = new std::vector<std::uint8_t>(
                        buf.begin() + sram.offset,
                        buf.begin() + sram.offset + sram.size);
                    if (!commands.tryPush(Command::makeLoadSram(liveId, v))) delete v;
                }
            }
        }
    });

    reader.join();
    writer.join();

    // Free any commands the writer didn't get to (no threads running now).
    Command leftover;
    while (commands.tryPop(leftover)) {
        if (leftover.kind == Command::Kind::LoadState) delete leftover.payload.loadState.bytes;
        else if (leftover.kind == Command::Kind::LoadSram) delete leftover.payload.loadSram.bytes;
    }

    REQUIRE(goodReads.load() > 0);          // reader made progress (liveness)
    REQUIRE(boundsFails.load() == 0);       // region offsets always in range
    REQUIRE(!samples.empty());
    for (const auto& snap : samples)
        CHECK(validate(snap));              // every snapshot is a loadable savestate
}

} // namespace rp::test
