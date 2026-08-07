#include "N8Bridge.hpp"

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
#include "host/n8/WjwwoodSerialPort.hpp"

namespace retroplug {

namespace {

// Ctrl-C stop flag. SIGINT is async-signal-limited, so the handler only flips this atomic; the loop
// polls it. One process, one bridge, so a TU-local is the right mechanism.
std::atomic<bool> g_stop{false};
void onSigint(int) { g_stop.store(true, std::memory_order_relaxed); }

void printUsage() {
    std::puts(
        "usage: retroplug-cli n8-bridge [options]\n"
        "\n"
        "  Pipe live MIDI input to a physical Everdrive N8 Pro over USB (running EverMIDI),\n"
        "  so a controller / DAW plays the real NES. One-way (host -> cart). Ctrl-C to stop.\n"
        "\n"
        "options:\n"
        "  --list               list MIDI inputs + serial ports (the N8 is tagged) and exit\n"
        "  --midi-in <name>     use this MIDI input (default: all hardware inputs)\n"
        "  --serial <port>      use this serial port (default: auto-detect the N8, VID:PID 38df:0017)\n"
        "  --lookahead-ms <N>   delay each message by N ms and release on a timed schedule (default: 0,\n"
        "                       immediate forward - the MIDI source's own timing is authoritative)\n"
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

int runN8Bridge(int argc, char** argv) {
    // Parse argv[2..] (argv[1] == "n8-bridge").
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
                     "       --serial <port>. Run 'retroplug-cli n8-bridge --list' to see ports.\n");
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
    if (!midi.open("RetroPlug N8")) {
        std::fprintf(stderr, "error: no MIDI system available\n");
        return 1;
    }
    midi.setInputSelection(midiIn);  // "" = all hardware inputs
    if (midiIn.empty())
        std::puts("MIDI input: all hardware inputs");
    else
        std::printf("MIDI input: %s\n", midiIn.c_str());
    if (lookaheadMs > 0) std::printf("Lookahead: %d ms (timed release)\n", lookaheadMs);
    std::puts("Bridging - Ctrl-C to stop.");

    std::signal(SIGINT, onSigint);

    // Forward one message to the cart FIFO. Returns false if the serial link died (stop the loop).
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

    using clock = std::chrono::steady_clock;
    struct Pending { clock::time_point due; std::vector<std::uint8_t> bytes; };
    std::deque<Pending> queue;  // lookahead>0: constant delay -> arrival order == release order (FIFO)

    std::vector<MidiIo::Message> scratch;
    clock::time_point lastStatus = clock::now();
    bool alive = true;

    while (!g_stop.load(std::memory_order_relaxed) && alive) {
        const clock::time_point now = clock::now();

        midi.poll(scratch);
        for (MidiIo::Message& m : scratch) {
            if (m.bytes.empty()) continue;
            if (lookaheadMs > 0) {
                queue.push_back({now + std::chrono::milliseconds(lookaheadMs), std::move(m.bytes)});
            } else if (!forward(m.bytes)) {
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
