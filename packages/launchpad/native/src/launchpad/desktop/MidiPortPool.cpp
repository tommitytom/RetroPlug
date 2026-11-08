#include "launchpad/desktop/MidiPortPool.hpp"

#include <exception>
#include <utility>

#include "launchpad/desktop/MidiPortFilter.hpp"

namespace rp::launchpad {

std::vector<int> MidiPortPool::openInputs(const std::vector<std::string>& names) {
    std::vector<int> handles;
    if (names.empty()) return handles;
    if (!opener_) {
        error_ = "no MIDI backend";
        return std::vector<int>(names.size(), 0);
    }
    // Raised BEFORE the opens, so the host's own stream has already let go of the inputs we are about to ask
    // for. Undone below if none of them opened and no other input is held.
    const bool  wasHeld = inputs_ > 0;
    std::string firstError;
    if (!wasHeld) heldEdge(true);
    for (const std::string& name : names) {
        handles.push_back(openOne(true, name));
        if (handles.back() == 0 && firstError.empty()) firstError = error_;
    }
    error_ = firstError;  // the first refusal, not whichever port happened to be last
    if (!wasHeld && inputs_ == 0) heldEdge(false);
    return handles;
}

int MidiPortPool::openOutput(const std::string& name) {
    if (!opener_) {
        error_ = "no MIDI backend";
        return 0;
    }
    return openOne(false, name);
}

int MidiPortPool::openOne(bool input, const std::string& name) {
    if (!opener_) {
        error_ = "no MIDI backend";
        return 0;
    }
    // The test-only allow list (MidiPortFilter.hpp) is refused here as well as by the opener, so a scan of a filtered
    // process cannot reach a port the process was not given, whatever backend the product injected.
    if (!midiPortAllowed(name)) {
        error_ = "MIDI port not allowed: " + name;
        return 0;
    }
    Entry e;
    e.input = input;
    e.queue = std::make_shared<Queue>();
    try {
        IMidiPort::Receiver receiver;
        if (input) {
            receiver = [q = e.queue](const std::uint8_t* data, std::size_t n) {  // backend callback thread
                if (!data || n == 0) return;
                std::lock_guard<std::mutex> lk(q->m);
                if (q->messages.size() >= kMaxQueued) {
                    q->dropped++;
                    return;
                }
                q->messages.emplace_back(data, data + n);
            };
        }
        e.port = opener_(input, name, std::move(receiver));
        if (!e.port) error_ = "MIDI port unavailable";
    } catch (const std::exception& ex) {
        error_ = ex.what();
        e.port.reset();
    }
    if (!e.port) return 0;
    error_.clear();
    const int handle = next_++;
    ports_.emplace(handle, std::move(e));
    if (input) inputs_++;
    return handle;
}

bool MidiPortPool::send(int handle, const std::uint8_t* data, std::size_t n) {
    auto it = ports_.find(handle);
    if (it == ports_.end() || it->second.input || !data || n == 0) return false;
    it->second.port->send(data, n);
    return true;
}

std::vector<MidiPortPool::Message> MidiPortPool::read(int handle) {
    std::vector<Message> out;
    auto                 it = ports_.find(handle);
    if (it == ports_.end() || !it->second.input) return out;
    std::lock_guard<std::mutex> lk(it->second.queue->m);
    out.swap(it->second.queue->messages);
    return out;
}

std::uint64_t MidiPortPool::dropped(int handle) const {
    auto it = ports_.find(handle);
    if (it == ports_.end() || !it->second.input) return 0;
    std::lock_guard<std::mutex> lk(it->second.queue->m);
    return it->second.queue->dropped;
}

void MidiPortPool::close(int handle) {
    auto it = ports_.find(handle);
    if (it == ports_.end()) return;
    const bool input = it->second.input;
    ports_.erase(it);  // the port is destroyed here, cancelling its callback
    if (input && --inputs_ == 0) heldEdge(false);
}

void MidiPortPool::closeAll() {
    const bool hadInputs = inputs_ > 0;
    ports_.clear();
    inputs_ = 0;
    if (hadInputs) heldEdge(false);
}

}  // namespace rp::launchpad
