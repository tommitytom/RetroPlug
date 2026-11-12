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
    // A call failed on the line, which may have left it out of step with the protocol: the rest of a reply still
    // arriving, or the N8 still inside a command. The next call resyncs first (N8Rpc::ready), and only a handshake that
    // answers clears it.
    bool dirty = false;
    // The timeout the session's last handshake was given (0: the table's), which a resync handshakes with.
    std::uint32_t handshakeMs = 0;
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
    r.step                = stepName(status.step);
    if (status.code == Code::Device) r.deviceStatus = status.device;
    if (!status.ok()) r.message = retroplug::failureText(status, port, path);
    return r;
}

// Whether a call that answered `status` left the line as it found it: it succeeded, it was refused before its first byte
// (refusedBeforePort, and a file larger than its buffer, refused after a listing that ended in step), or its file was
// not on a listing that ended in step. Anything else may have stopped part-way through a reply or a command.
bool leavesLineInStep(const Status& status) {
    if (status.ok() || status.code == Code::NotFound || retroplug::refusedBeforePort(status)) return true;
    return status.code == Code::TooLarge && status.where == Where::Buffer;
}

// Read until the line has been quiet for one read (20 ms; 20 reads, about 0.4 s, at most), then drop what the OS holds:
// the rest of a reply the caller gave up on can still be arriving, record by record. A read the port fails ends it
// too, and the handshake after it meets that failure in the port's words.
void drain(retroplug::PortTransport& port) {
    std::uint8_t sink[4096];
    for (int i = 0; i < 20 && port.read(sink, sizeof sink, 20) > 0; ++i) {
    }
    port.flushInput();
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
// it allows. A COM name is already canonical, and std::filesystem::canonical on one would open the device (MSVC's STL
// opens a handle to resolve it), so Windows compares names alone.
bool samePort(const std::string& a, const std::string& b) {
    if (a == b) return true;
#if defined(_WIN32)
    return false;
#else
    if (a.empty() || b.empty()) return false;
    std::error_code ea, eb;
    const auto      ca = std::filesystem::canonical(a, ea);
    const auto      cb = std::filesystem::canonical(b, eb);
    return !ea && !eb && ca == cb;
#endif
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

N8Rpc::Session* N8Rpc::ready(std::int32_t handle, N8Reply& refused) {
    Session* s = find(handle);
    if (!s) {
        refused = notOpen(handle);
        return nullptr;
    }
    if (!s->dirty) return s;
    drain(*s->port);
    // A drain cannot tell a line in step from one where the N8 is still inside a command (a file write waiting for
    // data that never came); the handshake can, since only a line in step answers it.
    const Status st = s->edio.connect(nullptr, s->handshakeMs);
    if (!st.ok()) {
        refused = replyOf(st, *s->port);
        return nullptr;
    }
    s->dirty = false;
    return s;
}

N8Reply N8Rpc::settle(Session& session, const Status& status, std::string_view path) {
    if (!leavesLineInStep(status)) session.dirty = true;
    return replyOf(status, *session.port, path);
}

std::vector<retroplug::N8PortDto> N8Rpc::n8ListPorts() {
    return lister_ ? lister_() : std::vector<retroplug::N8PortDto>{};
}

N8Reply N8Rpc::n8Open(std::string port) {
    N8Reply r;
    r.status  = codeName(Code::LinkDown);
    r.message = "cannot open serial port: " + port;
    // The streaming link's port is not a session's: two writers on one N8 would interleave their commands. (Another
    // process's port is refused by the transport, which holds its port exclusive: N8Rpc.hpp.)
    if ((host_.link().isConnected() || host_.cartJobBusy()) && samePort(host_.link().portName(), port)) return r;
    std::unique_ptr<retroplug::PortTransport> transport;
    try {
        transport = factory_ ? factory_(port) : nullptr;
    } catch (const retroplug::PortBusyError& e) {
        r.message = e.what();  // another process has it: the words say so
        return r;
    } catch (const std::exception&) {
        return r;  // absent, no permission
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
    N8Reply  r;
    Session* s = find(handle);
    if (!s) return notOpen(handle);
    // The handshake is the resync: a dirty session drains and runs the caller's own once, with the caller's timeout,
    // not ready()'s first.
    if (s->dirty) drain(*s->port);
    s->handshakeMs    = timeoutMs;
    std::uint8_t code = 0;
    const Status st   = s->edio.connect(&code, timeoutMs);
    if (st.ok()) s->dirty = false;
    r                 = settle(*s, st);
    r.value           = code;
    return r;
}

N8Reply N8Rpc::n8Status(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint8_t code = 0;
    r                 = settle(*s, s->edio.status(code));
    r.value           = code;
    return r;
}

N8Reply N8Rpc::n8SysInfo(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint8_t info[Edio::SIZE_SYS_INFO];
    const Status st = s->edio.sysInfo(info);
    r               = settle(*s, st);
    if (st.ok()) r.bytes = bytesOf(info, sizeof info);
    return r;
}

N8Reply N8Rpc::n8Vdc(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint8_t vdc[Edio::SIZE_VDC];
    const Status st = s->edio.vdc(vdc);
    r               = settle(*s, st);
    if (st.ok()) r.bytes = bytesOf(vdc, sizeof vdc);
    return r;
}

N8Reply N8Rpc::n8FreeSpace(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint64_t bytesFree = 0;
    r                       = settle(*s, s->edio.freeSpace(bytesFree));
    r.value                 = bytesFree;
    return r;
}

N8Reply N8Rpc::n8DirMake(std::int32_t handle, std::string path) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.dirMake(path), path) : r;
}

N8Reply N8Rpc::n8FileDelete(std::int32_t handle, std::string path) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.fileDelete(path), path) : r;
}

N8Reply N8Rpc::n8MemRD(std::int32_t handle, std::uint32_t addr, std::uint32_t size) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::vector<std::uint8_t> buf(size);
    const Status              st = s->edio.memRD(addr, buf.data(), buf.size());
    r                            = settle(*s, st);
    if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
    return r;
}

N8Reply N8Rpc::n8MemWR(std::int32_t handle, std::uint32_t addr, rfl::Bytestring data) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.memWR(addr, dataOf(data), data.size())) : r;
}

N8Reply N8Rpc::n8FifoWR(std::int32_t handle, rfl::Bytestring data) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.fifoWR(dataOf(data), data.size())) : r;
}

N8Reply N8Rpc::n8FifoTxString(std::int32_t handle, std::string text) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.fifoTxString(text), text) : r;
}

N8Reply N8Rpc::n8ListDir(std::int32_t handle, std::string path) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::vector<N8DirEntryDto> entries;
    const Status               st = s->edio.listDir(path, [&](const DirEntry& e) {
        entries.push_back({wellFormed(e.name), e.size, e.isDir});
    });
    r = settle(*s, st, path);
    if (st.ok()) r.entries = std::move(entries);
    return r;
}

N8Reply N8Rpc::n8FileOpen(std::int32_t handle, std::string path, std::int32_t mode) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.fileOpen(path, static_cast<std::uint8_t>(mode)), path) : r;
}

N8Reply N8Rpc::n8FileRead(std::int32_t handle, std::uint32_t size) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::vector<std::uint8_t> buf(size);
    const Status              st = s->edio.fileRead(buf.data(), buf.size());
    r                            = settle(*s, st);
    if (st.ok()) r.bytes = bytesOf(buf.data(), buf.size());
    return r;
}

N8Reply N8Rpc::n8FileWrite(std::int32_t handle, rfl::Bytestring data) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.fileWrite(dataOf(data), data.size())) : r;
}

N8Reply N8Rpc::n8FileClose(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, s->edio.fileClose()) : r;
}

N8Reply N8Rpc::n8ReadData(std::int32_t handle, std::uint32_t size, std::uint32_t timeoutMs) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::vector<std::uint8_t> buf(size);
    const Status              st = s->edio.readData(buf.data(), buf.size(), timeoutMs);
    r                            = settle(*s, st);
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
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, Menu(s->edio).test()) : r;
}

N8Reply N8Rpc::n8MenuInstall(std::int32_t handle, std::string path) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint16_t index = 0;
    r                   = settle(*s, Menu(s->edio).install(path, &index), path);
    r.value             = index;
    return r;
}

N8Reply N8Rpc::n8MenuStart(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, Menu(s->edio).start()) : r;
}

N8Reply N8Rpc::n8MenuVramDump(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint8_t vram[Menu::kVramBytes];
    std::uint8_t palette[Menu::kPaletteBytes];
    const Status st = Menu(s->edio).vramDump(vram, palette);
    r               = settle(*s, st);
    if (st.ok()) {
        r.bytes = bytesOf(vram, sizeof vram);
        const rfl::Bytestring tail = bytesOf(palette, sizeof palette);
        r.bytes.insert(r.bytes.end(), tail.begin(), tail.end());
    }
    return r;
}

N8Reply N8Rpc::n8MenuReset(std::int32_t handle) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, Menu(s->edio).reset()) : r;
}

N8Reply N8Rpc::n8ReadFile(std::int32_t handle, std::string path) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::vector<std::uint8_t> buf;
    const FileSink            sink{[](void* ctx, std::size_t size) {
                            auto& b = *static_cast<std::vector<std::uint8_t>*>(ctx);
                            b.resize(size);
                            return b.data();
                        },
                        &buf};
    std::size_t  size = 0;
    const Status st   = readFile(s->edio, path, sink, &size);
    r                 = settle(*s, st, path);
    if (st.ok()) r.bytes = bytesOf(buf.data(), size);
    return r;
}

N8Reply N8Rpc::n8WriteFile(std::int32_t handle, std::string path, rfl::Bytestring data) {
    N8Reply  r;
    Session* s = ready(handle, r);
    return s ? settle(*s, writeFile(s->edio, path, ByteView{dataOf(data), data.size()}), path) : r;
}

N8Reply N8Rpc::n8BootRom(std::int32_t handle, std::string path, rfl::Bytestring rom, rfl::Bytestring srm,
                         bool upload) {
    N8Reply  r;
    Session* s = ready(handle, r);
    if (!s) return r;
    std::uint16_t index = 0;
    const Status  st    = bootRom(s->edio, path, ByteView{dataOf(rom), rom.size()}, ByteView{dataOf(srm), srm.size()},
                                  upload ? Upload::Always : Upload::Never, &index);
    r                   = settle(*s, st, path);
    r.value             = index;
    return r;
}

}  // namespace rp::n8
