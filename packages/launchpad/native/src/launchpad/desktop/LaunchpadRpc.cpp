#include "launchpad/desktop/LaunchpadRpc.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "launchpad/desktop/MidiPortFilter.hpp"

namespace rp::launchpad {
namespace {

// rfl::Bytestring is a vector of std::byte; the link and the pool speak std::uint8_t. One conversion each way, so no
// call site has to know.
std::vector<std::uint8_t> bytesOf(const rfl::Bytestring& bytes) {
    const auto* at = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return std::vector<std::uint8_t>(at, at + bytes.size());
}

rfl::Bytestring byteStringOf(const std::vector<std::uint8_t>& bytes) {
    const auto* at = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(at, at + bytes.size());
}

LaunchpadStatusDto statusOf(ControllerLink& link, const std::string& claimError) {
    LaunchpadStatusDto dto;
    dto.connected  = link.isConnected();
    dto.generation = link.generation();
    dto.sent       = link.messagesSent();
    dto.dropped    = link.dropped();
    dto.input      = link.inputName();
    dto.output     = link.outputName();
    dto.error      = !claimError.empty() ? claimError : link.lastError();
    return dto;
}

}  // namespace

LaunchpadRpc::LaunchpadRpc(ControllerLink& link, MidiPortPool& pool, const rp_device_host* host, PortLister lister)
    : link_(link), pool_(pool), host_(host), lister_(std::move(lister)) {}

LaunchpadRpc::~LaunchpadRpc() = default;

void LaunchpadRpc::reserve(const std::string& input, const std::string& output) {
    // The product keeps the claimed pair out of its shared MIDI stream (a second reader would answer the scan's probes
    // and take the user's pad presses as music). Empty names undo it. A host with no shared stream answers true; a
    // refusal only means the pair is also in the stream, which is a menu complaint, not a lost device.
    if (host_ && host_->reserve_midi) host_->reserve_midi(host_->ctx, input.c_str(), output.c_str());
}

LaunchpadPortsDto LaunchpadRpc::launchpadPorts() {
    LaunchpadPortsDto ports = lister_ ? lister_() : LaunchpadPortsDto{};
    // One machine's view whichever lister this is, which is what a scan's sweep and both port pickers read: RetroPlug's
    // own virtual ports (the shared stream's client, and this pack's `RetroPlug Launchpad` link ports, so a second
    // instance's claimed pair is not taken for a device) and ALSA's "Midi Through" are dropped exactly as
    // MidiIo::listInputs() drops them. A scan writes a Device Inquiry out of every output it is handed, so a name that is
    // not a device must not reach one. This is NOT a filter to Launchpad-like names: a Launchpad on a TRS cable arrives
    // on an interface's port, named after the interface.
    ports.inputs  = allowedMidiPorts(ports.inputs);
    ports.outputs = allowedMidiPorts(ports.outputs);
    return ports;
}

LaunchpadStatusDto LaunchpadRpc::launchpadStatus() {
    return statusOf(link_, error_);
}

LaunchpadStatusDto LaunchpadRpc::launchpadClaim(std::string input, std::string output, rfl::Bytestring closeBytes) {
    error_.clear();
    // The OLD pair goes first, while its ports and its claim are still held: it is closed (with the bytes that unlock
    // its front panel), its cross-process claims are given back and it rejoins the shared stream. Only then is the new
    // one picked up.
    //
    // The reservation moves STRAIGHT to the new pair, with no empty reservation in between: the product sees one change of
    // pair, so a swap costs ONE audio restart, and A's ports never come back into the shared stream for the span between
    // two reservations. Emptying it here would also be wrong on the way in: the old pair is already closed above, so an
    // empty reservation would reopen its ports only to close them again. A failure below hands the reservation back from
    // its own `fail`, which is the only place an empty pair belongs.
    link_.disconnect();
    claimIn_.reset();
    claimOut_.reset();
    const auto fail = [&](std::string why) {
        reserve("", "");
        error_ = std::move(why);
        return statusOf(link_, error_);
    };
    if (input.empty() || output.empty()) return fail("no port chosen");
    // Both ports must be HERE before anything is reserved: a stale or unplugged pick would otherwise reserve, fail to
    // open and hand back - two reopens of the shared stream, and two audio restarts, for nothing. It is the SAME list the
    // pickers and a scan read, filter included: a name that is not a device at all is not connected here either.
    if (lister_) {
        const LaunchpadPortsDto ports = launchpadPorts();
        if (std::find(ports.inputs.begin(), ports.inputs.end(), input) == ports.inputs.end())
            return fail("not connected: " + input);
        if (std::find(ports.outputs.begin(), ports.outputs.end(), output) == ports.outputs.end())
            return fail("not connected: " + output);
    }
    // Then the cross-process claims, on BOTH ports: two instances with different inputs must not share one output
    // either. Held before anything opens, so a refusal opens nothing. An interface whose input and output are one
    // name (ALSA's "client:port N:M", CoreMIDI's endpoint, WinMM's "LPProMK3 MIDI" both ways) is ONE claim: a second
    // claim of the same name is refused by our own lock, which would make the ordinary device unclaimable.
    if (claim_) {
        claimIn_  = claim_(input);
        claimOut_ = output == input ? claimIn_ : claim_(output);
        if (!claimIn_ || !claimOut_) {
            claimIn_.reset();
            claimOut_.reset();
            // Held by another RetroPlug - and so still kept OUT of this one's shared stream. Giving it back here would
            // let this process hear every pad press the other one is taking as raw notes (a row launch in this cart
            // too), and mirror its own MIDI out onto the other one's pads.
            reserve(input, output);
            error_ = "in use by another RetroPlug";
            return statusOf(link_, error_);
        }
    }
    // BEFORE the open: the shared stream must have let go of both (a Windows input it still held could not be opened
    // at all), and what the stream holds is the product's to change (it may stop audio for it).
    reserve(input, output);
    if (!link_.connect(input, output, bytesOf(closeBytes))) {
        claimIn_.reset();
        claimOut_.reset();
        return fail(link_.lastError());
    }
    return statusOf(link_, error_);
}

LaunchpadStatusDto LaunchpadRpc::launchpadRelease() {
    // The close bytes go out first: the device's front panel is locked until they do, and the claim keeping it out of
    // the shared stream ends after the port is closed.
    link_.disconnect();
    reserve("", "");
    claimIn_.reset();
    claimOut_.reset();
    error_.clear();
    return statusOf(link_, error_);
}

bool LaunchpadRpc::launchpadSend(rfl::Bytestring data) {
    const std::vector<std::uint8_t> bytes = bytesOf(data);
    return link_.send(bytes.data(), bytes.size());
}

bool LaunchpadRpc::launchpadRestored() {
    // The rendezvous's end (docs/modular-graph-phase-f3-design.md section 3): the control plane read its selection,
    // claimed the pair and is now ready, so a product that held MIDI and audio for the claim starts them. The provider
    // is the only one that can tell the product (rp_device_host::restored), which is why a product names no class of
    // this pack. Always true: a product that holds nothing has nothing to resume.
    if (host_ && host_->restored) host_->restored(host_->ctx);
    return true;
}

std::vector<std::int32_t> LaunchpadRpc::launchpadOpenInputs(std::vector<std::string> names) {
    const std::vector<int> handles = pool_.openInputs(names);
    std::vector<std::int32_t> out;
    out.reserve(handles.size());
    for (int h : handles) out.push_back(h);
    return out;
}

std::int32_t LaunchpadRpc::launchpadOpenOutput(std::string name) {
    return pool_.openOutput(name);
}

bool LaunchpadRpc::launchpadPortSend(std::int32_t handle, rfl::Bytestring data) {
    const std::vector<std::uint8_t> bytes = bytesOf(data);
    return pool_.send(handle, bytes.data(), bytes.size());
}

LaunchpadReadDto LaunchpadRpc::launchpadPortRead(std::int32_t handle) {
    LaunchpadReadDto dto;
    for (auto& message : pool_.read(handle)) dto.messages.push_back(byteStringOf(message));
    dto.dropped = pool_.dropped(handle);
    return dto;
}

bool LaunchpadRpc::launchpadPortClose(std::int32_t handle) {
    pool_.close(handle);
    return true;
}

std::string LaunchpadRpc::launchpadPortError() {
    return pool_.lastError();
}

}  // namespace rp::launchpad
