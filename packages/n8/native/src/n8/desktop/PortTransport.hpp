#pragma once

// The desktop's transports (n8/Transport.hpp) as its hosts hold them: owned through a pointer (N8Link's port factory,
// the facet's sessions), so with a virtual destructor of their own, and keeping the words of a failure that Edio's
// Status cannot carry. deps/serial reports a failed port by throwing, and a transport answers short instead (Edio
// throws nothing and runs on the device too), so SerialTransport catches the exception and keeps its text here: the
// text N8Link puts in its lastError (Settings > N8 shows it) and a job its failure, as when the throw reached them.

#include <string>
#include <string_view>

#include "n8/Edio.hpp"
#include "n8/Transport.hpp"

namespace retroplug {

class PortTransport : public rp::n8::Transport {
public:
    virtual ~PortTransport() = default;

    // What the port said when its last call failed in the port itself (a deps/serial exception's text), or "" when
    // that call succeeded or only ran out its time. Each call sets it afresh, so after an Edio call that failed it is
    // about the transport call the Edio call stopped at; after one refused before it reached the port (portSaid) it is
    // about an earlier call's.
    virtual std::string failure() const { return {}; }
};

// What the port said about `status`'s call: its failure() words, or "" when the port did not fail, or when the call was
// refused before it made a transport call (a string past its length prefix, a battery slot that does not fit, a size
// past the 32-bit count), where failure() is still the previous call's.
inline std::string portSaid(const rp::n8::Status& status, const PortTransport& port) {
    const bool refusedFirst = status.code == rp::n8::Code::TooLarge &&
                              (status.where == rp::n8::Where::Length || status.where == rp::n8::Where::BatteryPath ||
                               status.where == rp::n8::Where::Size);
    return refusedFirst ? std::string() : port.failure();
}

// The text of a failed Edio call on the desktop: the port's own words when the port failed, else describe()'s.
inline std::string failureText(const rp::n8::Status& status, const PortTransport& port, std::string_view path = {}) {
    if (std::string said = portSaid(status, port); !said.empty()) return said;
    char text[512];
    rp::n8::describe(status, text, sizeof text, path);
    return text;
}

// The same for an Edio a desktop host built, which it always builds over a PortTransport (N8Link's port, a session's).
inline std::string failureText(const rp::n8::Status& status, rp::n8::Edio& edio, std::string_view path = {}) {
    return failureText(status, static_cast<const PortTransport&>(edio.transport()), path);
}

}  // namespace retroplug
