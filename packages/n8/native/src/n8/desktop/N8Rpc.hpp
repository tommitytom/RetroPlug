#pragma once

// The N8 facet (docs/modular-graph-phase-e2-design.md section 6): what a product that starts the pack's provider mounts
// on its RPC server, through the rp_register_package_rpc cmake/packages.cmake generates for it from the pack's manifest
// (its `rpc` field names this header and registerRpc). Two halves, both answered on the thread the server answers on,
// the control thread, as every facet's are, and both synchronous: a long operation (a ROM booted) blocks its caller, as
// a serial read always did.
//
// The streaming-link half is what the UI's Settings > N8 drives (packages/n8/ts's createN8Client, ui/screens/menu/
// n8Devices.ts): the ports, the chosen port, the link's state and lookahead, read and set on the provider's N8Host. A
// product that does not mount it has no N8 submenu, since the UI takes "the facet answers" for "this host has the N8
// seam" (hasN8). N8Host serialises these with its link thread. The link half is the UI's alone. Each of its setters
// persists n8.cfg, which the standalone, the plugin and the CLI share (one config dir). Every product that lists the
// pack mounts the whole facet: the plugin and SDL channels carry the session half as well, and the CLI the link half,
// and only a convention keeps the CLI's sessions off the link half (no untrusted code reaches a product's channel
// today; docs/modular-graph-phase-e2-design.md section 6).
//
// The session half is what the CLI's sessions drive (n8-load, n8-play, n8-bridge, n8-sync, through createN8Client's
// openN8): a port opened by name (n8Open, answering a handle), the Edio primitives, the menu's commands and the
// operations (n8/Ops.hpp) on that handle, and n8Close. A session persists nothing and is its own Edio over its own
// port, opened through the provider's port factory (deps/serial's, or a test host's fake cart). One writer to an N8
// within this process: a port the streaming link holds does not open in a session, and the link does not connect to a
// port a session holds (N8Host::setPortGuard, which the facet answers). Both compare the device where the platform can
// tell (a /dev/serial/by-id symlink and its /dev/ttyACM are one port), else the name. Another process (another
// instance, a CLI session, minicom) is kept out by the transport: SerialTransport takes TIOCEXCL on a POSIX port after
// it opens it, so a second open fails busy, and a COM port is exclusive on Windows already. Every session call answers
// an N8Reply.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "n8/Edio.hpp"
#include "n8/desktop/N8Host.hpp"
#include "n8/desktop/N8Link.hpp"

namespace rp::n8 {

// One entry of n8ListDir's answer.
struct N8DirEntryDto {
    std::string   name;
    std::uint32_t size  = 0;
    bool          isDir = false;
};

// What every session call answers. `status` is the Edio Status's code by name (codeName: "ok", "timeout", "linkDown",
// "badReply", "device", "notFound", "cancelled", "tooLarge"), "linkDown" too for a call the port itself failed (its
// deps/serial exception: a port gone, not a device slow to answer, so the TS client throws an Error, not an
// N8TimeoutError), `deviceStatus` the N8's own status byte when it is
// "device" (0 otherwise), and `message` the failure's text as the CLI prints it (the port's own words when the port
// failed, else describe()'s; "" when ok). The rest is what the call answers: the bytes it read, a number (a handle, a
// status byte, the bytes free, a map index), a listing's entries, and the step of an operation that failed (Ops.hpp's
// Step, by name: "test", "upload", "save", "install", "start", "list", "open", "data", "close"; "" for a single call).
struct N8Reply {
    std::string                status = "ok";
    std::int32_t               deviceStatus = 0;
    std::string                message;
    rfl::Bytestring            bytes;
    std::uint64_t              value = 0;
    std::vector<N8DirEntryDto> entries;
    std::string                step;
};

class N8Rpc {
public:
    // The sessions open their ports through `factory` and list them through `lister`, the provider's own (those it
    // handed `host`), so a test host's fake cart answers a session as it answers the link.
    N8Rpc(retroplug::N8Host& host, retroplug::N8Link::PortFactory factory, retroplug::N8Host::PortLister lister);
    ~N8Rpc();
    N8Rpc(const N8Rpc&)            = delete;
    N8Rpc& operator=(const N8Rpc&) = delete;

    // --- the streaming link (the UI's) ---

    // The serial ports (each flagged when it is an N8 by its USB VID:PID), the chosen port ("" for auto-detect),
    // whether the link is up, the user's streaming toggle, the lookahead, the bytes forwarded since connect and the
    // last error.
    retroplug::N8ConfigDto n8LinkConfig() { return host_.getConfig(); }
    // Choose the serial port by name; a live link reconnects on it. Persisted to n8.cfg. Always true.
    bool n8SetPort(std::string port) {
        host_.setPort(port);
        return true;
    }
    // Open (true) or close (false) the streaming link. Persisted to n8.cfg. Always true: whether the link came up is
    // n8LinkConfig's `connected` and `error`.
    bool n8Connect(bool enabled) {
        host_.connect(enabled);
        return true;
    }
    // The timed-release lookahead the link thread applies, in ms. Persisted to n8.cfg. Always true.
    bool n8SetLookahead(std::int32_t ms) {
        host_.setLookahead(ms);
        return true;
    }

    // --- sessions (the CLI's) ---

    // The serial ports, each flagged when it is an N8 (USB VID:PID 38df:0017).
    std::vector<retroplug::N8PortDto> n8ListPorts();
    // Open `port`: `value` is the session's handle. A port that cannot be opened (absent, no permission, held by
    // another process where the OS refuses that, or the streaming link's) answers "linkDown", "cannot open serial
    // port: <port>".
    N8Reply n8Open(std::string port);
    // Close a session and its port. False for a handle that is not open.
    bool n8Close(std::int32_t handle);

    // Edio's primitives (n8/Edio.hpp), each on an open session. A handle that is not open answers "linkDown".
    N8Reply n8Handshake(std::int32_t handle, std::uint32_t timeoutMs);  // connect: `value` is its status byte
    N8Reply n8Status(std::int32_t handle);                              // `value` is the status byte
    N8Reply n8SysInfo(std::int32_t handle);                             // 64 bytes
    N8Reply n8Vdc(std::int32_t handle);                                 // 8 bytes
    N8Reply n8FreeSpace(std::int32_t handle);                           // `value` is the bytes free
    N8Reply n8DirMake(std::int32_t handle, std::string path);
    N8Reply n8FileDelete(std::int32_t handle, std::string path);
    N8Reply n8MemRD(std::int32_t handle, std::uint32_t addr, std::uint32_t size);
    N8Reply n8MemWR(std::int32_t handle, std::uint32_t addr, rfl::Bytestring data);
    N8Reply n8FifoWR(std::int32_t handle, rfl::Bytestring data);
    N8Reply n8FifoTxString(std::int32_t handle, std::string text);
    N8Reply n8ListDir(std::int32_t handle, std::string path);
    N8Reply n8FileOpen(std::int32_t handle, std::string path, std::int32_t mode);
    N8Reply n8FileRead(std::int32_t handle, std::uint32_t size);
    N8Reply n8FileWrite(std::int32_t handle, rfl::Bytestring data);
    N8Reply n8FileClose(std::int32_t handle);
    N8Reply n8ReadData(std::int32_t handle, std::uint32_t size, std::uint32_t timeoutMs);  // a raw read, no command
    N8Reply n8FlushInput(std::int32_t handle);
    N8Reply n8SetReadTimeout(std::int32_t handle, std::uint32_t ms);  // the session's Timeouts::readMs

    // The menu's commands (n8/Menu.hpp).
    N8Reply n8MenuTest(std::int32_t handle);
    N8Reply n8MenuInstall(std::int32_t handle, std::string path);  // `value` is the map index
    N8Reply n8MenuStart(std::int32_t handle);
    N8Reply n8MenuVramDump(std::int32_t handle);                   // 2048 VRAM bytes, then 16 palette bytes
    N8Reply n8MenuReset(std::int32_t handle);

    // The operations (n8/Ops.hpp).
    N8Reply n8ReadFile(std::int32_t handle, std::string path);
    N8Reply n8WriteFile(std::int32_t handle, std::string path, rfl::Bytestring data);
    // Boot `path` through the menu: the ROM uploaded there first when `upload`, the battery `srm` (when not empty)
    // written to its slot; `value` is the map index the install answered.
    N8Reply n8BootRom(std::int32_t handle, std::string path, rfl::Bytestring rom, rfl::Bytestring srm, bool upload);

private:
    struct Session;
    Session* find(std::int32_t handle);

    retroplug::N8Host&                                   host_;
    retroplug::N8Link::PortFactory                       factory_;
    retroplug::N8Host::PortLister                        lister_;
    std::map<std::int32_t, std::unique_ptr<Session>>     sessions_;
    std::int32_t                                         nextHandle_ = 0;
};

// Mount the facet on `server`: an rpcpp TypedRpcServer whose codec carries rfl::Bytestring, which the products' QuickJS
// codec does as a Uint8Array (the tests mount it on the same, test/QjsRpc.hpp) and the JSON codec cannot at all.
// `service` is the N8 provider's (rp_provider.rpc_service), an N8Rpc; null (the provider did not start) mounts nothing,
// so the UI reads the host as having no N8 seam. The one template of the facet: it adds the methods to a server, and
// the methods are the pack's library's.
template <class Server>
void registerRpc(Server& server, void* service) {
    if (!service) return;
    N8Rpc& rpc = *static_cast<N8Rpc*>(service);
    server.template addMethod<&N8Rpc::n8LinkConfig>(rpc);
    server.template addMethod<&N8Rpc::n8SetPort>(rpc);
    server.template addMethod<&N8Rpc::n8Connect>(rpc);
    server.template addMethod<&N8Rpc::n8SetLookahead>(rpc);

    server.template addMethod<&N8Rpc::n8ListPorts>(rpc);
    server.template addMethod<&N8Rpc::n8Open>(rpc);
    server.template addMethod<&N8Rpc::n8Close>(rpc);
    server.template addMethod<&N8Rpc::n8Handshake>(rpc);
    server.template addMethod<&N8Rpc::n8Status>(rpc);
    server.template addMethod<&N8Rpc::n8SysInfo>(rpc);
    server.template addMethod<&N8Rpc::n8Vdc>(rpc);
    server.template addMethod<&N8Rpc::n8FreeSpace>(rpc);
    server.template addMethod<&N8Rpc::n8DirMake>(rpc);
    server.template addMethod<&N8Rpc::n8FileDelete>(rpc);
    server.template addMethod<&N8Rpc::n8MemRD>(rpc);
    server.template addMethod<&N8Rpc::n8MemWR>(rpc);
    server.template addMethod<&N8Rpc::n8FifoWR>(rpc);
    server.template addMethod<&N8Rpc::n8FifoTxString>(rpc);
    server.template addMethod<&N8Rpc::n8ListDir>(rpc);
    server.template addMethod<&N8Rpc::n8FileOpen>(rpc);
    server.template addMethod<&N8Rpc::n8FileRead>(rpc);
    server.template addMethod<&N8Rpc::n8FileWrite>(rpc);
    server.template addMethod<&N8Rpc::n8FileClose>(rpc);
    server.template addMethod<&N8Rpc::n8ReadData>(rpc);
    server.template addMethod<&N8Rpc::n8FlushInput>(rpc);
    server.template addMethod<&N8Rpc::n8SetReadTimeout>(rpc);
    server.template addMethod<&N8Rpc::n8MenuTest>(rpc);
    server.template addMethod<&N8Rpc::n8MenuInstall>(rpc);
    server.template addMethod<&N8Rpc::n8MenuStart>(rpc);
    server.template addMethod<&N8Rpc::n8MenuVramDump>(rpc);
    server.template addMethod<&N8Rpc::n8MenuReset>(rpc);
    server.template addMethod<&N8Rpc::n8ReadFile>(rpc);
    server.template addMethod<&N8Rpc::n8WriteFile>(rpc);
    server.template addMethod<&N8Rpc::n8BootRom>(rpc);
}

}  // namespace rp::n8
