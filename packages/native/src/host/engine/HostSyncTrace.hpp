#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

// Per-block trace of what the HOST reports and what the DSP kernel emits to a core's byte device.
// Opt-in via an env var, inert otherwise:
//
//   RETROPLUG_SYNC_TRACE=/tmp/risa-sync.log
//
// Built for diagnosing tracker host-sync problems that only appear under a real DAW (a stop/restart
// landing off-grid in Renoise, say) and can't be reproduced with the test harness's clean transport.
// It records BOTH halves of the seam, because either can be the culprit: the host's transport/ppq
// (does the DAW rewind? does ppq jump? does the transport flag drop at all?) and the bytes the
// risa-sync role derived from it (is the arm packet where we think, is the clock stream continuous?).
//
// Written INCREMENTALLY by a background thread, not at exit. A DAW is under no obligation to unwind
// cleanly - it can _exit, be killed, or unload a sandboxed plugin host - and a trace that only lands
// at teardown is a trace you don't get on exactly the runs you care about. The cost is that the file
// lags real time by up to kFlushMs.
//
// Audio-thread rules: fixed-size POD records into a pre-allocated ring, published with one atomic
// increment. No allocation, no locks, no file I/O in the callback.
class HostSyncTrace {
public:
    HostSyncTrace() {
        const char* path = std::getenv("RETROPLUG_SYNC_TRACE");
        if (path == nullptr || *path == '\0') return;
        path_ = path;
        // A second traced instance (a multi-instance DAW project) gets its own file rather than
        // silently clobbering the first one's.
        const unsigned n = instances()++;
        if (n > 0) path_ += "." + std::to_string(n + 1);

        file_ = std::fopen(path_.c_str(), "w");
        if (file_ == nullptr) {
            // Loud: a diagnostic that silently does nothing is worse than none at all.
            std::fprintf(stderr, "[sync-trace] FAILED to open %s - tracing disabled\n", path_.c_str());
            path_.clear();
            return;
        }
        records_.resize(kRingSize);
        writeHeader();
        std::fprintf(stderr, "[sync-trace] recording to %s (flushing every %d ms)\n", path_.c_str(), kFlushMs);
        std::fflush(stderr);
        writer_ = std::thread([this] { writerLoop(); });
    }

    ~HostSyncTrace() {
        if (path_.empty()) return;
        stop_.store(true, std::memory_order_release);
        if (writer_.joinable()) writer_.join();
        drain();  // whatever the last interval didn't catch
        if (file_ != nullptr) {
            std::fprintf(stderr, "[sync-trace] wrote %llu blocks to %s\n",
                         static_cast<unsigned long long>(written_), path_.c_str());
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    bool enabled() const { return !path_.empty(); }

    // Audio thread: start a block. Returns false when tracing is off, so the caller can skip the
    // per-byte work entirely.
    bool beginBlock(std::uint32_t frames, double sampleRate, double tempo, double ppqStart, bool transport) {
        if (path_.empty()) return false;
        Record& r = records_[committed_.load(std::memory_order_relaxed) % kRingSize];
        r.frames     = frames;
        r.sampleRate = sampleRate;
        r.tempo      = tempo;
        r.ppqStart   = ppqStart;
        r.transport  = transport;
        r.byteCount  = 0;
        r.truncated  = false;
        return true;
    }

    // Audio thread: record one byte the kernel pushed to a core this block, with its intra-block frame.
    void byte(std::uint32_t frame, std::uint8_t value) {
        if (path_.empty()) return;
        Record& r = records_[committed_.load(std::memory_order_relaxed) % kRingSize];
        if (r.byteCount >= kMaxBytesPerBlock) { r.truncated = true; return; }
        r.frames_of[r.byteCount] = frame;
        r.bytes[r.byteCount] = value;
        r.byteCount++;
    }

    // Audio thread: publish the block. The release pairs with the writer's acquire, so it only ever
    // reads records the audio thread has finished filling.
    void endBlock() {
        if (path_.empty()) return;
        committed_.fetch_add(1, std::memory_order_release);
    }

private:
    // An arm (5) + start (1) + a beat of clocks (24) is 30; 48 leaves headroom for a long block.
    static constexpr std::uint32_t kMaxBytesPerBlock = 48;
    // ~95 s of ring at 512 frames / 44.1 kHz, drained every 200 ms - orders of magnitude of slack.
    static constexpr std::uint64_t kRingSize = 8192;
    static constexpr int           kFlushMs  = 200;

    struct Record {
        std::uint32_t frames = 0;
        double        sampleRate = 0.0;
        double        tempo = 0.0;
        double        ppqStart = 0.0;
        bool          transport = false;
        bool          truncated = false;
        std::uint32_t byteCount = 0;
        std::uint32_t frames_of[kMaxBytesPerBlock] = {};
        std::uint8_t  bytes[kMaxBytesPerBlock] = {};
    };

    void writeHeader() {
        std::fprintf(file_, "# RetroPlug host-sync trace. One line per audio block.\n");
        std::fprintf(file_, "#\n");
        std::fprintf(file_, "#   block frames rate tempo ppqStart transport | frame:BYTES ...\n");
        std::fprintf(file_, "#\n");
        std::fprintf(file_, "# 'transport' is the host's play flag; ppqStart is its playhead at the block start.\n");
        std::fprintf(file_, "# risa bytes: F9 52 ss cc tt = arm+locate, FA = start, F8 = clock, FC = stop.\n");
        std::fprintf(file_, "# Markers: <<START / <<STOP on a transport edge, <<PPQ-JUMP when the playhead\n");
        std::fprintf(file_, "# didn't continue from where the previous block should have left it (a locate).\n");
        std::fflush(file_);
    }

    void writerLoop() {
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kFlushMs));
            drain();
        }
    }

    // Writer thread (and once more at teardown): append every block published since the last pass.
    void drain() {
        if (file_ == nullptr) return;
        const std::uint64_t upto = committed_.load(std::memory_order_acquire);
        if (upto == written_) return;
        // The audio thread laps the writer only if the ring is too small; say so rather than emit a
        // silently mangled trace.
        if (upto - written_ > kRingSize) {
            std::fprintf(file_, "# ... DROPPED %llu blocks (ring overrun)\n",
                         static_cast<unsigned long long>(upto - written_ - kRingSize));
            written_ = upto - kRingSize;
        }
        for (; written_ < upto; ++written_) {
            const Record& r = records_[written_ % kRingSize];
            const double expected = prevPpq_ + (prevTransport_ && r.sampleRate > 0.0
                ? (static_cast<double>(prevFrames_) / r.sampleRate) * (prevTempo_ / 60.0) : 0.0);
            const char* mark = "";
            if (!first_ && r.transport != prevTransport_) mark = r.transport ? " <<START" : " <<STOP";
            else if (!first_ && r.transport && std::fabs(r.ppqStart - expected) > 1e-3) mark = " <<PPQ-JUMP";

            std::fprintf(file_, "%6llu %5u %8.1f %7.3f %12.6f %d%s |",
                         static_cast<unsigned long long>(written_), r.frames, r.sampleRate, r.tempo,
                         r.ppqStart, r.transport ? 1 : 0, mark);
            for (std::uint32_t b = 0; b < r.byteCount; ++b) {
                if (b == 0 || r.frames_of[b] != r.frames_of[b - 1]) std::fprintf(file_, " %u:", r.frames_of[b]);
                std::fprintf(file_, "%02X", r.bytes[b]);
            }
            if (r.truncated) std::fprintf(file_, " ...(truncated)");
            std::fprintf(file_, "\n");
            prevPpq_ = r.ppqStart;
            prevTransport_ = r.transport;
            prevFrames_ = r.frames;
            prevTempo_ = r.tempo;
            first_ = false;
        }
        std::fflush(file_);  // so the file is readable while the DAW is still running
    }

    static unsigned& instances() { static unsigned n = 0; return n; }

    std::string           path_;
    std::FILE*            file_ = nullptr;
    std::vector<Record>   records_;
    std::atomic<std::uint64_t> committed_{0};   // audio thread publishes
    std::atomic<bool>     stop_{false};
    std::thread           writer_;

    // Writer-thread-only state.
    std::uint64_t written_ = 0;
    double        prevPpq_ = 0.0, prevTempo_ = 120.0;
    std::uint32_t prevFrames_ = 0;
    bool          prevTransport_ = false, first_ = true;
};
