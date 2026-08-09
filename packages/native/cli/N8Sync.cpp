#include "N8Sync.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "host/input/MidiIo.hpp"
#include "host/n8/Edio.hpp"
#include "host/n8/RisaSyncTranslator.hpp"
#include "host/n8/WjwwoodSerialPort.hpp"

namespace retroplug {

namespace {

// Ctrl-C stop flag. SIGINT is async-signal-limited, so the handler only flips this atomic; the loop
// polls it. One process, one sync bridge, so a TU-local is the right mechanism.
std::atomic<bool> g_stop{false};
void onSigint(int) { g_stop.store(true, std::memory_order_relaxed); }

void printUsage() {
    std::puts(
        "usage: retroplug-cli n8-sync [options]\n"
        "\n"
        "  Translate an incoming MIDI clock/transport into risa's host-sync byte protocol and stream it\n"
        "  to a physical Everdrive N8 Pro over USB (running the risa NES tracker), so the real NES follows\n"
        "  the DAW / sequencer transport. Sibling of n8-bridge (which raw-forwards notes for EverMIDI);\n"
        "  this one is transport-driven, for risa. Ctrl-C to stop.\n"
        "\n"
        "  Feed it MIDI clock (0xF8), Start (0xFA), Continue (0xFB), Stop (0xFC) and, optionally, Song\n"
        "  Position (0xF2). Start arms from the top; Continue arms from the last Song Position. NOTE: risa's\n"
        "  mid-song locate is only exact for a uniformly-laid-out song - playing from the top is always\n"
        "  exact (see docs/risa-host-sync-report.md).\n"
        "\n"
        "options:\n"
        "  --list               list MIDI inputs + serial ports (the N8 is tagged) and exit\n"
        "  --midi-in <name>     use this MIDI input (default: all hardware inputs - pick your clock master)\n"
        "  --serial <port>      use this serial port (default: auto-detect the N8, VID:PID 38df:0017)\n"
        "  --lookahead-ms <N>   delay each message by N ms and release on a timed schedule (default: 0,\n"
        "                       immediate forward - the clock master's own timing is authoritative)\n"
        "  -h, --help           show this help");
}

int runList() {
    std::puts("MIDI inputs:");
    {
        MidiIo midi;
        const std::vector<std::string> inputs = midi.listInputs();
        if (inputs.empty()) {
            std::puts("  (none)");
        } else {
            for (const std::string& name : inputs) std::printf("  %s\n", name.c_str());
        }
    }
    std::puts("\nSerial ports:");
    const std::vector<N8PortInfo> ports = listSerialPorts();
    if (ports.empty()) {
        std::puts("  (none)");
    } else {
        for (const N8PortInfo& p : ports) {
            std::string line = "  " + p.port;
            if (!p.description.empty()) line += "  (" + p.description + ")";
            if (p.isN8) line += "  [Everdrive N8 Pro]";
            std::puts(line.c_str());
        }
    }
    return 0;
}

}  // namespace

int runN8Sync(int argc, char** argv) {
    // Parse argv[2..] (argv[1] == "n8-sync").
    bool        list = false;
    std::string midiIn;
    std::string serialPortName;
    int         lookaheadMs = 0;

    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(a, "--list") == 0) {
            list = true;
        } else if (std::strcmp(a, "--midi-in") == 0) {
            midiIn = next("--midi-in");
        } else if (std::strcmp(a, "--serial") == 0) {
            serialPortName = next("--serial");
        } else if (std::strcmp(a, "--lookahead-ms") == 0) {
            lookaheadMs = std::atoi(next("--lookahead-ms"));
            if (lookaheadMs < 0) lookaheadMs = 0;
        } else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown option '%s' (try --help)\n", a);
            return 2;
        }
    }

    if (list) return runList();

    // Resolve the serial port (explicit --serial, else auto-detect the N8).
    std::string portName = serialPortName.empty() ? findN8Port() : serialPortName;
    if (portName.empty()) {
        std::fprintf(stderr,
                     "error: no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass\n"
                     "       --serial <port>. Run 'retroplug-cli n8-sync --list' to see ports.\n");
        return 1;
    }

    std::unique_ptr<WjwwoodSerialPort> port;
    try {
        port = std::make_unique<WjwwoodSerialPort>(portName);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: cannot open serial port %s: %s\n", portName.c_str(), e.what());
        return 1;
    }

    Edio edio(*port);
    try {
        edio.connect();  // handshake; throws on a bad / absent reply
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: N8 handshake failed on %s: %s\n", portName.c_str(), e.what());
        return 1;
    }
    std::printf("Connected to Everdrive N8 Pro on %s\n", portName.c_str());

    MidiIo midi;
    midi.setInputSelection(midiIn);  // "" = all hardware inputs; set before open() so it's applied once
    if (!midi.open("RetroPlug N8 Sync")) {
        std::fprintf(stderr, "error: no MIDI system available\n");
        return 1;
    }
    if (midiIn.empty())
        std::puts("MIDI input: all hardware inputs");
    else
        std::printf("MIDI input: %s\n", midiIn.c_str());
    if (lookaheadMs > 0) std::printf("Lookahead: %d ms (timed release)\n", lookaheadMs);
    std::puts("Syncing risa - Ctrl-C to stop.");

    std::signal(SIGINT, onSigint);

    // Write one already-translated risa message to the cart FIFO. Returns false if the link died (stop).
    std::uint64_t msgCount = 0, byteCount = 0;
    auto forward = [&](const std::vector<std::uint8_t>& bytes) -> bool {
        if (bytes.empty()) return true;
        try {
            edio.fifoWR(bytes);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "\nerror: serial write failed (N8 unplugged?): %s\n", e.what());
            return false;
        }
        ++msgCount;
        byteCount += bytes.size();
        return true;
    };

    RisaSyncTranslator translator;  // MIDI clock/transport -> risa arm/clock/stop bytes

    using clock = std::chrono::steady_clock;
    struct Pending { clock::time_point due; std::vector<std::uint8_t> bytes; };
    std::deque<Pending> queue;  // lookahead>0: constant delay -> arrival order == release order (FIFO)

    std::vector<MidiIo::Message> scratch;
    std::vector<std::uint8_t>    risaOut;  // reused per message
    clock::time_point lastStatus = clock::now();
    bool alive = true;

    while (!g_stop.load(std::memory_order_relaxed) && alive) {
        const clock::time_point now = clock::now();

        midi.poll(scratch);
        for (MidiIo::Message& m : scratch) {
            if (m.bytes.empty()) continue;
            risaOut.clear();
            translator.onMessage(m.bytes.data(), m.bytes.size(), risaOut);
            if (risaOut.empty()) continue;  // non-transport MIDI (notes/CC) produces nothing
            if (lookaheadMs > 0) {
                queue.push_back({now + std::chrono::milliseconds(lookaheadMs), risaOut});
            } else if (!forward(risaOut)) {
                alive = false;
                break;
            }
        }

        // Release any due messages (lookahead path).
        while (alive && !queue.empty() && queue.front().due <= now) {
            if (!forward(queue.front().bytes)) { alive = false; break; }
            queue.pop_front();
        }

        // Throttled status line (in place).
        if (now - lastStatus >= std::chrono::milliseconds(500)) {
            std::printf("\r  forwarded %llu messages / %llu bytes   ",
                        static_cast<unsigned long long>(msgCount),
                        static_cast<unsigned long long>(byteCount));
            std::fflush(stdout);
            lastStatus = now;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }

    std::printf("\nStopped. Forwarded %llu messages / %llu bytes.\n",
                static_cast<unsigned long long>(msgCount), static_cast<unsigned long long>(byteCount));
    midi.close();
    return 0;
}

}  // namespace retroplug
