#include "n8/Ops.hpp"

#include <cstring>

#include "graph/noinline.hpp"
#include "n8/Menu.hpp"

namespace rp::n8 {

namespace {

Status at(Status s, Step step) {
    s.step = step;
    return s;
}

Status failed(Code code, Where where, Step step) {
    Status s;
    s.code  = code;
    s.where = where;
    s.step  = step;
    return s;
}

// A path's directory and file name, split at its last separator (either kind: the menu takes '/', a Windows caller may
// hand '\\'). Built from the pointer, not substr(), whose range check is a throw a device image may not reference.
std::string_view baseName(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : std::string_view(path.data() + slash + 1, path.size() - slash - 1);
}
std::string_view dirName(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string_view{} : std::string_view(path.data(), slash);
}

// One operation's progress inside another's: bootRom's files report their bytes on top of what came before them.
struct Shifted {
    Progress      outer;
    std::uint32_t base  = 0;
    std::uint32_t total = 0;

    Progress progress() {
        return Progress{[](void* ctx, std::uint32_t done, std::uint32_t) {
                            const Shifted& s = *static_cast<const Shifted*>(ctx);
                            return s.outer(s.base + done, s.total);
                        },
                        this};
    }
};

// A Progress that answers its first ask itself (go on) and passes the rest on.
struct SkipFirst {
    Progress outer;
    bool     first = true;

    Progress progress() {
        return Progress{[](void* ctx, std::uint32_t done, std::uint32_t total) {
                            SkipFirst& s = *static_cast<SkipFirst*>(ctx);
                            if (s.first) {
                                s.first = false;
                                return true;
                            }
                            return s.outer(done, total);
                        },
                        this};
    }
};

// The menu keeps a game's battery at EDN8/gamedata/<the ROM's file name>/bram.srm.
constexpr std::string_view kBatteryDir  = "EDN8/gamedata/";
constexpr std::string_view kBatteryFile = "/bram.srm";

// The longest battery path a FatFs name gives, its NUL included: the directory (14), the name (255 UTF-16 units, at
// most three bytes of UTF-8 each), the file (9).
constexpr std::size_t kBatteryPathBytes = 14 + 3 * 255 + 9 + 1;

// Whether the battery path for `romPath` fits kBatteryPathBytes, which is what batteryPath() builds it in.
bool batterySlotFits(std::string_view romPath) {
    return kBatteryDir.size() + baseName(romPath).size() + kBatteryFile.size() + 1 <= kBatteryPathBytes;
}

// bootRom's battery: its path built and the file written. Apart from bootRom and never inlined into it, so the path's
// buffer is on the stack only while a battery is written, and a boot with none (the Pico box's) carries none of it.
RP_NOINLINE Status writeBattery(Edio& edio, std::string_view romPath, ByteView srm, Progress progress) {
    char battery[kBatteryPathBytes];
    if (!batteryPath(romPath, battery, sizeof battery)) return failed(Code::TooLarge, Where::BatteryPath, Step::Save);
    return writeFile(edio, battery, srm, progress);
}

// The caller's before() for `step`: true to go on (and when it set none).
bool ahead(const BootOptions& options, Step step) { return !options.before || options.before(options.ctx, step); }

}  // namespace

Status readFile(Edio& edio, std::string_view path, FileSink sink, std::size_t* size, Progress progress) {
    if (size) *size = 0;
    if (path.size() > Edio::kStringMax) return failed(Code::TooLarge, Where::Length, Step::Open);  // before any byte
    const std::string_view dir   = dirName(path);
    const std::string_view name  = baseName(path);
    bool                   found = false;
    std::uint32_t          bytes = 0;
    const Status listed = edio.listDir(dir, [&](const DirEntry& e) {
        if (!found && !e.isDir && e.name == name) {
            found = true;
            bytes = e.size;
        }
    });
    if (!listed.ok()) return at(listed, Step::List);
    if (!found) return failed(Code::NotFound, Where::None, Step::List);
    if (size) *size = bytes;
    // An empty file needs no buffer (an empty vector's may be null).
    std::uint8_t* const buffer = bytes == 0 ? nullptr : sink.buffer ? sink.buffer(sink.ctx, bytes) : nullptr;
    if (bytes != 0 && !buffer) return failed(Code::TooLarge, Where::Buffer, Step::List);  // sized, nothing opened

    if (!progress(0, bytes)) return failed(Code::Cancelled, Where::None, Step::Open);
    if (Status s = edio.fileOpen(path, Edio::FA_READ); !s.ok()) return at(s, Step::Open);
    // The open's stop point answered for the data's start, so fileRead's own first ask (at 0, before the first block)
    // is not passed on: the caller is asked once at 0, as at every other point.
    SkipFirst skip{progress};
    if (Status s = edio.fileRead(buffer, bytes, skip.progress()); !s.ok()) {
        // A cancel came between two blocks, with nothing owed: the file is closed (best-effort) before it answers.
        if (s.code == Code::Cancelled) (void)edio.fileClose();
        return at(s, Step::Data);
    }
    if (Status s = edio.fileClose(); !s.ok()) return at(s, Step::Close);
    return {};
}

Status readFile(Edio& edio, std::string_view path, std::uint8_t* buffer, std::size_t capacity, std::size_t* size,
                Progress progress) {
    struct Fixed {
        std::uint8_t* buffer;
        std::size_t   capacity;
    } fixed{buffer, capacity};
    const FileSink sink{[](void* ctx, std::size_t bytes) -> std::uint8_t* {
                            const Fixed& f = *static_cast<const Fixed*>(ctx);
                            return bytes <= f.capacity ? f.buffer : nullptr;
                        },
                        &fixed};
    return readFile(edio, path, sink, size, progress);
}

Status writeFile(Edio& edio, std::string_view path, ByteView data, Progress progress) {
    if (path.size() > Edio::kStringMax) return failed(Code::TooLarge, Where::Length, Step::Open);  // before any byte
    if (!Edio::countable(data.size)) return failed(Code::TooLarge, Where::Size, Step::Open);  // before any byte
    const auto total = static_cast<std::uint32_t>(data.size);
    if (!progress(0, total)) return failed(Code::Cancelled, Where::None, Step::Open);
    if (Status s = edio.fileOpen(path, Edio::FA_REPLACE); !s.ok()) return at(s, Step::Open);
    if (Status s = edio.fileWrite(data.data, data.size, progress); !s.ok()) return at(s, Step::Data);
    if (Status s = edio.fileClose(); !s.ok()) return at(s, Step::Close);
    return {};
}

bool batteryPath(std::string_view romPath, char* out, std::size_t capacity) {
    const std::string_view rom  = baseName(romPath);
    const std::size_t      size = kBatteryDir.size() + rom.size() + kBatteryFile.size();
    if (size + 1 > capacity) return false;
    std::memcpy(out, kBatteryDir.data(), kBatteryDir.size());
    std::memcpy(out + kBatteryDir.size(), rom.data(), rom.size());
    std::memcpy(out + kBatteryDir.size() + rom.size(), kBatteryFile.data(), kBatteryFile.size());
    out[size] = '\0';
    return true;
}

Status bootRom(Edio& edio, std::string_view path, ByteView rom, ByteView srm, Upload upload, std::uint16_t* mapIndex,
               Progress progress, BootOptions options) {
    // What a boot cannot finish is refused before its first byte, not after a test, an upload or a battery went out.
    if (path.size() > Edio::kStringMax)
        return failed(Code::TooLarge, Where::Length, upload == Upload::Always ? Step::Upload : Step::Install);
    if (upload == Upload::Always && !Edio::countable(rom.size))
        return failed(Code::TooLarge, Where::Size, Step::Upload);
    if (!Edio::countable(srm.size)) return failed(Code::TooLarge, Where::Size, Step::Save);
    if (srm.size != 0 && !batterySlotFits(path)) return failed(Code::TooLarge, Where::BatteryPath, Step::Save);
    // The progress counts both files in 32 bits, so the two together must fit it too.
    const std::uint64_t counted = (upload == Upload::Always ? std::uint64_t{rom.size} : 0) + srm.size;
    if (counted > 0xFFFFFFFFu) return failed(Code::TooLarge, Where::Size, Step::Upload);
    const std::uint32_t uploaded = upload == Upload::Always ? static_cast<std::uint32_t>(rom.size) : 0;
    const std::uint32_t total    = static_cast<std::uint32_t>(counted);
    Menu                menu(edio);

    // Confirm the menu is running: a running game does not answer '*t'.
    if (options.handshake) {
        if (!ahead(options, Step::Test) || !progress(0, total)) return failed(Code::Cancelled, Where::None, Step::Test);
        if (Status s = menu.test(); !s.ok()) return at(s, Step::Test);
    }

    if (upload == Upload::Always) {
        if (!ahead(options, Step::Upload)) return failed(Code::Cancelled, Where::None, Step::Upload);
        Shifted shifted{progress, 0, total};
        if (Status s = writeFile(edio, path, rom, shifted.progress()); !s.ok()) return at(s, Step::Upload);
    }
    // The battery goes the native way: into the menu's per-game save slot BEFORE the menu loads the game, so the MENU
    // copies it into cart SRAM at the hand-off.
    if (srm.size != 0) {
        if (!ahead(options, Step::Save)) return failed(Code::Cancelled, Where::None, Step::Save);
        Shifted shifted{progress, uploaded, total};
        if (Status s = writeBattery(edio, path, srm, shifted.progress()); !s.ok()) return at(s, Step::Save);
    }

    if (!ahead(options, Step::Install) || !progress(total, total))
        return failed(Code::Cancelled, Where::None, Step::Install);
    if (Status s = menu.install(path, mapIndex); !s.ok()) return at(s, Step::Install);
    if (!ahead(options, Step::Start)) return failed(Code::Cancelled, Where::None, Step::Start);
    if (Status s = menu.start(); !s.ok()) return at(s, Step::Start);
    return {};
}

}  // namespace rp::n8
