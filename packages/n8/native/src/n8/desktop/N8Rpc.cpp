#include "n8/desktop/N8Rpc.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

#include "n8/Menu.hpp"
#include "n8/Ops.hpp"
#include "n8/desktop/PortTransport.hpp"

namespace rp::n8 {

// One open session: its port, and the Edio over it. Declared here, so the facet's header names no transport.
struct N8Rpc::Session {
    Session(std::string name, std::unique_ptr<retroplug::PortTransport> p)
        : portName(std::move(name)), port(std::move(p)), edio(*port) {}

    std::string                               portName;
    std::unique_ptr<retroplug::PortTransport> port;  // before edio, which holds a reference to it
    Edio                                      edio;
};

namespace {

// A reply from a status: its code and step by name, the N8's own byte when it answered one, and the failure's text as
// the CLI prints it (the port's own words when the port failed, else describe()'s, given the call's path). A call the
// port itself failed (deps/serial threw, and the transport answered short) is "linkDown" whatever Edio made of the
// short answer: Edio sees a read that brought nothing, which on a port that cannot tell its link's state is a
// timeout, but the port is gone, not slow, and the TS client throws an N8TimeoutError only for a "timeout".
N8Reply replyOf(const Status& status, const retroplug::PortTransport& port, std::string_view path = {}) {
    N8Reply r;
    const bool portFailed = !status.ok() && !retroplug::portSaid(status, port).empty();
    r.status              = portFailed ? codeName(Code::LinkDown) : codeName(status.code);
    r.step   = stepName(status.step);
    if (status.code == Code::Device) r.deviceStatus = status.device;
    if (!status.ok()) r.message = retroplug::failureText(status, port, path);
    return r;
}

N8Reply notOpen(std::int32_t handle) {
    N8Reply r;
    r.status  = codeName(Code::LinkDown);
    r.message = "no open N8 session (handle " + std::to_string(handle) + ")";
    return r;
}

// A listing's name as the TS client decoded it (a default TextDecoder, the WHATWG UTF-8 decoder): a leading BOM
// dropped (its ignoreBOM is false), every well-formed sequence kept, and each maximal ill-formed subpart replaced by
// U+FFFD, so a card's non-UTF-8 name reaches JS as it always did and never as whatever a codec makes of invalid UTF-8.
std::string wellFormed(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    std::size_t i = raw.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    while (i < raw.size()) {
        const auto  lead   = static_cast<unsigned char>(raw[i]);
        std::size_t needed = 0;
        // The range the next continuation byte may take: bytes, so the comparisons below are between like types.
        unsigned char lower = 0x80, upper = 0xBF;
        if (lead <= 0x7F) {
            out.push_back(static_cast<char>(lead));
            ++i;
            continue;
        }
        if (lead >= 0xC2 && lead <= 0xDF) needed = 1;
        else if (lead >= 0xE0 && lead <= 0xEF) {
            needed = 2;
            if (lead == 0xE0) lower = 0xA0;
            if (lead == 0xED) upper = 0x9F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            needed = 3;
            if (lead == 0xF0) lower = 0x90;
            if (lead == 0xF4) upper = 0x8F;
        }
        std::size_t seen = 0;
        while (needed != 0 && seen < needed && i + 1 + seen < raw.size()) {
            const auto b = static_cast<unsigned char>(raw[i + 1 + seen]);
            if (b < lower || b > upper) break;
            lower = 0x80;
            upper = 0xBF;
            ++seen;
        }
        if (needed != 0 && seen == needed) out.append(raw.substr(i, needed + 1));
        else out.append("\xEF\xBF\xBD");  // U+FFFD for the lead and the continuations it took
        i += 1 + seen;
    }
    return out;
}

// Whether two port names are one device: the same name, or (where the platform resolves them: a /dev/serial/by-id
// symlink and the /dev/ttyACM it points at) the same file. The port lister names ports only, so this is the identity
// it allows.
bool samePort(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (a.empty() || b.empty()) return false;
    std::error_code ea, eb;
    const auto      ca = std::filesystem::canonical(a, ea);
    const auto      cb = std::filesystem::canonical(b, eb);
    return !ea && !eb && ca == cb;
}

rfl::Bytestring bytesOf(const std::uint8_t* data, std::size_t size) {
    const auto* b = reinterpret_cast<const std::byte*>(data);
    return rfl::Bytestring(b, b + size);
}

const std::uint8_t* dataOf(const rfl::Bytestring& bytes) { return reinterpret_cast<const std::uint8_t*>(bytes.data()); }

}  // namespace

N8Rpc::N8Rpc(retroplug::N8Host& host, retroplug::N8Link::PortFactory factory, retroplug::N8Host::PortLister lister)
    : host_(host), factory_(std::move(factory)), lister_(std::move(lister)) {
    host_.setPortGuard([this](const std::string& port) {
        for (const auto& entry : sessions_)
            if (samePort(entry.second->portName, port)) return "the port " + port + " is open in an N8 session";
        return std::string();
    });
}

N8Rpc::~N8Rpc() { host_.setPortGuard(nullptr); }

N8Rpc::Session* N8Rpc::find(std::int32_t handle) {
    const auto it = sessions_.find(handle);
    return it == sessions_.end() ? nullptr : it->second.get();
}

std::vector<retroplug::N8PortDto> N8Rpc::n8ListPorts() {
    return lister_ ? lister_() : std::vector<retroplug::N8PortDto>{};
}

N8Reply N8Rpc::n8Open(std::string port) {
    N8Reply r;
    r.status  = codeName(Code::LinkDown);
    r.message = "cannot open serial port: " + port;
    // The streaming link's port is not a session's: two writers on one N8 would interleave their commands. (Another
    // process's port is not refused here; only Windows keeps that out, as N8Rpc.hpp says.)
    if ((host_.link().isConnected() || host_.cartJobBusy()) && samePort(host_.link().portName(), port)) return r;
    std::unique_ptr<retroplug::PortTransport> transport;
    try {
        transport = factory_ ? factory_(port) : nullptr;
    } catch (const std::exception&) {
        return r;  // absent, busy, no permission
    }
    if (!transport) return r;
    const std::int32_t handle = nextHandle_++;
    sessions_.emplace(handle, std::make_unique<Session>(port, std::move(transport)));
    r         = N8Reply{};
    r.value   = static_cast<std::uint64_t>(handle);
    return r;
}

bool N8Rpc::n8Close(std::int32_t handle) { return sessions_.erase(handle) > 0; }

N8Reply N8Rpc::n8Handshake(std::int32_t handle, std::uint32_t timeoutMs) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint8_t code = 0;
    N8Reply      r    = replyOf(s->edio.connect(&code, timeoutMs), *s->port);
    r.value           = code;
    return r;
}

N8Reply N8Rpc::n8Status(std::int32_t handle) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint8_t code = 0;
    N8Reply      r    = replyOf(s->edio.status(code), *s->port);
    r.value           = code;
    return r;
}

N8Reply N8Rpc::n8SysInfo(std::int32_t handle) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint8_t info[Edio::SIZE_SYS_INFO];
    const Status st = s->edio.sysInfo(info);
    N8Reply      r  = replyOf(st, *s->port);
    if (st.ok()) r.bytes = bytesOf(info, sizeof info);
    return r;
}

N8Reply N8Rpc::n8Vdc(std::int32_t handle) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint8_t vdc[Edio::SIZE_VDC];
    const Status st = s->edio.vdc(vdc);
    N8Reply      r  = replyOf(st, *s->port);
    if (st.ok()) r.bytes = bytesOf(vdc, sizeof vdc);
    return r;
}

N8Reply N8Rpc::n8FreeSpace(std::int32_t handle) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint64_t bytesFree = 0;
    N8Reply       r         = replyOf(s->edio.freeSpace(bytesFree), *s->port);
    r.value                 = bytesFree;
    return r;
}

N8Reply N8Rpc::n8DirMake(std::int32_t handle, std::string path) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.dirMake(path), *s->port, path) : notOpen(handle);
}

N8Reply N8Rpc::n8FileDelete(std::int32_t handle, std::string path) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.fileDelete(path), *s->port, path) : notOpen(handle);
}

N8Reply N8Rpc::n8MemRD(std::int32_t handle, std::uint32_t addr, std::uint32_t size) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::vector<std::uint8_t> buf(size);
    const Status              st = s->edio.memRD(addr, buf.data(), buf.size());
    N8Reply                   r  = replyOf(st, *s->port);
    if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
    return r;
}

N8Reply N8Rpc::n8MemWR(std::int32_t handle, std::uint32_t addr, rfl::Bytestring data) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.memWR(addr, dataOf(data), data.size()), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8FifoWR(std::int32_t handle, rfl::Bytestring data) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.fifoWR(dataOf(data), data.size()), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8FifoTxString(std::int32_t handle, std::string text) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.fifoTxString(text), *s->port, text) : notOpen(handle);
}

N8Reply N8Rpc::n8ListDir(std::int32_t handle, std::string path) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::vector<N8DirEntryDto> entries;
    const Status               st = s->edio.listDir(path, [&](const DirEntry& e) {
        entries.push_back({wellFormed(e.name), e.size, e.isDir});
    });
    N8Reply r = replyOf(st, *s->port, path);
    if (st.ok()) r.entries = std::move(entries);
    return r;
}

N8Reply N8Rpc::n8FileOpen(std::int32_t handle, std::string path, std::int32_t mode) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.fileOpen(path, static_cast<std::uint8_t>(mode)), *s->port, path) : notOpen(handle);
}

N8Reply N8Rpc::n8FileRead(std::int32_t handle, std::uint32_t size) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::vector<std::uint8_t> buf(size);
    const Status              st = s->edio.fileRead(buf.data(), buf.size());
    N8Reply                   r  = replyOf(st, *s->port);
    if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
    return r;
}

N8Reply N8Rpc::n8FileWrite(std::int32_t handle, rfl::Bytestring data) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.fileWrite(dataOf(data), data.size()), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8FileClose(std::int32_t handle) {
    Session* s = find(handle);
    return s ? replyOf(s->edio.fileClose(), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8ReadData(std::int32_t handle, std::uint32_t size, std::uint32_t timeoutMs) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::vector<std::uint8_t> buf(size);
    const Status              st = s->edio.readData(buf.data(), buf.size(), timeoutMs);
    N8Reply                   r  = replyOf(st, *s->port);
    if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
    return r;
}

N8Reply N8Rpc::n8FlushInput(std::int32_t handle) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    s->edio.flushInput();
    return N8Reply{};
}

N8Reply N8Rpc::n8SetReadTimeout(std::int32_t handle, std::uint32_t ms) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    s->edio.timeouts().readMs = ms;
    return N8Reply{};
}

N8Reply N8Rpc::n8MenuTest(std::int32_t handle) {
    Session* s = find(handle);
    return s ? replyOf(Menu(s->edio).test(), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8MenuInstall(std::int32_t handle, std::string path) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint16_t index = 0;
    N8Reply       r     = replyOf(Menu(s->edio).install(path, &index), *s->port, path);
    r.value             = index;
    return r;
}

N8Reply N8Rpc::n8MenuStart(std::int32_t handle) {
    Session* s = find(handle);
    return s ? replyOf(Menu(s->edio).start(), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8MenuVramDump(std::int32_t handle) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint8_t vram[Menu::kVramBytes];
    std::uint8_t palette[Menu::kPaletteBytes];
    const Status st = Menu(s->edio).vramDump(vram, palette);
    N8Reply      r  = replyOf(st, *s->port);
    if (st.ok()) {
        r.bytes = bytesOf(vram, sizeof vram);
        const rfl::Bytestring tail = bytesOf(palette, sizeof palette);
        r.bytes.insert(r.bytes.end(), tail.begin(), tail.end());
    }
    return r;
}

N8Reply N8Rpc::n8MenuReset(std::int32_t handle) {
    Session* s = find(handle);
    return s ? replyOf(Menu(s->edio).reset(), *s->port) : notOpen(handle);
}

N8Reply N8Rpc::n8ReadFile(std::int32_t handle, std::string path) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::vector<std::uint8_t> buf;
    const FileSink            sink{[](void* ctx, std::size_t size) {
                            auto& b = *static_cast<std::vector<std::uint8_t>*>(ctx);
                            b.resize(size);
                            return b.data();
                        },
                        &buf};
    std::size_t  size = 0;
    const Status st   = readFile(s->edio, path, sink, &size);
    N8Reply      r    = replyOf(st, *s->port, path);
    if (st.ok()) r.bytes = bytesOf(buf.data(), size);
    return r;
}

N8Reply N8Rpc::n8WriteFile(std::int32_t handle, std::string path, rfl::Bytestring data) {
    Session* s = find(handle);
    return s ? replyOf(writeFile(s->edio, path, ByteView{dataOf(data), data.size()}), *s->port, path) : notOpen(handle);
}

N8Reply N8Rpc::n8BootRom(std::int32_t handle, std::string path, rfl::Bytestring rom, rfl::Bytestring srm,
                         bool upload) {
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    std::uint16_t index = 0;
    const Status  st    = bootRom(s->edio, path, ByteView{dataOf(rom), rom.size()}, ByteView{dataOf(srm), srm.size()},
                                  upload ? Upload::Always : Upload::Never, &index);
    N8Reply       r     = replyOf(st, *s->port, path);
    r.value             = index;
    return r;
}

}  // namespace rp::n8
