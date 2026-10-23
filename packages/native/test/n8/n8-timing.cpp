// retroplug-n8-hwtest timing - BlipToaster's MIDI latency, jitter and latency-under-load on a REAL console, in ms.
//
//   retroplug-n8-hwtest timing <idle|busy> [steps] [--raw] [--no-kick] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] [--db -40]
//                              [--wav out.wav] [--port /dev/ttyACM0]
//
// Plays a 170 bpm arrangement through N8Link - the code the plugin streams to a cart with, over the reliable link
// unless --raw - while capturing the console's analog out on the lab's Zoom L6, and times every hit from the moment
// its MIDI was WRITTEN to USB to the moment it SOUNDED:
//   tick   a short pulse-1 blip every 16th (L6 ch3 carries only the pulses, so its onsets are clean)
//   kick   the DMC kit's kick/snare on the DnB pattern (ch4 - measured in `idle` only, where nothing else is on ch4)
//   busy   adds a song's load on voices that do not touch the measured channels' onsets: a triangle bassline (ch4,
//          so the kick is not measured there), and on a MUTED pulse 2 (CC7 = 0 - it still costs the ROM everything)
//          a note with a host-style vibrato (pitch bend at 50/s), a CC1 lane at 100/s
//
// One clock for both ends: N8Link's write log stamps each write with CLOCK_MONOTONIC (std::chrono::steady_clock),
// and the capture is opened with ALSA hardware timestamps of the same clock - after every read the (frame position,
// timestamp) pair is recorded, and a least-squares line through them maps any captured sample to host time. The
// fit's residual is printed: it bounds how well "sounded" is known. What it cannot remove is the L6's own input delay
// (ADC filter + USB transfer), a constant that every latency here includes - compare runs, not absolutes, across
// interfaces. Host scheduling jitter (sleep_until) moves WHEN a hit is written, not its latency; the grid error
// below includes it, as a listener would hear it.
//
// Output, per measured voice: latency n/mean/p50/p95/max and jitter (sd), the grid error (each onset against the
// ideal 16th grid fitted to the run: what the groove feels like), and hits with no onset.

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <initializer_list>
#include <vector>

#include "host/n8/N8Link.hpp"
#include "host/n8/WjwwoodSerialPort.hpp"

using namespace retroplug;

namespace {

using Clock = std::chrono::steady_clock;
std::int64_t monoNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

constexpr unsigned kRate = 48000, kChannels = 12;

struct Capture {
    snd_pcm_t*                pcm = nullptr;
    std::vector<float>        tick, kick;      // the two measured channels, as float
    std::vector<std::pair<double, double>> stamps;   // (frame position, CLOCK_MONOTONIC ns)
    std::atomic<bool>         run{true};
    std::thread               th;
    std::string               err;
    unsigned                  tickCh = 2, kickCh = 3;   // 0-based
    std::vector<std::int32_t> all;             // optional: every channel, for --wav

    bool open(const std::string& dev, bool keepAll) {
        if (snd_pcm_open(&pcm, dev.c_str(), SND_PCM_STREAM_CAPTURE, 0) < 0) { err = "snd_pcm_open " + dev; return false; }
        snd_pcm_hw_params_t* hw;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(pcm, hw);
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S32_LE);
        snd_pcm_hw_params_set_channels(pcm, hw, kChannels);
        unsigned rate = kRate;
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
        snd_pcm_uframes_t period = 240;   // 5 ms: a timestamp per read
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
        snd_pcm_uframes_t buffer = period * 8;
        snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
        if (snd_pcm_hw_params(pcm, hw) < 0 || rate != kRate) { err = "hw params (S32_LE, 12 ch, 48 kHz)"; return false; }
        snd_pcm_sw_params_t* sw;
        snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(pcm, sw);
        snd_pcm_sw_params_set_tstamp_mode(pcm, sw, SND_PCM_TSTAMP_ENABLE);
        snd_pcm_sw_params_set_tstamp_type(pcm, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC);   // the clock N8Link stamps with
        if (snd_pcm_sw_params(pcm, sw) < 0) { err = "sw params (monotonic timestamps)"; return false; }
        th = std::thread([this, period, keepAll] {
            std::vector<std::int32_t> buf(period * kChannels);
            double framesRead = 0;
            snd_pcm_start(pcm);
            while (run) {
                const snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf.data(), period);
                if (n < 0) { if (snd_pcm_recover(pcm, static_cast<int>(n), 1) < 0) { err = "capture xrun"; return; } continue; }
                for (snd_pcm_sframes_t i = 0; i < n; ++i) {
                    tick.push_back(static_cast<float>(buf[i * kChannels + tickCh] / 2147483648.0));
                    kick.push_back(static_cast<float>(buf[i * kChannels + kickCh] / 2147483648.0));
                }
                if (keepAll) all.insert(all.end(), buf.begin(), buf.begin() + n * kChannels);
                framesRead += static_cast<double>(n);
                snd_pcm_uframes_t avail = 0;
                snd_htimestamp_t  ts{};
                if (snd_pcm_htimestamp(pcm, &avail, &ts) == 0 && (ts.tv_sec || ts.tv_nsec))
                    stamps.emplace_back(framesRead + static_cast<double>(avail),
                                        static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec));
            }
        });
        return true;
    }
    void stop() {
        run = false;
        if (th.joinable()) th.join();
        if (pcm) snd_pcm_close(pcm);
    }
};

struct Fit { double a = 0, b = 0, residualNs = 0; };   // ns = a + b * frame

Fit fitClock(const std::vector<std::pair<double, double>>& p) {
    Fit f;
    const double n = static_cast<double>(p.size());
    if (n < 2) return f;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (auto& [x, y] : p) { sx += x; sy += y; }
    const double mx = sx / n, my = sy / n;
    for (auto& [x, y] : p) { sxx += (x - mx) * (x - mx); sxy += (x - mx) * (y - my); }
    f.b = sxy / sxx;
    f.a = my - f.b * mx;
    double r = 0;
    for (auto& [x, y] : p) { const double e = y - (f.a + f.b * x); r += e * e; }
    f.residualNs = std::sqrt(r / n);
    return f;
}

// Onsets: the first sample whose |level| crosses `thr` after at least `gap` frames below it.
std::vector<std::size_t> onsets(const std::vector<float>& x, float thr, std::size_t gap) {
    std::vector<std::size_t> out;
    std::size_t quiet = gap;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (std::fabs(x[i]) >= thr) { if (quiet >= gap) out.push_back(i); quiet = 0; }
        else ++quiet;
    }
    return out;
}

struct Stats { std::size_t n = 0; double mean = 0, p50 = 0, p95 = 0, max = 0, sd = 0; };
Stats stats(std::vector<double> v) {
    Stats s;
    s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    for (double x : v) s.mean += x;
    s.mean /= static_cast<double>(v.size());
    for (double x : v) s.sd += (x - s.mean) * (x - s.mean);
    s.sd = std::sqrt(s.sd / static_cast<double>(v.size()));
    auto q = [&](double p) { return v[std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size())))]; };
    s.p50 = q(0.5); s.p95 = q(0.95); s.max = v.back();
    return s;
}

struct Ev { double ms; std::vector<std::uint8_t> bytes; };

// Does a FIFO write carry this exact message? (raw: the bytes themselves; framed: inside F5 seq ... sum)
bool carries(const std::vector<std::uint8_t>& w, const std::uint8_t* m, std::size_t n) {
    return std::search(w.begin(), w.end(), m, m + n) != w.end();
}

const char* arg(int argc, char** argv, const char* name, const char* def) {
    for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

}  // namespace

int runTiming(int argc, char** argv, const std::string& defaultPort) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s timing <idle|busy> [steps] [--raw] [--pcm hw:L6,0] [--tick-ch 3] [--kick-ch 4] "
                             "[--db -40] [--wav out.wav] [--port dev]\n", argv[0]);
        return 2;
    }
    const std::string mode  = argv[2];
    const bool        busy  = (mode == "busy");
    const int         steps = (argc > 3 && argv[3][0] != '-') ? std::atoi(argv[3]) : 128;
    const bool        raw   = flag(argc, argv, "--raw");
    const bool        noKick = flag(argc, argv, "--no-kick");   // no DMC: tells DMC-DMA effects from the rest
    const std::string pcmDev = arg(argc, argv, "--pcm", "hw:L6,0");
    const std::string port  = arg(argc, argv, "--port", defaultPort.c_str());
    const float       thr   = static_cast<float>(std::pow(10.0, std::atof(arg(argc, argv, "--db", "-40")) / 20.0));
    const char*       wav   = arg(argc, argv, "--wav", nullptr);
    if (port.empty()) { std::fprintf(stderr, "no Everdrive N8 found; pass --port\n"); return 2; }

    // The arrangement, in host ms from the start.
    const double STEP = 60000.0 / 170 / 4, T0 = 300;
    const std::uint8_t TICK_ON[3] = {0x90, 84, 120}, KICK_ON[3] = {0x94, 0, 120}, SNARE_ON[3] = {0x94, 1, 120};
    std::vector<Ev> evs;
    auto at = [&](double ms, std::initializer_list<std::uint8_t> b) { evs.push_back({ms, b}); };
    if (raw)   // a ROM left in link mode by an earlier run drops back to plain MIDI after 8 unframed messages
        for (int i = 0; i < 8; ++i) at(i * 2.0, {0xB0, 7, 127});
    at(20, {0xB0, 7, 127});
    at(25, {0xB3, 7, 127});
    for (int i = 0; i < steps; ++i) {
        const double ms = T0 + i * STEP;
        const int s16 = i % 16;
        if (!noKick && (s16 == 0 || s16 == 10)) at(ms, {0x94, 0, 120});
        if (!noKick && (s16 == 4 || s16 == 12)) at(ms, {0x94, 1, 120});
        at(ms, {0x90, 84, 120});
        at(ms + 30, {0x80, 84, 0});
    }
    const double end = T0 + steps * STEP;
    if (busy) {
        at(T0 - 200, {0xB1, 7, 0});                 // pulse 2 muted: it still costs the ROM all of the below
        at(T0 - 190, {0x91, 57, 100});
        for (int i = 0; i < steps; i += 2) {
            const std::uint8_t n = static_cast<std::uint8_t>(33 + ((i >> 2) % 5));
            at(T0 + i * STEP, {0x92, n, 100});
            at(T0 + (i + 2) * STEP - 1, {0x82, n, 0});
        }
        for (double t = T0; t < end; t += 10)
            at(t, {0xB1, 1, static_cast<std::uint8_t>(std::lround(64 + 63 * std::sin(t / 300)))});
        // Its vibrato, the way a DAW's LFO plays it since BlipToaster dropped its own (2026-10-22): a 5 Hz wobble sent as
        // pitch bend at 50 messages/s - the same load as the emulator's tests/timing.test.ts busy song.
        for (double t = T0 + 5; t < end; t += 20) {
            const int b = static_cast<int>(std::lround(8192 + 1500 * std::sin(6.283185307179586 * 5 * t / 1000)));
            at(t, {0xE1, static_cast<std::uint8_t>(b & 0x7F), static_cast<std::uint8_t>(b >> 7)});
        }
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.ms < b.ms; });

    // The link, with its write log.
    std::mutex wm;
    std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> writes;
    N8Link link([](const std::string& p) -> std::unique_ptr<ISerialPort> { return std::make_unique<WjwwoodSerialPort>(p); });
    link.setLinkAllowed(!raw);
    link.setWriteLog([&](const std::uint8_t* d, std::size_t n, std::int64_t ns, bool resend) {
        if (resend) return;   // a hit's latency runs from its FIRST write: a re-send is part of it
        std::lock_guard<std::mutex> l(wm);
        writes.emplace_back(ns, std::vector<std::uint8_t>(d, d + n));
    });
    if (!link.connect(port)) { std::fprintf(stderr, "timing: connect failed: %s\n", link.lastError().c_str()); return 1; }
    link.setLookaheadMs(0);

    Capture cap;
    cap.tickCh = static_cast<unsigned>(std::atoi(arg(argc, argv, "--tick-ch", "3")) - 1);
    cap.kickCh = static_cast<unsigned>(std::atoi(arg(argc, argv, "--kick-ch", "4")) - 1);
    if (!cap.open(pcmDev, wav != nullptr)) { std::fprintf(stderr, "timing: capture: %s\n", cap.err.c_str()); return 1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // the capture settles; the link probe is done
    const bool reliable = link.reliable();

    const auto t0 = Clock::now();
    for (const Ev& e : evs) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<std::int64_t>(e.ms * 1000)));
        link.push(0, e.bytes.data(), e.bytes.size(), 48000.0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const std::uint64_t resends = link.linkResends();
    link.disconnect();
    cap.stop();
    if (!cap.err.empty()) { std::fprintf(stderr, "timing: capture: %s\n", cap.err.c_str()); return 1; }

    if (wav) {   // the raw capture, for a second look (analyze-capture / capture-onsets.ts)
        if (FILE* f = std::fopen(wav, "wb")) {
            const std::uint32_t data = static_cast<std::uint32_t>(cap.all.size() * 4), byteRate = kRate * kChannels * 4;
            const std::uint16_t fmt = 1, ch = kChannels, align = kChannels * 4, bits = 32;
            const std::uint32_t riff = 36 + data, fmtLen = 16;
            std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
            std::fwrite(&fmtLen, 4, 1, f); std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
            std::fwrite(&kRate, 4, 1, f); std::fwrite(&byteRate, 4, 1, f); std::fwrite(&align, 2, 1, f);
            std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
            std::fwrite(cap.all.data(), 4, cap.all.size(), f);
            std::fclose(f);
        }
    }

    const Fit clk = fitClock(cap.stamps);
    // A capture that glitched (an xrun, a stalled timestamp) leaves a fit that maps samples to the wrong times - every
    // number below would be wrong with it. A healthy run fits to ~0.01 ms.
    if (cap.stamps.size() < 100 || clk.residualNs > 0.5e6) {
        std::fprintf(stderr, "timing: the capture clock fit is bad (%zu timestamps, residual %.3f ms) - run again\n",
                     cap.stamps.size(), clk.residualNs / 1e6);
        return 1;
    }
    auto frameNs = [&](std::size_t k) { return clk.a + clk.b * static_cast<double>(k); };
    const double ppm = (clk.b / (1e9 / kRate) - 1.0) * 1e6;

    // Pair each hit's first write with the first onset after it on its channel.
    auto measure = [&](const std::uint8_t* msg, const std::vector<float>& x) {
        std::vector<std::int64_t> sent;
        for (auto& [ns, w] : writes) if (carries(w, msg, 3)) sent.push_back(ns);
        const auto on = onsets(x, thr, static_cast<std::size_t>(0.020 * kRate));
        std::vector<double> lat, onsetMs;
        std::size_t j = 0, missed = 0;
        for (std::int64_t s : sent) {
            while (j < on.size() && frameNs(on[j]) < static_cast<double>(s)) ++j;
            if (j < on.size() && frameNs(on[j]) - static_cast<double>(s) < STEP * 0.5e6) {
                lat.push_back((frameNs(on[j]) - static_cast<double>(s)) / 1e6);
                onsetMs.push_back(frameNs(on[j]) / 1e6);
                ++j;
            } else {
                ++missed;
            }
        }
        return std::make_tuple(sent.size(), stats(lat), onsetMs, missed);
    };
    auto [tickSent, tickLat, tickOnsets, tickMissed] = measure(TICK_ON, cap.tick);

    // Grid error: the tick onsets against the ideal 16th grid, offset fitted (a pure drift would show as a slope).
    std::vector<double> grid;
    if (tickOnsets.size() > 2) {
        double off = 0;
        for (std::size_t i = 0; i < tickOnsets.size(); ++i) off += tickOnsets[i] - static_cast<double>(i) * STEP;
        off /= static_cast<double>(tickOnsets.size());
        for (std::size_t i = 0; i < tickOnsets.size(); ++i) grid.push_back(std::fabs(tickOnsets[i] - (off + static_cast<double>(i) * STEP)));
    }
    const Stats g = stats(grid);

    std::printf("timing: %s, %d steps @ 170 bpm, %s, %llu re-sends\n", mode.c_str(), steps,
                reliable ? "reliable link" : (raw ? "raw (--raw)" : "raw (the ROM did not answer the probe)"),
                static_cast<unsigned long long>(resends));
    std::printf("clock: %zu ALSA timestamps, fit residual %.3f ms, L6 vs host %+.1f ppm (latency includes the L6's input delay)\n",
                cap.stamps.size(), clk.residualNs / 1e6, ppm);
    std::printf("  voice   n    mean    p50    p95    max  jitter(sd)  missed   [ms, USB write -> sound]\n");
    auto row = [](const char* v, const Stats& s, std::size_t missed) {
        std::printf("  %-6s%4zu %6.2f %6.2f %6.2f %6.2f %6.2f      %4zu\n", v, s.n, s.mean, s.p50, s.p95, s.max, s.sd, missed);
    };
    row("tick", tickLat, tickMissed + (static_cast<std::size_t>(steps) - std::min<std::size_t>(tickSent, steps)));
    if (!busy) {
        auto [ks, kLat, kOn, kMiss] = measure(KICK_ON, cap.kick);
        auto [ss, sLat, sOn, sMiss] = measure(SNARE_ON, cap.kick);
        (void)ks; (void)kOn; (void)ss; (void)sOn;
        row("kick", kLat, kMiss);
        row("snare", sLat, sMiss);
    }
    std::printf("  tick grid error: p50 %6.2f  p95 %6.2f  max %6.2f ms (onsets vs the ideal 16th grid; includes host scheduling)\n",
                g.p50, g.p95, g.max);
    return 0;
}
