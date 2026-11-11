#include "app.hpp"

namespace retroplug::sdl {


// The audio loop: the product's AudioClient (the engine behind AudioHost's interface), the control-surface calls this
// product answers for, the startup rendezvous, and the `audio` section's apply (docs/modular-graph-phase-f5b-design.md
// 5.5 and 6.6). The device settings are config.json's own section, written and read by the audio package's TS; no
// native audio or MIDI settings file is read or written (E1).

// ---- the audio-thread scheduling this product still owns -------------------------------------------
#if defined(__linux__)
// Keep the UI/emulator-advancing work off the audio core (the last one), where AudioHost's callback and renderer run:
// a preempted or core-shared audio thread underruns under an A53's load. Best effort: a failure (no RT permission, too
// few cores) is ignored and the default scheduling stands.
void pinUiThreadAwayFromAudioCore() {
    const long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 2) return; // too few cores to segregate
    cpu_set_t set; CPU_ZERO(&set);
    for (long c = 0; c + 1 < ncpu; ++c) CPU_SET(c, &set); // all cores except the audio core (the last)
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}
#else
void pinUiThreadAwayFromAudioCore() {}
#endif

// ---- the audio client: the Engine, behind AudioHost's interface -------------------------------------
// render() is today's renderAudioBlock, unchanged except that the buffer is the pull's: AudioHost sizes it, and the
// engine renders planar into our own channel buffers which render() interleaves. prepare/release are the
// reconfigure's quiescent half, called by AudioHost between stop and start.

// ---- the control surface, as this product answers for it ------------------------------------------
//
// The three calls a provider makes of the product that owns the shared MIDI stream (rp_provider.h, design section 3).
// The Launchpad pack's facet asks for them and knows nothing about the stream's type; what they mean here is the two
// hazards a claimed port has in a stream that is already open: the stream must let go of it BEFORE it is opened (a
// Windows input is exclusive, and everywhere a pad press reaching both would fire every launch twice while notes out of
// the stream would light the surface's pads), and closing an input under a running callback. Neither costs an audio
// stop: an input's port cancels its own callback as it is destroyed, and the output set is replaced through the stream's
// epoch handshake, which waits out at most the send in flight (one block's sends, none when audio is not running).

bool AppState::onReserveMidi(void* ctx, const std::string& input, const std::string& output) {
    auto& a = *static_cast<AppState*>(ctx);
    if (a.midi.reservedInput() == input && a.midi.reservedOutput() == output) return true;  // already so
    a.midi.setReserved(input, output);
    std::fprintf(stderr, "[retroplug-sdl] control surface: reserved MIDI input '%s', output '%s'\n",
                 input.empty() ? "(none)" : input.c_str(), output.empty() ? "(none)" : output.c_str());
    return true;
}

void AppState::onSuspendInputs(void* ctx, bool suspended) {
    // The scan's held edge, and the one platform where it is real (midi/PortFilter.hpp's scanSuspendsInputs). Answering
    // before touching the stream is what makes this a no-op off Windows rather than a MIDI outage for the scan's
    // length.
    // Off Windows (ALSA's sequencer, CoreMIDI) an input is SHARED: the scan hears everything the stream does without it
    // closing anything.
    if (!rp::midi::scanSuspendsInputs()) return;
    auto& a = *static_cast<AppState*>(ctx);
    a.midi.setHardwareInputsSuspended(suspended);  // logs the suspended / resumed line, and is a no-op if unchanged
}

void AppState::onRestored(void* ctx) { static_cast<AppState*>(ctx)->controlSurfaceRestored = true; }

// Let the control plane report the two things the host waits on before it opens anything of its own: the control
// surface it persisted (config.json, launchpadDevices.ts restoreLaunchpad) and the `audio` section (the audio package's
// service, audioDevice.apply on its first poll). Both are TS's, and both run asynchronously with respect to the mount -
// React runs the services' starts in an effect, and their first polls (the audio apply and the project's MIDI selection)
// on a later pump of the JS loop - so pump the loop until they land. The order the host then opens in is the app's
// always: the claim (during this rendezvous), then the MIDI stream and the providers' restore, then the audio device
// (main, after this returns); the audio apply recorded here is what that device opens with. Bounded in TIME, not pumps:
// a pump does not block, so a count would be over in a millisecond. A UI that never reports (a broken bundle, no facet,
// no audio package) costs half a second at startup and then starts anyway with the defaults, so a missing or unreadable
// config.json can never keep the app silent. A product that started no control-surface provider has nothing to wait FOR
// in that half - no facet is mounted, so TS never calls restored() - and the audio apply is the only word it waits on.
void waitForUiStartup(AppState& a) {
    const bool waitsForSurface = a.providers.startedNamed("launchpad");
    const Uint32 start = SDL_GetTicks();
    auto settled = [&] { return (!waitsForSurface || a.controlSurfaceRestored) && a.audioApplied; };
    while (!settled() && SDL_GetTicks() - start < 500) {
        a.ui.tick();
        // The services poll on the "frame" event, which main's loop emits once a frame - after this rendezvous. Emit one
        // here too, so their first polls (the `audio` section's apply and a project's MIDI selection) land while the
        // host still holds everything, which is what lets the order be the claim, then the device, then the ports.
        emitUiFrame(a);
        if (!settled()) SDL_Delay(1);
    }
    if (waitsForSurface && !a.controlSurfaceRestored)
        std::fprintf(stderr, "[retroplug-sdl] control surface: the UI did not report its restore; continuing\n");
    if (!a.audioApplied)
        std::fprintf(stderr, "[retroplug-sdl] audio: the UI did not apply its settings; starting on the defaults\n");
}

// RETROPLUG_DEBUG_AUDIO: the audio backends' own chatter and the host API / device enumeration. Read once (it cannot
// change under us), by the render loop's clock sync and by the PortAudio backend's log handlers, which are its own.
bool audioDebugEnabled() {
    static const bool on = std::getenv("RETROPLUG_DEBUG_AUDIO") != nullptr;
    return on;
}

// ---- the audio client's body ------------------------------------------------------------------------

void ProductAudioClient::prepare(double sampleRateIn, int maxBlockFrames, int channels) {
    AppState& a = *app;
    // AudioHost runs this between stop and start, so the engine is quiescent: the same three calls today's
    // reconfigureAudio made, in the order its own comment describes (re-rate live cores, shape the host, size the
    // render), except that the rate and width are the DEVICE's rather than the request's.
    a.sampleRate       = sampleRateIn;
    a.numOutputs       = channels > 0 ? channels : 2;
    a.reqBlockSize     = maxBlockFrames > 0 ? maxBlockFrames : a.reqBlockSize;
    a.svc.engine.setSampleRate(sampleRateIn);
    setHostOutputs(a);
    a.svc.engine.setMaxBlockFrames(static_cast<std::uint32_t>(a.reqBlockSize));
    a.audioPlanar.assign(static_cast<std::size_t>(a.numOutputs),
                         std::vector<float>(static_cast<std::size_t>(a.reqBlockSize), 0.0f));
    a.audioPtrs.assign(static_cast<std::size_t>(a.numOutputs), nullptr);
    for (int c = 0; c < a.numOutputs; ++c) a.audioPtrs[static_cast<std::size_t>(c)] = a.audioPlanar[static_cast<std::size_t>(c)].data();
    // Hand the engine back: AudioHost's next call is start(), and the first pull renders on the audio thread.
    a.svc.invoker.setAudioThreadOwns(true);
}

void ProductAudioClient::release() {
    AppState& a = *app;
    // Take the Engine back from the audio thread: control-plane edits become push-only while it is away, and the
    // invoker's queue is drained into it so nothing is lost across the reconfigure (the plugin's
    // deactivate -> setSampleRate -> activate handoff, DPF-free).
    a.svc.invoker.setAudioThreadOwns(false);
    a.svc.invoker.drainInto(a.svc.engine);
}

void ProductAudioClient::render(float* out, int frames) {
    AppState& a = *app;
    const int N = a.numOutputs;
    if (frames <= 0 || N <= 0) return;
    if (static_cast<int>(a.audioPlanar[static_cast<std::size_t>(0)].size()) < frames)
        for (int c = 0; c < N; ++c) {
            a.audioPlanar[static_cast<std::size_t>(c)].resize(static_cast<std::size_t>(frames));
            a.audioPtrs[static_cast<std::size_t>(c)] = a.audioPlanar[static_cast<std::size_t>(c)].data();
        }

    a.svc.invoker.drainInto(a.svc.engine);
    // Drain MIDI the backend's callback threads staged into the ring, and hand it to the Engine - we own it on this
    // (audio) thread, exactly like PluginDSP::run stages host MIDI. Frame 0 for now: live hardware MIDI carries no
    // sub-block timing worth preserving (a proper offset from the block clock is a later step).
    a.midi.poll(a.midiScratch);
    a.clockSync.sampleRate         = a.sampleRate;
    a.clockSync.debug              = audioDebugEnabled();
    a.clockSync.droppedMessages    = a.midi.droppedCount();
    for (auto& m : a.midiScratch) {
        if (m.bytes.empty()) continue;
        // System Real-Time first, wherever it sits in the message. It drives the host transport and is never staged,
        // never sent to a cart: the sync roles regenerate whatever each core needs from Engine tempo/transport, and
        // risa's generated stream reaches the N8 through the graph's fan-out, so forwarding the wire bytes too would
        // double-clock it. Testing only for a ONE-BYTE message (what this did before) drops any clock a transport
        // batched with another event, and a tempo estimate missing pulses reads a whole ratio slow.
        rp::midi::extractRealtime(m.bytes, [&](std::uint8_t status) { a.clockSync.onByte(status); });
        if (m.bytes.empty()) continue;
        // A physical N8 is fed from the graph, not from here: an rp.n8.nes node, placed by a system's "drive the real
        // cart" setting, takes the same protocol-node MIDI and bytes its emulated NES's core does, at their frames.
        // Sysex is still not staged as musical MIDI: no role consumes it, and the passthrough translators (mGB, LSDj
        // MidiPassthrough) push every byte they are given straight down the cart's link port - so a controller's
        // handshake or bulk-LED message would arrive at the Game Boy as noise. A control surface has its own stream
        // for this (rp.launchpad.surface-in, read by the controller session).
        if (m.bytes[0] == 0xF0) continue;
        // Everything left (notes / CC / ...) is staged as host MIDI in.
        a.svc.engine.stageMidi(0, m.bytes);
        a.midiStaged.fetch_add(1, std::memory_order_relaxed);
    }
    a.clockSync.advance(frames);

    // Fold in the menu's play/stop + tempo, then publish what the transport ended up being (see AppState).
    a.clockSync.manualPlaying = a.transportRequest.load(std::memory_order_relaxed);
    a.clockSync.manualBpm     = a.clockBpmRequest.load(std::memory_order_relaxed);
    const double bpm          = a.clockSync.outBpm();
    const bool   playing      = a.clockSync.outPlaying();
    a.transportPlaying.store(playing, std::memory_order_relaxed);
    a.transportExternal.store(a.clockSync.active, std::memory_order_relaxed);
    a.transportBpm.store(static_cast<float>(bpm), std::memory_order_relaxed);
    a.svc.engine.setBpm(bpm);            // 120 free-run until an external clock master appears
    a.svc.engine.setTransport(playing);  // then locks to its tempo + start/stop
    // Multi-out: render into the N planar channel buffers; the Engine zeroes them, and the plan routes each system to
    // its pair per audioRouting (TS builds the graph for the N-channel host shape). N=2 collapses every mode to the
    // stereo mix. N=4/6/8 spreads stems across the device's pairs.
    a.svc.engine.processBlock(static_cast<std::uint32_t>(frames), a.audioPtrs.data(), static_cast<std::size_t>(N));

    // Host MIDI out (LSDj MI.OUT / Master Sync, the controller's launches) to the stream, then clear the block's
    // queue. Mirrors PluginDSP::run's writeMidiEvent drain (there it goes to the DAW), each message's frame dropped:
    // the stream's send takes no timestamp, so a message goes when its block is rendered. No length cap: a control
    // surface's LED traffic is sysex.
    for (const auto& mo : a.svc.engine.midiOut())
        if (!mo.data.empty()) a.midi.send(mo.data.data(), mo.data.size());
    a.svc.engine.clearMidiOut();

    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < N; ++c)
            out[static_cast<std::size_t>(i) * static_cast<std::size_t>(N) + static_cast<std::size_t>(c)] =
                a.audioPlanar[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
}

// ---- the audio configuration: the request, the flags and the hook --------------------------------------

rp::audio::AudioConfig audioConfigOf(const AppState& a) {
    rp::audio::AudioConfig c;
    c.driver            = a.reqDriver;
    c.device            = a.reqOutputDevice;
    c.sampleRate        = a.reqSampleRate;
    c.blockSize         = a.reqBlockSize;
    c.channels          = a.reqOutChannels;
    c.renderAheadFrames = a.renderAhead;
    return c;
}

// The whole sequence, once, through AudioHost: stop, release, open, prepare(info()), start (section 5.2). The run's
// --audio-backend / RETROPLUG_SDL_AUDIO_BACKEND and --block-size overrides are applied here, so they win over the section
// a row or a file saved (design 5.2, E12).
void applyAudioConfig(AppState& a) {
    if (!a.audio) return;
    rp::audio::AudioConfig c = audioConfigOf(a);
    c.backend                = a.reqBackend;
    // The run's flags over the saved choice (design 5.2, E12): a harness pins `null` so nothing else can come up
    // whatever a row or a click says, and a pinned portaudio run still opens the device its section picks.
    a.audio->apply(rp::audio::applyRunOverrides(c, a.backendOverride, a.blockSizeOverride));
}

// Live audio reconfigure (the harness's and the tests' path - the Settings > Audio Apply goes through the audio
// package's hook): AudioHost runs the whole sequence once (stop, release, open, prepare with what the DEVICE obtained,
// start). The chosen values are AppState's request; the section the file keeps is written by the UI that owns it, so
// this persists nothing. Called on the UI thread. A no-change request costs nothing: AudioHost returns at once for a
// config it is already running.
void reconfigureAudio(AppState& a, int sampleRate, int blockSize, int channels, const std::string& device) {
    a.reqSampleRate   = sampleRate;
    a.reqBlockSize    = blockSize;
    a.reqOutChannels  = channels;
    a.reqOutputDevice = device;
    applyAudioConfig(a);
    std::fprintf(stderr, "[retroplug-sdl] audio reconfigured: %d Hz, %d frames, %d ch (backend '%s', device '%s')\n",
                 a.reqSampleRate, a.reqBlockSize, a.reqOutChannels,
                 a.reqBackend.empty() ? "auto" : a.reqBackend.c_str(),
                 a.reqOutputDevice.empty() ? "default" : a.reqOutputDevice.c_str());
}

}  // namespace retroplug::sdl
