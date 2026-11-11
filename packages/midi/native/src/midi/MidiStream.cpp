#include "midi/MidiStream.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <utility>

#include "midi/PortFilter.hpp"

namespace rp::midi {
namespace {

/** Whether `names` holds `name` (the selection's own small list, so a duplicate selection opens one port). */
bool contains(const std::vector<std::string>& names, const std::string& name) {
    for (const std::string& n : names)
        if (n == name) return true;
    return false;
}

}  // namespace

MidiStream::MidiStream(MidiBackend& backend)
    : backend_(&backend), clientName_(backend.clientName()), filterName_(backend.familyName()) {}

MidiStream::~MidiStream() { close(); }

bool MidiStream::open() {
    close();  // idempotent: a second open replaces the first, ports and ring alike
    clientName_ = backend_->clientName();
    filterName_ = backend_->familyName();
    log_.store(std::getenv("RETROPLUG_MIDI_LOG") != nullptr);  // set before a callback thread can start
    if (!backend_->usable()) return false;
    if (backend_->supportsVirtual()) {
        // A backend with no virtual ports says so (MidiBackend::supportsVirtual) rather than attempting one that cannot
        // exist: WinMM's openVirtualPort raises a warning and returns, so the old code announced two ports that were
        // not there. Linux and macOS are unchanged: the line is the one the harnesses read.
        try {
            virtualIn_ =
                backend_->openVirtualInput([this](const std::uint8_t* data, std::size_t n) { pushRing(data, n); });
            virtualOut_ = backend_->openVirtualOutput();
        } catch (const std::exception& e) {
            emit(std::string("MIDI unavailable: ") + e.what() + " (running without MIDI)");
            virtualIn_.reset();
            virtualOut_.reset();
            return false;
        }
        emit("MIDI: virtual ports '" + clientName_ + " In' / '" + clientName_ + " Out' open");
    }
    open_ = true;
    applyInputs(Apply::Reopen);    // nothing was open: this is the change, and the lines say what it opened
    applyOutputs(Apply::Reopen);
    return true;
}

void MidiStream::close() {
    // Nothing is published any more (not even an empty set: the virtual output is freed below, and a set naming it
    // would let a send that starts after the publish reach a freed port), and the send in flight is waited out here.
    OutputSet* old = outputs_.exchange(nullptr, std::memory_order_seq_cst);
    retire(old);
    openOutputs_.clear();
    hwOut_.clear();      // closed only now: no published set named them, so no send can be inside one
    virtualIn_.reset();  // cancels its callback: no receiver runs after this returns
    hwIn_.clear();       // each port cancels its own callback as it goes
    openInputs_.clear();
    virtualOut_.reset();
    open_ = false;
    // The ring's indices are NOT reset: poll() runs on the audio thread and may be between its load of head_ and its
    // store of it, so a reset here would have it store a stale index over the empty ring and replay moved-from slots.
    // They are consistent as they stand - a message that arrived before the close is still one that arrived.
}

std::vector<std::string> MidiStream::listInputs() const {
    const std::vector<std::string> raw = backend_->listInputs();
    std::vector<std::string>       out;
    for (std::size_t i : hardwarePortIndices(raw, filterName_)) out.push_back(raw[i]);
    return out;
}

std::vector<std::string> MidiStream::listOutputs() const {
    const std::vector<std::string> raw = backend_->listOutputs();
    std::vector<std::string>       out;
    for (std::size_t i : hardwarePortIndices(raw, filterName_)) out.push_back(raw[i]);
    return out;
}

void MidiStream::setInputs(const std::vector<std::string>& names, bool all, Apply mode) {
    selectedIn_ = names;
    allInputs_  = all;
    if (open_) applyInputs(mode);
}

void MidiStream::setOutputs(const std::vector<std::string>& names, Apply mode) {
    selectedOut_ = names;
    if (open_) applyOutputs(mode);
}

void MidiStream::setReserved(const std::string& input, const std::string& output) {
    if (reservedIn_ == input && reservedOut_ == output) return;
    reservedIn_  = input;
    reservedOut_ = output;
    if (!open_) return;
    applyInputs(Apply::Diff);   // diffed: the claim changes what is open, and nothing else moves
    applyOutputs(Apply::Diff);
}

void MidiStream::setHardwareInputsSuspended(bool suspended) {
    if (inputsSuspended_ == suspended) return;
    inputsSuspended_ = suspended;
    if (open_) applyInputs(Apply::Diff);
    emit(std::string("MIDI in: hardware inputs ") +
         (suspended ? "suspended for a control-surface scan" : "resumed"));
}

void MidiStream::applyInputs(Apply mode) {
    const std::vector<std::string> names = backend_->listInputs();
    std::vector<std::string>       want;
    for (std::size_t i : inputPortsToOpen(names, filterName_, selectedIn_, allInputs_, reservedIn_, inputsSuspended_))
        want.push_back(names[i]);
    if (mode == Apply::Diff && want == openInputs_) return;
    // Said once per real change: it describes the state the stream is now in, not a call it answered.
    if (want.empty() && selectedIn_.empty() && !allInputs_ && !inputsSuspended_)
        emit("MIDI in: no hardware device selected (Settings > MIDI)");
    // A port the change keeps stays open, so a project load costs no connection (and a clock arriving on it loses no
    // message). A REOPEN lets every port go first and only then opens it again: a MIDI input is exclusive on the one
    // platform where a re-pick is the remedy for an unplugged device (Windows), so opening before closing is refused.
    if (mode == Apply::Reopen) {
        hwIn_.clear();  // each port cancels its own callback in its destructor, so no receiver runs after this
        openInputs_.clear();
    } else {
        std::vector<std::pair<std::string, std::unique_ptr<MidiPort>>> kept;
        for (const std::string& name : want)
            for (auto& entry : hwIn_)
                if (entry.first == name) {
                    kept.push_back(std::move(entry));
                    break;
                }
        hwIn_ = std::move(kept);  // the rest are closed here, once the selection no longer names them
        openInputs_.clear();
    }
    for (const std::string& name : want) {
        bool held = false;
        for (const auto& entry : hwIn_)
            if (entry.first == name) {
                held = true;
                break;
            }
        if (held) continue;
        try {
            hwIn_.emplace_back(name, backend_->openInput(name, [this](const std::uint8_t* data, std::size_t n) {
                                   pushRing(data, n);
                               }));
            emit("MIDI in: connected hardware port '" + name + "'");
        } catch (const std::exception& e) {
            emit("MIDI: input open failed for '" + name + "': " + e.what());
        }
    }
    for (const std::string& name : want)  // what OPENED, in the selection's order (a name that refused is missing)
        for (const auto& entry : hwIn_)
            if (entry.first == name) {
                openInputs_.push_back(name);
                break;
            }
}

void MidiStream::applyOutputs(Apply mode) {
    const std::vector<std::string> names = backend_->listOutputs();
    std::vector<std::string>       want;
    for (const std::string& sel : selectedOut_) {
        if (sel.empty()) continue;
        if (!reservedOut_.empty() && sel == reservedOut_) {
            emit("MIDI out: '" + sel + "' is held by the control surface");
            continue;
        }
        if (matchPortIndex(names, filterName_, sel)) {
            if (!contains(want, sel)) want.push_back(sel);
        } else {
            emit("MIDI out: selected port '" + sel + "' not present");
        }
    }
    std::vector<std::string> keep;  // what stays open: a diff's unchanged names, and none at all under a reopen
    if (mode == Apply::Diff)
        for (const std::string& name : openOutputs_)
            if (contains(want, name)) keep.push_back(name);
    if (mode == Apply::Diff && keep == openOutputs_ && keep.size() == want.size()) return;

    if (mode == Apply::Reopen) {
        // A port being asked for again has to be LET GO first: opening a name the stream still holds is refused where a
        // port is exclusive (a WinMM output), which is how the second open used to lose the device the re-pick meant to
        // heal. Nothing is published while that happens, so a send in the gap reaches no hardware output - the price of
        // the re-pick, and the reason a project load diffs instead.
        publishOutputs({});  // waits out the send in flight, so no send is inside a port when it is closed
        hwOut_.clear();
        openOutputs_.clear();
    }
    // A port the selection ADDS opens while the set it is joining is still published, so a send in that gap reaches the
    // ports the change keeps and nothing goes nowhere.
    std::vector<std::string> next = keep;
    for (const std::string& name : want) {
        if (contains(next, name)) continue;
        try {
            hwOut_.emplace_back(name, backend_->openOutput(name));
            next.push_back(name);
            emit("MIDI out: connected hardware port '" + name + "'");
        } catch (const std::exception& e) {
            emit("MIDI: output open failed for '" + name + "': " + e.what());
        }
    }
    openOutputs_ = next;
    publishOutputs(openOutputs_);
    // Only now are the ports the selection dropped closed: the set just published no longer names them, and the send
    // that could still have been inside one has been waited out.
    for (auto it = hwOut_.begin(); it != hwOut_.end();) {
        if (contains(openOutputs_, it->first)) {
            ++it;
            continue;
        }
        it = hwOut_.erase(it);  // closes it
    }
}

void MidiStream::publishOutputs(const std::vector<std::string>& open) {
    auto next         = std::make_unique<OutputSet>();
    next->virtualPort = virtualOut_.get();  // carried, never reopened: see the class comment
    for (const std::string& name : open)
        for (const auto& entry : hwOut_)
            if (entry.first == name) {
                next->ports.push_back(entry.second.get());
                break;
            }
    OutputSet* old = outputs_.exchange(next.release(), std::memory_order_seq_cst);
    retire(old);  // waits out the send in flight, which is the only thing that could still be inside the old set
}

void MidiStream::waitForSends() {
    // The wait is for the ONE send in flight, so it is bounded by a send and never by a run of them: the counter is
    // read once, and an odd value is waited out until it CHANGES (the in-flight send has exited). A send that starts
    // afterwards has read the newly published set, so it is no reason to wait; waiting "while odd" would instead chase
    // every send that follows, which under back-to-back sends is unbounded.
    const std::uint32_t seen = epoch_.load(std::memory_order_seq_cst);
    if ((seen & 1u) == 0) return;
    // A few yields first - a send is usually short - then short sleeps: a WinMM bulk SysEx blocks about 170 ms inside
    // send(), and 170 ms of a spinning UI thread is a visible stall, where a sleeping one is not.
    for (int spin = 0; epoch_.load(std::memory_order_seq_cst) == seen; ++spin) {
        if (spin < 64) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void MidiStream::retire(OutputSet* old) {
    if (!old) return;
    // The new set is published before this, so a send either sees it or is still inside the old one; waiting out that
    // send is all this needs. The orders are seq_cst because this is the store-buffering (Dekker) shape: send() writes
    // the counter and then reads the set, while this writes the set and then reads the counter. Under acquire/release
    // the two reordered store-load pairs let send() read the OLD set while this reads an even counter, and the old
    // ports are then freed under a send still using them. x86 hides that (its `lock xadd` is a full fence) and
    // ThreadSanitizer cannot see it either (it checks happens-before, not weak-memory reordering); AArch64, which the
    // handheld targets are, does not hide it. The four operations on the two locations are therefore seq_cst, which is
    // free on x86.
    waitForSends();
    delete old;
}

void MidiStream::pushRing(const std::uint8_t* data, std::size_t len) {
    if (!data || len == 0) return;
    {
        std::lock_guard<std::mutex> lock(pushMutex_);  // one producer at a time (see the class comment)
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t n = (t + 1) % kCapacity;
        if (n == head_.load(std::memory_order_acquire)) {  // full → drop
            dropped_.fetch_add(1, std::memory_order_relaxed);  // counted, so a lost clock isn't invisible
            return;
        }
        ring_[t].seq = ++seq_;
        ring_[t].bytes.assign(data, data + len);
        tail_.store(n, std::memory_order_release);
    }

    if (log_.load()) {
        std::string line = "MIDI in: ";
        char        hex[4];
        for (std::size_t i = 0; i < len; ++i) {
            std::snprintf(hex, sizeof(hex), "%02X ", data[i]);
            line += hex;
        }
        emit(line);
    }
}

void MidiStream::poll(std::vector<Message>& out) {
    out.clear();
    std::size_t       h = head_.load(std::memory_order_relaxed);
    const std::size_t t = tail_.load(std::memory_order_acquire);
    while (h != t) {
        out.push_back(std::move(ring_[h]));
        h = (h + 1) % kCapacity;
    }
    head_.store(h, std::memory_order_release);
}

void MidiStream::send(const std::uint8_t* data, std::size_t n) {
    if (!data || n == 0) return;
    if (log_.load()) {
        std::string line = "MIDI out: ";
        char        hex[4];
        for (std::size_t i = 0; i < n; ++i) {
            std::snprintf(hex, sizeof(hex), "%02X ", data[i]);
            line += hex;
        }
        emit(line);
    }
    // The bracket covers the virtual output and the published set alike, so close() waits both out. ONE caller: the
    // audio thread. A second one would be a bug of the same class as freeing a port under a send - its entry would read
    // an odd counter and make it even again while the first caller is still inside a port, and a swap landing between
    // them would free that port. The counter is the invariant's only witness, so the entry checks it where a build
    // keeps its assertions.
    const std::uint32_t was = epoch_.fetch_add(1, std::memory_order_seq_cst);  // odd: a send is in flight
    (void)was;
    assert((was & 1u) == 0 && "MidiStream::send has one caller (the audio thread)");
    OutputSet* set = outputs_.load(std::memory_order_seq_cst);
    if (set) {
        if (set->virtualPort) set->virtualPort->send(data, n);
        for (auto& port : set->ports) port->send(data, n);
    }
    epoch_.fetch_add(1, std::memory_order_seq_cst);  // even: done, and everything above happens-before a retire
}

void MidiStream::emit(const std::string& text) {
    // The prefix the app has always written: the CLI prints it too, and three harness patterns grep past it.
    const std::string line = "[retroplug-sdl] " + text;
    if (sink_) {
        sink_(line);
        return;
    }
    std::fprintf(stderr, "%s\n", line.c_str());
}

}  // namespace rp::midi
