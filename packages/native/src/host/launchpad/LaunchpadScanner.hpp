#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host/launchpad/LaunchpadLink.hpp"  // IMidiPort

namespace retroplug {

/** One message an input returned while a given output was being probed. `bytes` is opaque - see the class
 *  comment for why this file does not know what a Launchpad is. */
struct LaunchpadScanReply {
    std::string               output;  // the port the probe went out of
    std::string               input;   // the port this came back on
    std::vector<std::uint8_t> bytes;
};

// The current/last scan as the UI reads it (matches the __rp_getLaunchpadScan object in launchpadDevices.ts).
struct LaunchpadScanStatusDto {
    bool                            busy    = false;
    bool                            done    = false;  // a scan finished (with or without replies)
    std::string                     phase;            // short human label, e.g. "Probing LPProMK3 MIDI"
    std::string                     error;            // "" = ok
    std::uint32_t                   skipped = 0;      // ports that would not open (in use by another app)
    std::uint64_t                   version = 0;      // bumped on any change, so the UI re-renders only when it moves
    std::vector<LaunchpadScanReply> replies;
};

// Finds which MIDI port PAIR has a device on it, by writing an opaque probe out of each output and reporting
// what comes back on every input. Single-in-flight, run on its own thread, polled through status() - the
// N8SdWorker lifecycle, without the progress handle (a scan has no byte count to report).
//
// This class does not know what a Launchpad is, deliberately, and for the same reason LaunchpadLink does not
// know what the farewell means: TS hands down the probe bytes (a Universal Device Inquiry) and parses the
// replies (the manufacturer + family bytes that say which model answered). Native staying protocol-free is
// what keeps a second controller model a TS-only change.
//
// Why a probe at all, rather than watching for a device to announce itself: a Launchpad on TRS/DIN arrives
// through an ordinary MIDI interface, and DIN has no enumeration and no idle heartbeat - the OS sees the
// interface and nothing else. The inquiry is the only identification mechanism MIDI 1.0 has, and it is also
// the only way to learn the PAIRING, since the reply tells you which input belongs to the output you wrote
// to. Hence user-initiated: never on a timer, never at startup.
class LaunchpadScanner {
public:
    /** Open ONE port for the duration of the scan: an input when `input` (delivering to `receiver`), else an
     *  output (whose `receiver` is unused). Throws the way the port factory does when the port will not open.
     *  Injected, so this is testable with no MIDI library - RtMidiPort.cpp holds the only real one. */
    using OpenFn = std::function<std::unique_ptr<IMidiPort>(bool input, const std::string& name,
                                                            IMidiPort::Receiver receiver)>;

    /** Replies kept per scan. A port with a running sequencer on it can talk continuously, and the answer we
     *  are looking for arrives in the first few milliseconds; this only has to be deeper than the number of
     *  ports anyone owns. */
    static constexpr std::size_t kMaxReplies = 64;

    explicit LaunchpadScanner(OpenFn opener) : opener_(std::move(opener)) {}
    ~LaunchpadScanner();
    LaunchpadScanner(const LaunchpadScanner&)            = delete;
    LaunchpadScanner& operator=(const LaunchpadScanner&) = delete;

    /** Probe every output in turn, listening on every input. Returns false (does nothing) if a scan is
     *  already running. `windowMs` is how long to wait for an answer after each write. */
    bool start(std::vector<std::uint8_t> probe, std::vector<std::string> inputs, std::vector<std::string> outputs,
               unsigned windowMs);

    bool                   busy() const { return busy_.load(std::memory_order_acquire); }
    LaunchpadScanStatusDto status() const;

private:
    void bump() { version_.fetch_add(1, std::memory_order_release); }
    void run(const std::vector<std::uint8_t>& probe, const std::vector<std::string>& inputs,
             const std::vector<std::string>& outputs, unsigned windowMs);
    /** The receiver every open input shares. Runs on the MIDI backend's callback thread. */
    void onReply(const std::string& inName, const std::uint8_t* data, std::size_t n);

    OpenFn                     opener_;
    std::atomic<bool>          busy_{false};
    std::atomic<bool>          done_{false};
    std::atomic<std::uint32_t> skipped_{0};
    std::atomic<std::uint64_t> version_{0};

    // Guards everything a callback-thread reply touches as well as the short status strings. `probing_` is
    // the output currently inside its answer window: a reply is tagged with it, and an arrival with no
    // window open is DROPPED rather than attributed to whichever port happens to be next. A late answer is
    // worth losing; a reply blamed on the wrong output would record a pair whose LEDs go nowhere.
    mutable std::mutex              meta_;
    std::string                     probing_;
    std::string                     phase_;
    std::string                     error_;
    std::vector<LaunchpadScanReply> replies_;

    std::thread thread_;  // LAST: joined in the dtor / before the next start, so members outlive the scan
};

}  // namespace retroplug
