#pragma once

// The host's one MIDI stream (docs/modular-graph-phase-f5b-design.md section 4.2): the port policy over a MidiBackend,
// so the SDL app's audio loop and the CLI's facet open ports through the same code and a test injects a fake. A change
// of selection publishes a new output set and opens or closes inputs, and neither stops audio.
//
// Threading. Inputs deliver on the backend's own callback threads - the virtual input plus one per connected hardware
// port - so the receive ring has MANY producers and one consumer (the audio thread, which drains it via poll() at block
// start, never allocating). The producers serialize on pushMutex_, which makes the ring single-producer from the
// consumer's side; poll() never takes the lock, so the audio thread stays lock-free.
//
// Outputs are the hazard, because send() calls a port the control thread may be replacing: the open outputs are an
// immutable set reached through one atomic pointer, a change publishes a new one, waits out the send in flight, and
// only then frees the old one (section 4.3). The wait is an epoch handshake: send() bumps a counter to an odd value on
// entry and an even one on exit, and the swapping thread reads it after publishing and, when it read an odd value,
// waits until the counter has CHANGED - so it waits out the one send in flight, never the stream of sends that follows.
// send() is the AUDIO THREAD's alone: that is what makes the parity a valid "a send is in flight" mark, and a second
// concurrent caller would break it (its entry would make the counter even while the first is still inside a port), so
// the entry is asserted. A block runs at most one send per pending message, so the wait is at most about one block -
// none when audio is not running, since the counter is even then.
//
// Every operation that frees a port first publishes what replaces it and then waits, so close() is safe beside a send
// too: send() reaches the virtual output through the same published set (its pointer travels in it), and close() frees
// it only after that wait, so no send is inside it. The virtual port's LIFETIME is the stream's open(), not the set's:
// a selection change publishes a new set naming the same virtual port without reopening it, so a DAW or an aconnect
// user connected to "RetroPlug Out" keeps that connection across a Settings > MIDI change. The hardware ports are the
// CONTROL thread's (hwOut_), and a published set only names them: a port leaving the selection is unpublished, waited
// out, and only then closed, which is also what lets a name be reopened at all where a port is exclusive (Windows).
//
// The filters judge a listing by the backend's FAMILY (MidiBackend::familyName), not by the name this stream publishes
// under: a session's stream is "RetroPlug N8" and a new name for it would otherwise turn the standalone's virtual output
// and a claimed Launchpad's link into devices it may open (the E11 "every input" default makes that reachable).
//
// Logging: every line the harnesses grep goes through one sink, by default the "[retroplug-sdl] " voice to stderr that
// the app has always written (the CLI prints it too), so a test asserts the very bytes that reach the console.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "midi/MidiBackend.hpp"

namespace rp::midi {

/** How a selection change treats the ports it names. Diff re-opens only what the change moved, which is what a project
 *  load does: a port that stays selected keeps its connection (a clock arriving on it loses nothing, and on Windows,
 *  where a MIDI port is exclusive, it is not lost to a second open). Reopen closes and opens every port the selection
 *  names, which is the user's one remedy for an unplugged device (there is no hotplug watch): Settings > MIDI's picker
 *  asks for it, and so does the Rescan row beside it. */
enum class Apply { Diff, Reopen };

class MidiStream {
public:
    struct Message {
        std::uint64_t             seq = 0;  // monotonic arrival order; a frame offset is derived later
        std::vector<std::uint8_t> bytes;
    };

    /** The receive ring's size: the most messages one poll() returns. Public so the consumer's scratch can be reserved
     *  to it, and poll() then never grows a vector on the audio thread. */
    static constexpr std::size_t kCapacity = 256;

    explicit MidiStream(MidiBackend& backend);
    ~MidiStream();
    MidiStream(const MidiStream&)            = delete;
    MidiStream& operator=(const MidiStream&) = delete;

    /** The line sink: what would be written to stderr, one line at a time, prefix included. A test installs one to hold
     *  the bytes a harness greps; the default writes them to stderr. */
    using LogSink = std::function<void(const std::string&)>;
    void setLogSink(LogSink sink) { sink_ = std::move(sink); }

    /** Open the virtual "<clientName> In" / "<clientName> Out" ports where the backend supports them, then apply the
     * current selection. The client name is the backend's (MidiBackend::clientName): one owner, so the ports opened and
     * the ports published cannot be named differently. False when the backend refuses (no MIDI system): the host then
     * runs without MIDI. */
    bool open();
    /** Free every port and empty the ring. Safe beside a send: the set is unpublished and waited out, then the virtual
     *  output, whose sends the same handshake covers. */
    void close();
    bool isOpen() const { return open_; }

    /** The input selection: EXACT names (TypeScript matched them, design 6.3). `all` opens every hardware input and
     * `names` is then ignored; an empty list with `all` false is None, the default. Applied at once, by `mode`: a name
     * that is not present is skipped and re-applied by the next call. */
    void setInputs(const std::vector<std::string>& names, bool all, Apply mode = Apply::Diff);
    /** The outputs to mirror send() to; empty is the virtual port only, the default. Applied by `mode`, as setInputs
     *  is. */
    void setOutputs(const std::vector<std::string>& names, Apply mode = Apply::Diff);

    /** Keep a port pair OUT of this stream because another owner holds it (the control surface's link). Empty releases.
     *  The input matters everywhere - a pad NoteOn here is a second, unquantised row launch - and so does the output:
     *  notes meant for a synth would land on the surface and light its pads at random. On Windows both are also
     *  exclusive, so a port this stream holds could not be claimed at all. Neither call stops audio. */
    void               setReserved(const std::string& input, const std::string& output);
    const std::string& reservedInput() const { return reservedIn_; }
    const std::string& reservedOutput() const { return reservedOut_; }

    /** Close every hardware INPUT for the duration of a control-surface SCAN, without disturbing the user's selection
     *  (resuming re-applies it). Not the same lever as setReserved, which names ONE pair a surface holds for as long as
     *  it is connected: a scan needs every input, briefly. A product reaches this only where scanSuspendsInputs() says
     *  the edge is real, which is Windows alone. Neither call stops audio. */
    void setHardwareInputsSuspended(bool suspended);
    bool hardwareInputsSuspended() const { return inputsSuspended_; }

    /** The pickers' lists: the hardware ports only (our own virtual ports, ALSA's "Through" and anything the test-only
     *  allow list hides are not ports a user may pick). A port a control surface holds IS listed: the reservation only
     *  governs what this stream opens, and hiding it would make the list move whenever a Launchpad connects. */
    std::vector<std::string> listInputs() const;
    std::vector<std::string> listOutputs() const;
    bool                     supportsVirtual() const { return backend_->supportsVirtual(); }

    /** What actually OPENED, which is not the same as what was selected: a name the machine does not list, a port a
     *  control surface holds and one that refused to open are all missing here. The UI's "(missing)" entries and its
     *  Rescan row read this rather than inferring it from a listing taken at another moment. */
    const std::vector<std::string>& openedInputs() const { return openInputs_; }
    const std::vector<std::string>& openedOutputs() const { return openOutputs_; }

    /** Drain queued input into `out` (the audio thread, each block; clears `out` first). Lock-free with respect to the
     *  backend's callback threads. Reserved to kCapacity up front, this never allocates. */
    void poll(std::vector<Message>& out);
    /** Send one short message out (the audio thread each block - MI.OUT etc.): the virtual output and every selected
     *  hardware output. No-op when there is no output. */
    void send(const std::uint8_t* data, std::size_t n);
    /** Messages dropped because the ring was full since startup - always 0 in a healthy run. Non-zero means the audio
     *  thread stopped draining, and any transport clock in what was lost is simply gone, which downstream reads as a
     *  tempo that is too slow. */
    std::uint64_t droppedCount() const { return dropped_.load(std::memory_order_relaxed); }

    /** Test-only: push a message into the input ring as if a port had received it, for a headless self-test that has no
     *  MIDI hardware. Not used in normal operation. */
    void injectForTest(const std::uint8_t* data, std::size_t len) { pushRing(data, len); }

private:
    /** What send() reaches, immutable once published: the hardware outputs this set NAMES, and the virtual output
     *  (owned by open(), carried here only so a send reads ONE pointer and close() can wait sends out before freeing
     *  it). It owns no port: hwOut_ is the hardware ports' owner, so a port can be unpublished, waited out and closed
     *  before the same name is opened again. One control-thread allocation per change, nothing on the audio path. */
    struct OutputSet {
        std::vector<MidiPort*> ports;
        MidiPort*              virtualPort = nullptr;
    };

    void applyInputs(Apply mode);
    void applyOutputs(Apply mode);
    void publishOutputs(const std::vector<std::string>& names);  // builds the set from hwOut_ and retires the last
    void waitForSends();          // spins out the send in flight, and only that one
    void retire(OutputSet* old);  // waits out a send in flight, then frees the set (never a port: hwOut_'s)
    void pushRing(const std::uint8_t* data, std::size_t len);
    void emit(const std::string& line);  // one log line, prefix included, through sink_ or stderr

    MidiBackend* backend_;  // non-const on purpose: a listing probes the system, and listInputs() is const
    std::string  clientName_ = "RetroPlug";  // copied from the backend at open(): the name this stream PUBLISHES under
    std::string  filterName_ = "RetroPlug";  // the backend's FAMILY, copied at open(): what the filters judge a port by
    LogSink      sink_;                      // empty: stderr
    bool         open_       = false;
    // RETROPLUG_MIDI_LOG: dump in + out bytes to stderr. Set by open() on the control thread and read by the ports'
    // callback threads, so it is atomic rather than a plain bool a thread may read torn or hoisted.
    std::atomic<bool> log_{false};

    std::vector<std::string> selectedIn_;   // exact names; ignored when allInputs_
    bool                     allInputs_ = false;
    std::vector<std::string> selectedOut_;
    std::string              reservedIn_;   // "" = nothing claimed by a control surface
    std::string              reservedOut_;  // ditto, the pair's output
    bool                     inputsSuspended_ = false;  // a scan is holding the inputs

    // What the open ports hold (the control thread's record): the getters publish it, and an apply the control surface
    // or a scan triggers (which is diffed) reads it. A selection setter reapplies regardless.
    std::vector<std::string> openInputs_;
    std::vector<std::string> openOutputs_;

    std::unique_ptr<MidiPort> virtualIn_;
    std::unique_ptr<MidiPort> virtualOut_;
    // The hardware ports, name and owner together, the CONTROL thread's alone: it opens them, and it closes one only
    // after the published set has stopped naming it and the send in flight has been waited out.
    std::vector<std::pair<std::string, std::unique_ptr<MidiPort>>> hwIn_;
    std::vector<std::pair<std::string, std::unique_ptr<MidiPort>>> hwOut_;
    std::atomic<OutputSet*>                                       outputs_{nullptr};  // published for send()
    std::atomic<std::uint32_t>              epoch_{0};          // odd while a send is in flight

    // Fixed-capacity ring, many producers (serialized by pushMutex_) and one consumer. A producer (a callback thread)
    // allocs into a slot's vector; the consumer (audio thread) moves it out. Overflow drops the incoming message (a
    // drained-every-block consumer never realistically fills 256; MIDI loss under an extreme flood is acceptable).
    std::array<Message, kCapacity> ring_;
    std::atomic<std::size_t>       head_{0};  // consumer index (audio thread)
    std::atomic<std::size_t>       tail_{0};  // producer index (written under pushMutex_)
    std::uint64_t                  seq_ = 0;  // monotonic arrival counter (guarded by pushMutex_)
    // Serializes the producers. Without it two ports delivering at once raced tail_, seq_ and the slot vector - heap
    // corruption, not just a lost message. Producer-side only: never taken by poll().
    std::mutex                     pushMutex_;
    std::atomic<std::uint64_t>     dropped_{0};
};

}  // namespace rp::midi
