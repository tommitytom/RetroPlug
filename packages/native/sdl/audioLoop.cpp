#include "app.hpp"

namespace retroplug::sdl {


// The audio loop: the product's AudioClient (the engine behind AudioHost's interface), the control-surface calls this
// product answers for, and the audio.json / midi.json shim until TypeScript owns the settings (docs/modular-graph-phase-
// f5b-design.md 5.5).

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

// Let the control plane apply the control surface it persisted (config.json, launchpadDevices.ts restoreLaunchpad)
// before MIDI opens and audio starts. A claim made from here lands with audio not yet started, so it is reserved
// before the shared stream ever opens the port and costs no restart; a claim that arrives after the bound below is a
// MIDI-SESSION reserve, which onReserveMidi handles the same way a later Connect does.
//
// What it waits on is the facet's restored() reaching this product (rp_device_host::restored), because the selection
// is TS's and the claim is asynchronous with respect to the mount: React runs the restore in an effect, on a later
// pump of the JS loop. So pump the loop until it lands. Bounded in TIME, not pumps: a pump does not block, so a count
// would be over in a millisecond. A UI that never reports (a broken bundle, no facet at all) costs half a second at
// startup and then starts anyway, so a missing or unreadable config.json can never keep the app silent. A product that
// started no control-surface provider has nothing to wait FOR - no facet is mounted, so TS never calls restored() - and
// waits not at all.
void restoreControlSurface(AppState& a) {
    if (!a.providers.startedNamed("launchpad")) return;
    const Uint32 start = SDL_GetTicks();
    while (!a.controlSurfaceRestored && SDL_GetTicks() - start < 500) {
        a.ui.tick();
        if (!a.controlSurfaceRestored) SDL_Delay(1);
    }
    if (!a.controlSurfaceRestored)
        std::fprintf(stderr, "[retroplug-sdl] control surface: the UI did not report its restore; continuing\n");
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

// ---- the audio configuration: the shim, the flags and the hook ----------------------------------------

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

// The whole sequence, once, through AudioHost: stop, release, open, prepare(info()), start (section 5.2). `--backend`
// is the flag's / RETROPLUG_SDL_AUDIO_BACKEND's override, applied where the shim is read so it wins over audio.json
// (design 5.2, report item 8).
void applyAudioConfig(AppState& a) {
    if (!a.audio) return;
    rp::audio::AudioConfig c = audioConfigOf(a);
    c.backend                = a.reqBackend;
    if (!a.backendOverride.empty()) {
        // The run's flag wins over every saved or UI choice for its duration (design 5.2, E12): a harness pins `null`
        // so nothing else can come up, whatever a row or a click says.
        c.backend = a.backendOverride;
        c.driver.clear();
        c.device.clear();
    }
    a.audio->apply(c);
}

// audio.json / midi.json are NATIVE-owned device settings, read here at startup (setupSdl) before
// bootControlPlane brings up the JS runtime that owns config.json — so they can't live in the TS config, and
// they're standalone-only anyway (the plugin gets audio/MIDI from its DAW). They're small reflect-cpp JSON
// blobs, decoded tolerantly (DefaultIfMissing) exactly like the per-system role config that crosses to native
// (a missing/garbage field takes the struct default). Whole file into a string (empty if unreadable).
std::string slurpTextFile(const std::string& path) {
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}
void writeTextFile(const std::string& path, const std::string& text) {
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

// The standalone audio device settings: a SHIM, until the choice is config.json's `audio` section (the TypeScript
// half). The fields a saved file carries are the ones the device opens with. `hostApi` is neither read nor resolved: a
// PaHostApiTypeId is a library's numeric id, which a saved choice may not be (section 5.1), and the backend and driver
// come from --audio-backend / the backend's auto choice / the Settings row. The field is still WRITTEN (as -1) so a file
// this build writes keeps the shape an older one had, and reading it ignores it. The rate, block and channel count
// apply. Defaults mirror AppState's so a partial file lands on them.
struct AudioCfgJson {
    int sampleRate = 48000;
    int blockSize = 512;
    int outChannels = 2;
    int hostApi = -1; // written for an old file's shape, never read: a numeric library id selects nothing
    std::string outputDevice; // the device's name in the driver's vocabulary; "" = the driver's default
    // The MIDI Clock submenu's tempo. Not an audio-device setting, but it belongs to the same
    // native-owned, standalone-only settings file rather than earning one of its own. The transport's
    // play/stop is deliberately NOT here: a tempo is a preference, a running clock is not — that starts
    // stopped every launch.
    double clockBpm = 120.0;
};

std::string audioCfgPath(AppState& a) { return a.svc.host.configDir() + "/audio.json"; }

void loadAudioConfig(AppState& a) {
    const std::string json = slurpTextFile(audioCfgPath(a));
    if (json.empty()) return; // absent/unreadable → keep the compiled-in / --block-size-seeded defaults
    const auto r = rfl::json::read<AudioCfgJson, rfl::DefaultIfMissing>(json);
    if (!r) return; // unparseable → keep defaults
    const AudioCfgJson c = r.value();
    if (c.sampleRate > 0) a.reqSampleRate = c.sampleRate;
    if (c.blockSize > 0) a.reqBlockSize = c.blockSize;
    if (c.outChannels >= 2 && c.outChannels <= 8 && (c.outChannels % 2) == 0) a.reqOutChannels = c.outChannels;
    a.reqOutputDevice = c.outputDevice; // resolved by name in the backend; falls back to its default if absent
    if (c.clockBpm >= MidiClockSync::kMinBpm && c.clockBpm <= MidiClockSync::kMaxBpm)
        a.clockBpmRequest.store(static_cast<float>(c.clockBpm), std::memory_order_relaxed);
}

void saveAudioConfig(AppState& a) {
    const AudioCfgJson c{ a.reqSampleRate, a.reqBlockSize, a.reqOutChannels, -1, a.reqOutputDevice,
                          a.clockBpmRequest.load(std::memory_order_relaxed) };
    writeTextFile(audioCfgPath(a), rfl::json::write(c) + "\n");
}

// The chosen MIDI input/output device names (Settings > MIDI). "" = None in both directions (the virtual ports only),
// the default; the input also takes rp::midi::kAllInputs ("*") as an explicit "every device".
//
// A SHIM, kept until the selection is the project's (a `midi` project section, its ports by name): this file holds one
// name each, and the mapping onto the stream's lists lives in applyMidiSelection. Then it is ignored outright, like
// audio.json, and the SDL app never shipped.
//
// NOTE for anyone reading an old midi.json: "" used to mean All Devices on the input side, and there is no way to tell
// that apart from "never chose" - both wrote the same empty string. So an existing file simply lands on the current
// default, which is the behaviour a default change should have.
struct MidiCfgJson {
    std::string input;
    std::string output;
};

std::string midiCfgPath(AppState& a) { return a.svc.host.configDir() + "/midi.json"; }

// The one-name mapping onto the stream's lists, shared by the startup load and the picker's hooks. It neither persists
// nor logs: reading the stored file back over itself would rewrite it at every launch, and the log line belongs to the
// user's change, not to a value the file already held.
//
// `mode` is the stream's: the load DIFFS, so reopening a project costs no connection it already had, while the picker's
// change REOPENS, which is the user's one remedy for a port that was unplugged and plugged back in (there is no hotplug
// watch).
void selectMidi(AppState& a, bool input, const std::string& name, rp::midi::Apply mode) {
    if (input) {
        a.midiInputJson = name;
        if (name.empty()) a.midi.setInputs({}, false, mode);
        else if (name == rp::midi::kAllInputs) a.midi.setInputs({}, true, mode);
        else a.midi.setInputs({name}, false, mode);
    } else {
        a.midiOutputJson = name;
        if (name.empty()) a.midi.setOutputs({}, mode);
        else a.midi.setOutputs({name}, mode);
    }
}

void loadMidiConfig(AppState& a) {
    const std::string json = slurpTextFile(midiCfgPath(a));
    if (json.empty()) return;
    const auto r = rfl::json::read<MidiCfgJson, rfl::DefaultIfMissing>(json);
    if (!r) return;
    const MidiCfgJson c = r.value();
    // Applied when midi.open() publishes the ports: the same one-name mapping the picker's hooks use, in its diffing
    // mode (nothing the file names is reopened here, because nothing is open yet).
    selectMidi(a, true, c.input, rp::midi::Apply::Diff);
    selectMidi(a, false, c.output, rp::midi::Apply::Diff);
}

void saveMidiConfig(AppState& a) {
    const MidiCfgJson c{ a.midiInputJson, a.midiOutputJson };
    writeTextFile(midiCfgPath(a), rfl::json::write(c) + "\n");
}

// Live audio reconfigure (the Audio settings submenu): AudioHost runs the whole sequence once (stop, release, open,
// prepare with what the DEVICE obtained, start), and the chosen values are persisted so the next launch opens the same
// device. Called on the UI thread. A no-change request costs nothing: AudioHost returns at once for a config it is
// already running.
void reconfigureAudio(AppState& a, int sampleRate, int blockSize, int channels, const std::string& device) {
    a.reqSampleRate   = sampleRate;
    a.reqBlockSize    = blockSize;
    a.reqOutChannels  = channels;
    a.reqOutputDevice = device;
    applyAudioConfig(a);
    saveAudioConfig(a);
    std::fprintf(stderr, "[retroplug-sdl] audio reconfigured: %d Hz, %d frames, %d ch (backend '%s', device '%s')\n",
                 a.reqSampleRate, a.reqBlockSize, a.reqOutChannels,
                 a.reqBackend.empty() ? "auto" : a.reqBackend.c_str(),
                 a.reqOutputDevice.empty() ? "default" : a.reqOutputDevice.c_str());
}

// Live MIDI device reselection (Settings > MIDI), while audio may be running. It does not stop audio: the stream closes
// and opens inputs, each port cancelling its own callback as it goes, and replaces its output set through the epoch
// handshake, which waits out at most the send in flight. A brief audio gap on a device change belongs to
// reconfigureAudio alone, where the device itself changes.
//
// The picker offers one name each while midi.json is the selection's home (the shim): "" is None, "*" is every device
// on the input side, and anything else is that port. The stream itself takes lists.
void applyMidiSelection(AppState& a, bool input, const std::string& name) {
    selectMidi(a, input, name, rp::midi::Apply::Reopen);
    saveMidiConfig(a);  // the picker's change is the one that persists it (loadMidiConfig does not)
    std::fprintf(stderr, "[retroplug-sdl] MIDI %s device -> '%s'\n", input ? "input" : "output",
                 name.empty() ? "None" : (name == rp::midi::kAllInputs ? "All Devices" : name.c_str()));
}

}  // namespace retroplug::sdl
