#pragma once

// The byte pipe the Edio protocol runs over (docs/modular-graph-phase-e2-design.md section 4): the desktop's serial
// port (n8/desktop/SerialTransport.hpp, over deps/serial), the Pico's TinyUSB CDC host, a test's fake cart. Edio knows
// nothing else about where its bytes go: what it takes from a transport is that a write answers how much it took, a
// read how much arrived, and that a link known to be down says so.
//
// Portable, as the protocol over it is: no exceptions (a transport that meets one, as deps/serial's do, catches it and
// answers short, keeping the text for its owner), and a protected non-virtual destructor, since a transport is never
// deleted through this interface and a virtual one makes the deleting destructor reach operator delete. A host that
// owns its transports through a pointer gives them a virtual destructor of its own (the desktop's PortTransport).

#include <cstddef>
#include <cstdint>

namespace rp::n8 {

class Transport {
public:
    // Hand the port `size` bytes; the bytes it took. Fewer than `size` is a failure (the port did not take them in its
    // own time, or the link went down), and Edio stops the command there: what follows a part-written command would be
    // parsed by the cart as the rest of it.
    virtual std::size_t write(const std::uint8_t* data, std::size_t size) = 0;
    // Up to `size` bytes into `buffer`, waiting up to `timeoutMs` for them (0: only what has already arrived); the
    // bytes that arrived, 0 on a timeout. A short read is not a failure: Edio reads again for the rest, and each read
    // waits its own timeout, so a long reply that keeps arriving never times out. A read blocks its caller until it
    // answers, so a transport whose bytes arrive on another context (the Pico's USB host, a browser's serial port)
    // pumps that context inside the wait, or runs the protocol on a worker it can block (a page: Atomics.wait or JSPI).
    virtual std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) = 0;
    // Drop whatever has arrived and not been read: a late byte would otherwise be taken for the next reply's first.
    // Whether it also waits for the line to go quiet is the transport's: the Pico's does (it drops until 20 ms pass
    // with nothing arriving), the desktop's drops only what the OS holds now. So a caller cannot count on a flush to
    // take the tail of a reply still in flight (Ops.hpp's flush after a failure), and a transport that can wait should.
    virtual void flushInput() = 0;
    // False while the link is known to be down (the Pico's CDC unmount): every wait fails at once rather than running
    // out its timeout against a device that has gone. Once false it stays false until the transport's owner binds it
    // again (the Pico's bind() after a remount), so a call that was mid-command when its device went cannot carry on
    // into a new one. A port that cannot tell (the desktop's) answers true.
    virtual bool linkUp() const { return true; }

protected:
    ~Transport() = default;
};

}  // namespace rp::n8
