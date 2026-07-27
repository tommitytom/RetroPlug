#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
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
// Audio-thread rules: a Record is POD with an inline byte array, and the buffer is reserved up front,
// so recording allocates nothing and does no file I/O in the callback. The file is written once, at
// teardown. Recording stops at the cap rather than growing.
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
        records_.resize(kMaxRecords);  // allocate + touch once, off the audio thread

        // The destructor alone isn't enough: the test host leaves via tjs.exit, and a DAW can tear
        // down without unwinding. Register an atexit hook once; the slot is cleared on destruction so
        // the hook can't touch a dead object.
        if (activeSlot() == nullptr) {
            activeSlot() = this;
            static bool hooked = false;
            if (!hooked) {
                hooked = true;
                std::atexit([] { if (activeSlot() != nullptr) activeSlot()->dump(); });
            }
        }
    }

    ~HostSyncTrace() {
        dump();
        if (activeSlot() == this) activeSlot() = nullptr;
    }

    bool enabled() const { return !path_.empty(); }

    // Audio thread: start a block. Returns false when tracing is off or the cap is reached, so the
    // caller can skip gathering bytes entirely.
    bool beginBlock(std::uint32_t frames, double sampleRate, double tempo, double ppqStart, bool transport) {
        if (path_.empty() || used_ >= kMaxRecords) return false;
        Record& r = records_[used_];
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
        if (path_.empty() || used_ >= kMaxRecords) return;
        Record& r = records_[used_];
        if (r.byteCount >= kMaxBytesPerBlock) { r.truncated = true; return; }
        r.frames_of[r.byteCount] = frame;
        r.bytes[r.byteCount] = value;
        r.byteCount++;
    }

    // Audio thread: commit the block.
    void endBlock() {
        if (path_.empty() || used_ >= kMaxRecords) return;
        used_++;
    }

    // Control thread, at teardown: write the trace out. Safe to call twice (the second is a no-op).
    void dump() {
        if (path_.empty() || used_ == 0) return;
        std::FILE* f = std::fopen(path_.c_str(), "w");
        if (f == nullptr) {
            std::fprintf(stderr, "[sync-trace] could not open %s\n", path_.c_str());
            path_.clear();
            return;
        }
        std::fprintf(f, "# RetroPlug host-sync trace. One line per audio block.\n");
        std::fprintf(f, "#\n");
        std::fprintf(f, "#   block frames rate tempo ppqStart transport | frame:BYTES ...\n");
        std::fprintf(f, "#\n");
        std::fprintf(f, "# 'transport' is the host's play flag; ppqStart is its playhead at the block start.\n");
        std::fprintf(f, "# risa bytes: F9 52 ss cc tt = arm+locate, FA = start, F8 = clock, FC = stop.\n");
        std::fprintf(f, "# Markers: <<START / <<STOP on a transport edge, <<PPQ-JUMP when the playhead\n");
        std::fprintf(f, "# didn't continue from where the previous block should have left it (a locate).\n");
        double prevPpq = 0.0, prevTempo = 120.0;
        std::uint32_t prevFrames = 0;
        bool prevTransport = false, first = true;
        for (std::size_t i = 0; i < used_; ++i) {
            const Record& r = records_[i];
            const double expected = prevPpq + (prevTransport && r.sampleRate > 0.0
                ? (static_cast<double>(prevFrames) / r.sampleRate) * (prevTempo / 60.0) : 0.0);
            const char* mark = "";
            if (!first && r.transport != prevTransport) mark = r.transport ? " <<START" : " <<STOP";
            else if (!first && r.transport && std::fabs(r.ppqStart - expected) > 1e-3) mark = " <<PPQ-JUMP";

            std::fprintf(f, "%6zu %5u %8.1f %7.3f %12.6f %d%s |", i, r.frames, r.sampleRate, r.tempo,
                         r.ppqStart, r.transport ? 1 : 0, mark);
            for (std::uint32_t b = 0; b < r.byteCount; ++b) {
                if (b == 0 || r.frames_of[b] != r.frames_of[b - 1]) std::fprintf(f, " %u:", r.frames_of[b]);
                std::fprintf(f, "%02X", r.bytes[b]);
            }
            if (r.truncated) std::fprintf(f, " ...(truncated)");
            std::fprintf(f, "\n");
            prevPpq = r.ppqStart;
            prevTransport = r.transport;
            prevFrames = r.frames;
            prevTempo = r.tempo;
            first = false;
        }
        std::fclose(f);
        std::fprintf(stderr, "[sync-trace] wrote %zu blocks to %s\n", used_, path_.c_str());
        path_.clear();  // written once
    }

private:
    // An arm (5) + start (1) + a beat of clocks (24) is 30; 64 leaves room for a big block.
    static constexpr std::uint32_t kMaxBytesPerBlock = 64;
    // ~40 minutes at 512 frames / 44.1 kHz. Generous, and bounded.
    static constexpr std::size_t kMaxRecords = 200000;

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

    // The instance the atexit hook dumps, cleared when that instance dies. Function-local statics so
    // the header stays self-contained.
    static HostSyncTrace*& activeSlot() { static HostSyncTrace* p = nullptr; return p; }
    static unsigned&       instances()  { static unsigned n = 0; return n; }

    std::string         path_;
    std::vector<Record> records_;
    std::size_t         used_ = 0;
};
