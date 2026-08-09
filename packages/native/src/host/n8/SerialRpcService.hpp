#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/n8/WjwwoodSerialPort.hpp"

namespace retroplug {

// One discovered serial port for the TS serial client (mirrors N8PortInfo, trimmed to what TS needs).
struct SerialPortInfo {
    std::string port;
    bool        isN8 = false;
};

// The thin native serial byte-transport exposed to the TS control plane over the Backend RPC channel.
// It is the ONLY native piece of the N8 stack the TS side depends on: the Edio protocol framing, the
// on-device menu commands, and the ROM/save orchestration all live in TS (packages/retroplug/src/n8)
// on top of these six methods. A handle table owns the open serial ports; the CLI is single-threaded,
// so no locking is needed. Mounted CLI-only for now (registerSerialRpc, under RETROPLUG_N8_BRIDGE) -
// the SDL/plugin hosts pick it up in a later phase alongside their own connection manager.
class SerialRpcService {
public:
    // Enumerate serial ports, flagging Everdrive N8 units (USB VID:PID 38df:0017).
    std::vector<SerialPortInfo> serialListPorts();

    // Open a port (9600/8N1; baud is irrelevant on the N8's USB CDC). Returns an opaque handle (>= 0),
    // or -1 if the port can't be opened (absent / busy / permission).
    std::int32_t serialOpen(std::string port);

    // Write raw bytes to an open handle. Returns the number written, or -1 for an unknown handle.
    std::int32_t serialWrite(std::int32_t handle, rfl::Bytestring data);

    // Read up to `size` bytes, blocking up to `timeoutMs` (deps/serial's per-call total timeout). Returns
    // the bytes actually read - may be shorter, empty on timeout / unknown handle. TS loops to a deadline.
    rfl::Bytestring serialRead(std::int32_t handle, std::uint32_t size, std::int32_t timeoutMs);

    // Drop any buffered input on the handle. False for an unknown handle.
    bool serialFlush(std::int32_t handle);

    // Close + forget a handle. False if it wasn't open.
    bool serialClose(std::int32_t handle);

private:
    WjwwoodSerialPort* find(std::int32_t handle);

    std::unordered_map<std::int32_t, std::unique_ptr<WjwwoodSerialPort>> ports_;
    std::int32_t                                                        nextHandle_ = 0;
};

// Mount the serial facet onto an rpcpp server - the same cross-object addMethod pattern as
// BackendRpcRegistration.hpp. Header-only + templated on the server so it stays OUT of the shared
// registration union; only the CLI includes + calls it, so the plugin/test hosts never link `serial`.
template <class Server>
void registerSerialRpc(Server& s, SerialRpcService& svc) {
    s.template addMethod<&SerialRpcService::serialListPorts>(svc);
    s.template addMethod<&SerialRpcService::serialOpen>(svc);
    s.template addMethod<&SerialRpcService::serialWrite>(svc);
    s.template addMethod<&SerialRpcService::serialRead>(svc);
    s.template addMethod<&SerialRpcService::serialFlush>(svc);
    s.template addMethod<&SerialRpcService::serialClose>(svc);
}

}  // namespace retroplug
