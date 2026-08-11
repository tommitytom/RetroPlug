// n8-sd-hwtest - a manual hardware harness for the N8 SD / menu ops (Settings > N8 Pro). It constructs the
// SAME native N8Host + N8SdWorker + connection manager the standalone/plugin menu drives, but against a real
// /dev/ttyACM0, so a hardware pass exercises exactly the code path the UI does (not the CLI's TS stack).
// EXCLUDE_FROM_ALL, built by name (retroplug-n8-hwtest); NOT a CI target. Needs a real N8 plugged in.
//
//   retroplug-n8-hwtest dump    <dest.srm>   [port]   # read the 64 KB battery to a file (non-destructive)
//   retroplug-n8-hwtest load    <rom.nes>    [port]   # upload + boot a ROM (cart must be at its menu)
//   retroplug-n8-hwtest restore <save.srm>   [port]   # write a .srm to the running game's SRAM + verify
//
// Exit 0 on success, 1 on the op's error, 2 on a usage / no-device problem.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "host/n8/N8Host.hpp"
#include "host/n8/WjwwoodSerialPort.hpp"

using namespace retroplug;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <dump|load|restore> <path> [port]\n", argv[0]);
        return 2;
    }
    const std::string op   = argv[1];
    const std::string path = argv[2];
    std::string       port = argc > 3 ? argv[3] : findN8Port();
    if (port.empty()) {
        std::fprintf(stderr, "no Everdrive N8 found (VID:PID 38df:0017); pass a port explicitly\n");
        return 2;
    }
    std::printf("N8 SD hw-test: op=%s path=%s port=%s\n", op.c_str(), path.c_str(), port.c_str());

    N8Host host(
        [](const std::string& p) -> std::unique_ptr<ISerialPort> { return std::make_unique<WjwwoodSerialPort>(p); },
        [] {
            std::vector<N8PortDto> v;
            for (const auto& p : listSerialPorts()) v.push_back({p.port, p.isN8});
            return v;
        },
        "/tmp");
    host.setPort(port);

    if (op == "dump")         host.startDumpSram(path);
    else if (op == "load")    host.startLoadRom(path);
    else if (op == "restore") host.startRestoreSram(path);
    else {
        std::fprintf(stderr, "unknown op '%s'\n", op.c_str());
        return 2;
    }

    // Poll the same status snapshot the UI polls, printing progress until the job finishes.
    std::uint64_t lastVersion = ~0ull;
    for (;;) {
        const N8SdStatusDto s = host.sdStatus();
        if (s.version != lastVersion) {
            lastVersion = s.version;
            const int pct = s.bytesTotal ? static_cast<int>(100 * s.bytesDone / s.bytesTotal) : 0;
            std::printf("\r  [%-8s] %-14s %6llu/%-6llu %3d%%   ", s.op.c_str(), s.phase.c_str(),
                        static_cast<unsigned long long>(s.bytesDone),
                        static_cast<unsigned long long>(s.bytesTotal), pct);
            std::fflush(stdout);
        }
        if (s.done) {
            std::printf("\n");
            if (!s.error.empty()) { std::printf("ERROR: %s\n", s.error.c_str()); return 1; }
            std::printf("OK: %s\n", s.result.c_str());
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
