// The AUv3 <-> SameBoySystem bridge. Mirrors the shape of the desktop DPF
// plugin's run() (packages/native/plugin/PluginDSP.cpp): stage MIDI, zero the
// output lanes, drive the system's per-block triad (prepare → step → finish).
// Multi-out: the AU declares 5 stereo busses — bus 0 = the stereo mix, busses
// 1..4 = the four GB channel stems (the desktop ChannelSplit routing, see
// system/AudioRouting.hpp). finishBlock is driven with 8 lanes so it fans the
// per-channel tap across the stem pairs; the mix pair is summed from the
// stems. Hosts that don't do multi-out just connect bus 0.
//
// On top of the render path sits the CoreBridge (RetroPlugCoreBridge.h): a
// main-thread control surface that reaches the render thread through an SPSC
// command ring (cheap ops) or a bypass gate (heavy ops). See the header for
// the channel-per-operation rationale.
#import "RetroPlugAudioUnit.h"
#import "RetroPlugCoreBridge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "EmbeddedRoms.hpp"
#include "system/MemoryType.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoyConstants.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/SpscRing.hpp"

NSErrorDomain const RPCoreBridgeErrorDomain = @"com.toilville.retroplug.CoreBridge";
const NSUInteger RPScreenWidth  = sameboy::kPixelWidth;
const NSUInteger RPScreenHeight = sameboy::kPixelHeight;

namespace {

constexpr AUAudioFrameCount kMaxFrames = 4096;

// Multi-out lane layout. The core's split path (SystemBase finishBlock with
// 2 * streamCount lanes) fans stream k into lanes 2k / 2k+1, in
// SameBoySystem::channelLayout() order: Pulse 1, Pulse 2, Wave, Noise. The
// mix pair sits after the stems and is summed from them each block.
constexpr std::size_t kStemCount = SameBoySystem::kAudioChannelCount; // 4
constexpr std::size_t kStemLanes = 2 * kStemCount;
constexpr std::size_t kMixLane   = kStemLanes;      // lanes 8/9 = mix L/R
constexpr std::size_t kLaneCount = kStemLanes + 2;
constexpr NSInteger   kBusCount  = 1 + (NSInteger)kStemCount; // Mix + stems

// MIDI sync (see RPMidiSyncMode in the bridge header for the semantics). The
// AU parameter addresses double as the tree's stable identifiers.
constexpr AUParameterAddress kParamSyncMode     = 0;
constexpr AUParameterAddress kParamTempoDivisor = 1;
constexpr AUParameterAddress kParamAutoStart    = 2;

constexpr std::uint8_t kMidiClock      = 0xf8; // 24-PPQN realtime tick (LSDJ_CLOCK)
constexpr std::uint8_t kMidiStart      = 0xfa; // transport start — Arduinoboy bookend
constexpr std::uint8_t kMidiStop       = 0xfc; // transport stop
constexpr std::uint8_t kMidiMapNoteOff = 0xfe; // MidiMap NoteOff handshake sentinel
constexpr std::uint8_t kStartButton    = 7;    // GameboyButton::Start

constexpr bool isNoteOnStatus(std::uint8_t s)  { return (s & 0xf0) == 0x90; }
constexpr bool isNoteOffStatus(std::uint8_t s) { return (s & 0xf0) == 0x80; }

// MidiMap row byte: ch0 NoteOn → note; ch1 → note + 128; other channels
// skipped (-1). Port of dspRoles.ts midiMapRow.
constexpr int midiMapRow(int channel, int note) {
    return channel == 0 ? note : channel == 1 ? note + 128 : -1;
}

// PS/2 scancode tables for LSDj's keyboard mode (SYNC=KEYBD), ported verbatim
// from packages/retroplug/src/lsdjKeyboardMap.ts:
//   NOTE_START .. +24 → kKbNoteMap       (two octaves of note keys)
//   LOW_START  .. +12 → kKbLowOctaveMap  (mute / cursor / enter / table)
constexpr int kKbNoteStart = 48; // MIDI C-3
constexpr int kKbLowStart  = 36; // MIDI C-2
constexpr std::uint8_t kKbNoteMap[24] = {
    0x1a, 0x1b, 0x22, 0x23, 0x21, 0x2a, 0x34, 0x32, 0x33, 0x31, 0x3b, 0x3a,
    0x15, 0x1e, 0x1d, 0x26, 0x24, 0x2d, 0x2e, 0x2c, 0x36, 0x35, 0x3d, 0x3c,
};
constexpr std::uint8_t kKbLowOctaveMap[12] = {
    0x01, // Mute1
    0x09, // Mute2
    0x78, // Mute3
    0x07, // Mute4
    0x6b, // Cursor Left
    0x74, // Cursor Right
    0x75, // Cursor Up
    0x72, // Cursor Down
    0x5a, // Enter
    0x7a, // Table Up
    0x7d, // Table Down
    0x29, // Table Cue
};
constexpr std::uint8_t kKbOctDn = 0x05;
constexpr std::uint8_t kKbOctUp = 0x06;

// The four cursor scancodes that need the 0xE0 "extended" prefix first.
constexpr bool isExtendedScancode(std::uint8_t s) {
    return s == 0x6b || s == 0x72 || s == 0x74 || s == 0x75;
}

// The GB serial mangles each incoming PS/2 scancode: LSDj decodes the low 7
// bits in reversed order, so every byte is pre-mangled to that form.
constexpr std::uint8_t toGbSerialByte(std::uint8_t scancode) {
    std::uint8_t r = 0;
    for (int i = 0; i < 7; ++i)
        r = (std::uint8_t)((r << 1) | ((scancode >> i) & 1));
    return r;
}

// One UI→render command. POD so it crosses the SPSC ring as a byte copy.
struct RtCommand {
    enum Op : std::uint8_t { kButton = 0, kReset = 1, kGainDb = 2, kFastBoot = 3 };
    std::uint8_t op = 0;
    std::uint8_t a  = 0;    // button id
    std::uint8_t b  = 0;    // bool (down / on)
    float        f  = 0.0f; // gain dB
};

// Everything the render block touches. Owned by the AU, captured by raw
// pointer in internalRenderBlock (the block must not retain self).
struct RenderState {
    std::unique_ptr<SameBoySystem> system;
    double sampleRate = 44100.0;
    // Scratch planar lanes (8 stems + the mix pair), so rendering is
    // independent of the host's ABL buffer layout (null mData, interleaved
    // hosts, etc.).
    std::array<std::vector<float>, kLaneCount> lanes;
    // Render-thread-only: mSampleTime of the quantum the lanes currently hold.
    // Hosts call the render block once PER BUS per quantum; only the first
    // call advances the emulator, the rest copy cached lanes. NaN = nothing
    // rendered yet (NaN compares unequal to everything, itself included).
    double renderedSampleTime = std::numeric_limits<double>::quiet_NaN();

    // UI→render commands. Bridge methods are main-thread-only, so the
    // single-producer contract holds (producer = main, consumer = render).
    SpscRing<RtCommand, 128> commands;

    // Bypass gate for heavy main-thread mutations (ROM swap, model change,
    // save/load). While `bypassed` the render block emits silence and never
    // touches `system`; `inRender` brackets the block so the main thread can
    // wait out an in-flight render. All four accesses are seq_cst: the total
    // order guarantees that if the gate-holder saw inRender==false, a render
    // block entering afterwards must see bypassed==true (Dekker handshake).
    std::atomic<bool> bypassed{false};
    std::atomic<bool> inRender{false};

    // Main-thread-only: settings template (model / fastBoot / gainDb /
    // highpass) applied to the next system construction so they carry over
    // across ROM loads. Blob fields are never written here.
    SameBoyConfig nextConfig;

    // -- MIDI sync -----------------------------------------------------------
    // Written by the AU parameter observer (any thread), read by the render
    // block every quantum. Lives here rather than on the system so it
    // survives ROM/model swaps, mirroring the desktop role config.
    std::atomic<std::uint8_t> syncMode{RPMidiSyncModeMgb};
    std::atomic<std::uint8_t> tempoDivisor{1}; // 1–8; 24/divisor ticks per quarter
    std::atomic<bool>         autoStart{false};

    // Host clock taps and the MIDI output sink, cached at allocate time per
    // the AUAudioUnit contract (calling the properties from the render thread
    // is not allowed). Any may be nil — the standalone app's AVAudioEngine
    // provides none of them.
    AUHostMusicalContextBlock musicalContext = nil;
    AUHostTransportStateBlock transportState = nil;
    AUMIDIOutputEventBlock    midiOutput     = nil;

    // Render-thread-only per-mode translator state — the iOS twin of the
    // desktop role's ctx.state. Reset whenever the mode flips (lastMode
    // mismatch, 0xff forces it on first render) or a new cart is swapped in.
    struct SyncState {
        std::uint8_t lastMode = 0xff;
        bool prevTransport = false; // host transport last quantum (edges → bookends / autoStart)

        // Tick walk (desktop dspKernel walkTicks): the next 24/divisor-PPQ
        // tick index not yet emitted, persisted across blocks so the clock is
        // drift-free with no double-fire at block edges. clockRunning false →
        // re-anchor before walking.
        bool   clockRunning = false;
        double nextTick     = 0.0;

        bool   abPlaying = false; // Arduinoboy note-24/25 play flag
        double abDivisor = 1.0;   // Arduinoboy runtime divisor (notes 26-29)

        int lastRow = -1; // MidiMap: row of the most recent NoteOn
        int octave  = 4;  // KeyboardMidi octave cursor

        // MI.OUT flag-gated framing (1 data-present bit + 7 payload bits per
        // frame, MSB-first) + the command/value byte protocol. Partial frames
        // carry across blocks in bitAcc/bitCount.
        std::uint32_t bitAcc       = 0;
        int           bitCount     = 0;
        bool          pendingValue = false;
        std::uint8_t  pendingCmd   = 0;

        bool msStarted = false; // MasterSync: a run is in progress (0xFC owed on stop)
    } sync;
};

// Holds the render thread out of `system` for the scope. `acquired()` false
// means the render block never yielded within the timeout — callers must
// then leave the system untouched.
class BypassGate {
public:
    explicit BypassGate(RenderState* state) : state_(state) {
        state_->bypassed.store(true);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (state_->inRender.load()) {
            if (std::chrono::steady_clock::now() > deadline) return;
            std::this_thread::yield();
        }
        acquired_ = true;
    }
    ~BypassGate() { state_->bypassed.store(false); }
    BypassGate(const BypassGate&)            = delete;
    BypassGate& operator=(const BypassGate&) = delete;
    bool acquired() const { return acquired_; }

private:
    RenderState* state_;
    bool acquired_ = false;
};

void emitHostMidi(RenderState* state, AUEventSampleTime when,
                  const std::uint8_t* bytes, NSInteger length) {
    if (state->midiOutput) state->midiOutput(when, /*cable*/ 0, length, bytes);
}

// Arduinoboy MI.OUT byte protocol (port of lsdjArduinoboy.ts
// arduinoboyDecodeByte — itself verbatim from the trash80 firmware):
//   realtime 0x7D/0x7E/0x7F → 0xFA/0xFC/0xF8
//   command  0x70..0x7C     → m = byte-0x70; the NEXT value byte completes it
//     (m < 4 → NoteOn ch m, value 0 → NoteOff; m < 8 → CC ch m-4 with CC# = m,
//      a documented simplification; m < 0xC → PC ch m-8)
//   value    0x00..0x6F     → completes a pending command (else ignored)
void arduinoboyDecodeByte(RenderState* state, std::uint8_t byte,
                          AUEventSampleTime when) {
    RenderState::SyncState& st = state->sync;
    if (byte >= 0x80) return; // not a protocol byte — drop, keep pending state

    // Realtime commands are single-byte and orthogonal to a pending
    // command/value pair — they fire without disturbing the wait.
    switch (byte) {
        case 0x7f: { const std::uint8_t m[1] = {kMidiClock}; emitHostMidi(state, when, m, 1); return; }
        case 0x7d: { const std::uint8_t m[1] = {kMidiStart}; emitHostMidi(state, when, m, 1); return; }
        case 0x7e: { const std::uint8_t m[1] = {kMidiStop};  emitHostMidi(state, when, m, 1); return; }
        default: break;
    }
    if (byte >= 0x70) { // start a pending command/value pair
        st.pendingCmd   = (std::uint8_t)(byte - 0x70);
        st.pendingValue = true;
        return;
    }
    if (!st.pendingValue) return; // stray value byte — no command pending
    st.pendingValue = false;
    const std::uint8_t m = st.pendingCmd;
    const std::uint8_t v = (std::uint8_t)(byte & 0x7f);
    st.pendingCmd = 0;
    if (m < 4) {
        // value 0 → NoteOff. The firmware offs the channel's most-recent note;
        // without that running state, note 0 is the "channel quiet" signal.
        if (v == 0) {
            const std::uint8_t out[3] = {(std::uint8_t)(0x80 | m), 0, 0};
            emitHostMidi(state, when, out, 3);
        } else {
            const std::uint8_t out[3] = {(std::uint8_t)(0x90 | m), v, 0x7f};
            emitHostMidi(state, when, out, 3);
        }
    } else if (m < 8) {
        const std::uint8_t out[3] = {(std::uint8_t)(0xb0 | (m - 4)), m, v};
        emitHostMidi(state, when, out, 3);
    } else if (m < 0x0c) {
        const std::uint8_t out[2] = {(std::uint8_t)(0xc0 | (m - 8)), v};
        emitHostMidi(state, when, out, 2);
    } // m >= 0x0C: undefined per the firmware; drop.
}

// midiOut / masterSync: decode this block's captured serial-out bytes
// (SameBoySystem::serialOutLog_, cleared each prepareForBlock and filled while
// stepping) into host MIDI. Ports of arduinoboyDecodeSerialOut /
// arduinoboyMasterSyncBlock (lsdjArduinoboy.ts) — same-block rather than the
// desktop's one-block-latency handoff.
void drainSerialOutToHost(RenderState* state, SameBoySystem* sys,
                          std::uint8_t mode, AUEventSampleTime when) {
    const auto& log = sys->serialOutLog_;
    RenderState::SyncState& st = state->sync;

    if (mode == RPMidiSyncModeMasterSync) {
        // Density, not presence, discriminates play from stop: a PLAYING LSDj
        // master clocks ~1-2 bytes/block; a STOPPED one floods a continuous
        // link handshake (100+ bytes/block).
        if (log.size() > 16) {
            if (st.msStarted) {
                const std::uint8_t stop[1] = {kMidiStop};
                emitHostMidi(state, when, stop, 1);
                st.msStarted = false;
            }
            return;
        }
        for (const auto& entry : log) {
            if (!st.msStarted) {
                // First tick of a run: the byte is LSDj's song row → NoteOn,
                // then transport start, then this byte's own clock tick.
                st.msStarted = true;
                const std::uint8_t on[3] = {0x90, (std::uint8_t)(entry.second & 0x7f), 0x7f};
                emitHostMidi(state, when, on, 3);
                const std::uint8_t start[1] = {kMidiStart};
                emitHostMidi(state, when, start, 1);
            }
            const std::uint8_t clk[1] = {kMidiClock};
            emitHostMidi(state, when, clk, 1); // one MIDI clock per tempo byte
        }
        return;
    }

    // MI.OUT: reconstruct the MSB-first bit stream and strip the flag-gated
    // framing — read 1 flag bit; if set, the next 7 bits are a protocol byte,
    // else it was an idle bit.
    for (const auto& entry : log) {
        st.bitAcc = (st.bitAcc << 8) | entry.second;
        st.bitCount += 8;
        for (;;) {
            if (st.bitCount < 1) break;
            const std::uint32_t flag = (st.bitAcc >> (st.bitCount - 1)) & 1u;
            if (flag == 0) { st.bitCount -= 1; continue; } // idle bit
            if (st.bitCount < 8) break; // payload not fully arrived yet
            const std::uint8_t cmd =
                (std::uint8_t)((st.bitAcc >> (st.bitCount - 8)) & 0x7f);
            st.bitCount -= 8;
            arduinoboyDecodeByte(state, cmd, when);
        }
        // Keep only the still-buffered low bits so bitAcc can't overflow.
        st.bitAcc = st.bitCount > 0 ? (st.bitAcc & ((1u << st.bitCount) - 1u)) : 0u;
    }
}

// The host-MIDI → link-port translator + host-clock walk, dispatched on the
// sync mode. Runs once per quantum BEFORE the emulation triad so every pushed
// byte lands in this block's serial pump. The iOS twin of the desktop
// lsdjSync SystemBehavior (dspRoles.ts).
void processSyncInput(RenderState* state, SameBoySystem* sys,
                      AUAudioFrameCount frameCount,
                      const AURenderEvent* eventList) {
    RenderState::SyncState& st = state->sync;
    const std::uint8_t mode = state->syncMode.load(std::memory_order_relaxed);

    // Mode flip → reset all per-mode translator state (matching the desktop,
    // where a config change rebuilds the role) and reseed the Arduinoboy
    // divisor from the parameter.
    if (mode != st.lastMode) {
        st = RenderState::SyncState{};
        st.lastMode  = mode;
        st.abDivisor = state->tempoDivisor.load(std::memory_order_relaxed);
    }

    // MI.OUT / MasterSync read LSDJ's OUTGOING serial — keep the capture gate
    // armed exactly while one of them is active.
    sys->setSerialOutCapture(mode == RPMidiSyncModeMidiOut ||
                             mode == RPMidiSyncModeMasterSync);

    const bool hostHasClock =
        state->musicalContext != nil && state->transportState != nil;

    // -- host MIDI events → link-port bytes ----------------------------------
    for (const AURenderEvent* ev = eventList; ev != nullptr; ev = ev->head.next) {
        if (ev->head.eventType != AURenderEventMIDI) continue;
        const AUMIDIEvent& m = ev->MIDI;
        if (m.length == 0 || m.length > sizeof(m.data)) continue;
        const std::uint8_t status = m.data[0];
        const std::uint8_t note   = m.length >= 2 ? m.data[1] : 0;
        switch (mode) {
            case RPMidiSyncModeMgb:
                // Verbatim byte passthrough — mGB parses MIDI itself.
                for (std::uint16_t j = 0; j < m.length; ++j)
                    sys->pushSerialIn(m.data[j]);
                break;
            case RPMidiSyncModeMidiSync:
                // External-clock fallback for transport-less hosts only — the
                // walk below owns the tick stream when the host has one.
                if (!hostHasClock && status == kMidiClock)
                    sys->pushSerialIn(kMidiClock);
                break;
            case RPMidiSyncModeMidiSyncArduinoboy:
                // Input notes drive runtime state: 24/25 toggle the play flag,
                // 26-29 set the divisor, 30+ push a raw row byte.
                if (!isNoteOnStatus(status)) break;
                if      (note == 24) st.abPlaying = true;
                else if (note == 25) st.abPlaying = false;
                else if (note == 26) st.abDivisor = 1;
                else if (note == 27) st.abDivisor = 2;
                else if (note == 28) st.abDivisor = 4;
                else if (note == 29) st.abDivisor = 8;
                else if (note >= 30) sys->pushSerialIn((std::uint8_t)(note - 30));
                break;
            case RPMidiSyncModeMidiMap:
                // NoteOn → a row byte LSDj reads as a SONG-row jump; the
                // matching NoteOff sends the 0xFE handshake for the row most
                // recently sounded.
                if (isNoteOnStatus(status)) {
                    const int row = midiMapRow(status & 0x0f, note);
                    if (row >= 0) {
                        sys->pushSerialIn((std::uint8_t)(row & 0xff));
                        st.lastRow = row;
                    }
                } else if (isNoteOffStatus(status)) {
                    if (midiMapRow(status & 0x0f, note) == st.lastRow) {
                        sys->pushSerialIn(kMidiMapNoteOff);
                        st.lastRow = -1;
                    }
                }
                break;
            case RPMidiSyncModeKeyboardMidi: {
                // MIDI NoteOns → LSDj PS/2 scancodes, sliding the octave
                // cursor to track the incoming note.
                if (!isNoteOnStatus(status)) break;
                if (note >= kKbNoteStart) {
                    const int n      = note - kKbNoteStart;
                    const int target = n / 12;
                    while (st.octave != target) {
                        sys->pushSerialIn(toGbSerialByte(
                            target > st.octave ? kKbOctUp : kKbOctDn));
                        st.octave += target > st.octave ? 1 : -1;
                    }
                    const int idx = n >= 0x3c ? (n % 12) + 0x0c : n % 12;
                    sys->pushSerialIn(toGbSerialByte(kKbNoteMap[idx]));
                } else if (note >= kKbLowStart) {
                    const std::uint8_t cmd = kKbLowOctaveMap[note - kKbLowStart];
                    if (isExtendedScancode(cmd))
                        sys->pushSerialIn(toGbSerialByte(0xe0)); // extended prefix
                    sys->pushSerialIn(toGbSerialByte(cmd));
                }
                break;
            }
            default:
                break; // Off / midiOut / masterSync: host MIDI is dropped
        }
    }

    // -- host transport → bookends, auto-arm, and the 0xF8 tick walk ---------
    const bool clockedMode = mode == RPMidiSyncModeMidiSync ||
                             mode == RPMidiSyncModeMidiSyncArduinoboy;
    bool   transportMoving = false;
    bool   haveContext     = false;
    double tempo = 0.0, beat = 0.0;
    if (clockedMode && hostHasClock) {
        AUHostTransportStateFlags flags = 0;
        if (state->transportState(&flags, NULL, NULL, NULL))
            transportMoving = (flags & AUHostTransportStateMoving) != 0;
        haveContext = state->musicalContext(&tempo, NULL, NULL, &beat, NULL, NULL);
    }

    // Arduinoboy bookends every host-transport edge with 0xFA/0xFC —
    // independent of the note-24 play flag that gates its clock.
    if (mode == RPMidiSyncModeMidiSyncArduinoboy &&
        transportMoving != st.prevTransport) {
        sys->pushSerialIn(transportMoving ? kMidiStart : kMidiStop);
    }

    // autoStart: tap START on the transport rise so the cart parks itself in
    // "wait for sync" without a joypad (headless-DAW-render nicety; the
    // pendingButtons queue spaces the press/release ~10 ms apart).
    if (clockedMode && transportMoving && !st.prevTransport &&
        state->autoStart.load(std::memory_order_relaxed)) {
        sys->pressButton(kStartButton, true);
        sys->pressButton(kStartButton, false);
    }
    st.prevTransport = transportMoving;

    // The tick walk: 24/divisor PPQ against the host beat position, one 0xF8
    // per tick. midiSync clocks whenever the transport moves; Arduinoboy only
    // while its play flag is armed (and on its runtime divisor).
    const bool wantTicks =
        mode == RPMidiSyncModeMidiSync ||
        (mode == RPMidiSyncModeMidiSyncArduinoboy && st.abPlaying);
    bool ticking = false;
    if (wantTicks && transportMoving && haveContext && tempo > 0.0) {
        const double divisor = mode == RPMidiSyncModeMidiSyncArduinoboy
            ? st.abDivisor
            : (double)state->tempoDivisor.load(std::memory_order_relaxed);
        const double tpq     = 24.0 / divisor;
        const double tickPos = beat * tpq; // position at block start
        const double blockTicks =
            (double)frameCount / state->sampleRate * (tempo / 60.0) * tpq;
        // nextTick persists across blocks for a drift-free clock; re-anchor
        // when it falls outside the walk (start, loop wrap, seek, re-arm).
        if (!st.clockRunning || st.nextTick < tickPos - 1e-6 ||
            st.nextTick > tickPos + blockTicks + 1.0) {
            st.nextTick = std::ceil(tickPos - 1e-9);
        }
        while (st.nextTick < tickPos + blockTicks) {
            sys->pushSerialIn(kMidiClock);
            st.nextTick += 1.0;
        }
        ticking = true;
    }
    st.clockRunning = ticking;
}

// Copy one cached stereo pair into the host's buffers (deinterleaved float32;
// mono falls back to lane L via the min()).
void copyPairToOutput(RenderState* state, std::size_t pairBase,
                      AudioBufferList* outputData, AUAudioFrameCount frameCount) {
    float* lanes[2] = { state->lanes[pairBase].data(),
                        state->lanes[pairBase + 1].data() };
    const UInt32 chans = outputData->mNumberBuffers;
    for (UInt32 c = 0; c < chans; ++c) {
        AudioBuffer& buf = outputData->mBuffers[c];
        if (buf.mData == nullptr) buf.mData = lanes[std::min<UInt32>(c, 1)];
        else std::memcpy(buf.mData, lanes[std::min<UInt32>(c, 1)],
                         frameCount * sizeof(float));
        buf.mDataByteSize = frameCount * sizeof(float);
    }
}

// The settings that carry over from one system to the next (never blobs).
SameBoyConfig templateConfig(const SameBoyConfig& next) {
    SameBoyConfig cfg;
    cfg.model    = next.model;
    cfg.fastBoot = next.fastBoot;
    cfg.gainDb   = next.gainDb;
    cfg.highpass = next.highpass;
    return cfg;
}

NSError* bridgeError(RPCoreBridgeError code, NSString* message) {
    return [NSError errorWithDomain:RPCoreBridgeErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

} // namespace

@implementation RetroPlugAudioUnit {
    std::unique_ptr<RenderState> _state;
    AUAudioUnitBusArray* _outputBusArray;
    AUParameterTree*     _parameterTree;
}

- (nullable instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                               options:(AudioComponentInstantiationOptions)options
                                                 error:(NSError**)outError {
    self = [super initWithComponentDescription:componentDescription options:options error:outError];
    if (self == nil) return nil;

    _state = std::make_unique<RenderState>();

    AVAudioFormat* format =
        [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100.0 channels:2];
    // Names must track SameBoySystem::channelLayout() order (stems = busses 1..4).
    NSArray<NSString*>* busNames = @[ @"Mix", @"Pulse 1", @"Pulse 2", @"Wave", @"Noise" ];
    NSMutableArray<AUAudioUnitBus*>* busses =
        [NSMutableArray arrayWithCapacity:(NSUInteger)kBusCount];
    for (NSUInteger i = 0; i < (NSUInteger)kBusCount; ++i) {
        AUAudioUnitBus* bus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
        if (bus == nil) return nil;
        bus.name = busNames[i];
        bus.maximumChannelCount = 2;
        [busses addObject:bus];
    }
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                             busType:AUAudioUnitBusTypeOutput
                                                              busses:busses];
    self.maximumFramesToRender = kMaxFrames;

    // MIDI-sync knobs as AU parameters — the only control surface a DAW host
    // has over the extension (the CoreBridge is in-process only). Values map
    // 1:1 onto RPMidiSyncMode / the divisor / the auto-start flag.
    const AudioUnitParameterOptions rw =
        kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
    AUParameter* mode =
        [AUParameterTree createParameterWithIdentifier:@"syncMode"
                                                  name:@"MIDI Mode"
                                               address:kParamSyncMode
                                                   min:0
                                                   max:7
                                                  unit:kAudioUnitParameterUnit_Indexed
                                              unitName:nil
                                                 flags:rw
                                          valueStrings:@[
                                              @"Off", @"mGB Notes", @"LSDj MIDI Sync",
                                              @"LSDj Arduinoboy Sync", @"LSDj MIDI Map",
                                              @"LSDj Keyboard MIDI", @"LSDj MIDI Out",
                                              @"LSDj Master Sync"
                                          ]
                                   dependentParameters:nil];
    AUParameter* divisor =
        [AUParameterTree createParameterWithIdentifier:@"syncTempoDivisor"
                                                  name:@"Sync Tempo Divisor"
                                               address:kParamTempoDivisor
                                                   min:1
                                                   max:8
                                                  unit:kAudioUnitParameterUnit_Indexed
                                              unitName:nil
                                                 flags:rw
                                          valueStrings:nil
                                   dependentParameters:nil];
    AUParameter* autoStart =
        [AUParameterTree createParameterWithIdentifier:@"syncAutoStart"
                                                  name:@"Sync Auto Start"
                                               address:kParamAutoStart
                                                   min:0
                                                   max:1
                                                  unit:kAudioUnitParameterUnit_Boolean
                                              unitName:nil
                                                 flags:rw
                                          valueStrings:nil
                                   dependentParameters:nil];
    _parameterTree = [AUParameterTree createTreeWithChildren:@[ mode, divisor, autoStart ]];

    RenderState* state = _state.get(); // raw capture — the tree must not retain self
    _parameterTree.implementorValueObserver = ^(AUParameter* param, AUValue value) {
        switch (param.address) {
            case kParamSyncMode:
                state->syncMode.store((std::uint8_t)std::clamp<AUValue>(value, 0, 7),
                                      std::memory_order_relaxed);
                break;
            case kParamTempoDivisor:
                state->tempoDivisor.store((std::uint8_t)std::clamp<AUValue>(value, 1, 8),
                                          std::memory_order_relaxed);
                break;
            case kParamAutoStart:
                state->autoStart.store(value >= 0.5f, std::memory_order_relaxed);
                break;
        }
    };
    _parameterTree.implementorValueProvider = ^AUValue(AUParameter* param) {
        switch (param.address) {
            case kParamSyncMode:     return state->syncMode.load(std::memory_order_relaxed);
            case kParamTempoDivisor: return state->tempoDivisor.load(std::memory_order_relaxed);
            case kParamAutoStart:    return state->autoStart.load(std::memory_order_relaxed) ? 1 : 0;
        }
        return 0;
    };
    mode.value    = RPMidiSyncModeMgb; // matches the RenderState defaults
    divisor.value = 1;

    return self;
}

- (AUParameterTree*)parameterTree {
    return _parameterTree;
}

- (AUAudioUnitBusArray*)outputBusses {
    return _outputBusArray;
}

- (NSArray<NSString*>*)MIDIOutputNames {
    // Publishing an output port makes hosts hand us midiOutputEventBlock —
    // where the midiOut (MI.OUT) and masterSync decodes land.
    return @[ @"MIDI Out" ];
}

- (BOOL)allocateRenderResourcesAndReturnError:(NSError**)outError {
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;

    const double sr = _outputBusArray[0].format.sampleRate;
    _state->sampleRate = sr;
    for (std::vector<float>& lane : _state->lanes) lane.assign(kMaxFrames, 0.0f);
    _state->renderedSampleTime = std::numeric_limits<double>::quiet_NaN();

    if (_state->system == nullptr) {
        // Default spec: the embedded mGB ROM on a Game Boy Color core — keeps
        // the AUv3 extension behaving exactly like the spike until a host app
        // loads something else through the bridge.
        SameBoyConfig cfg = templateConfig(_state->nextConfig);
        cfg.embeddedRom = "mgb";
        const auto rom = rp::embeddedMgbRom();
        _state->system = std::make_unique<SameBoySystem>(
            /*id*/ 1, std::move(cfg),
            std::vector<std::uint8_t>(rom.begin(), rom.end()));
    }
    if (!_state->system->activated_) {
        _state->system->onActivate(sr);   // resumes from the deactivate snapshot if any
    } else if (_state->system->sampleRate_ != sr) {
        _state->system->onSampleRateChanged(sr);
    }
    // Arm the periodic whole-savestate snapshot (SRAM autosave reads slices
    // of it). Idempotent; must run before rendering starts.
    _state->system->enableStateSnapshot();

    // Cache the host clock taps + MIDI output for the render thread (the
    // properties must not be read mid-render). Nil in hosts without a
    // transport (e.g. the standalone app's AVAudioEngine) — midiSync then
    // falls back to incoming MIDI realtime clock bytes.
    _state->musicalContext = self.musicalContextBlock;
    _state->transportState = self.transportStateBlock;
    _state->midiOutput     = self.MIDIOutputEventBlock;
    _state->sync           = {}; // lastMode 0xff → full translator reset next render
    return YES;
}

- (void)deallocateRenderResources {
    _state->musicalContext = nil;
    _state->transportState = nil;
    _state->midiOutput     = nil;
    if (_state->system) {
        // Deactivate but KEEP the system: onDeactivate snapshots savestate +
        // SRAM into its config, so the next allocate resumes where it left
        // off instead of cold-booting (hosts toggle allocate/deallocate
        // around reconfiguration).
        _state->system->onDeactivate();
    }
    [super deallocateRenderResources];
}

- (AUInternalRenderBlock)internalRenderBlock {
    RenderState* state = _state.get(); // raw capture — never retain self in the block

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags* actionFlags,
                              const AudioTimeStamp*       timestamp,
                              AUAudioFrameCount           frameCount,
                              NSInteger                   outputBusNumber,
                              AudioBufferList*            outputData,
                              const AURenderEvent*        realtimeEventListHead,
                              AURenderPullInputBlock      pullInputBlock) {
        (void)actionFlags; (void)pullInputBlock;

        if (frameCount > kMaxFrames) return kAudioUnitErr_Uninitialized;
        if (outputBusNumber < 0 || outputBusNumber >= kBusCount)
            return kAudioUnitErr_InvalidElement;
        // Bus 0 serves the summed mix pair; bus n >= 1 serves stem n-1's pair.
        const std::size_t pairBase =
            (outputBusNumber == 0) ? kMixLane : 2 * (std::size_t)(outputBusNumber - 1);

        state->inRender.store(true);
        struct InRenderClear {
            std::atomic<bool>& flag;
            ~InRenderClear() { flag.store(false); }
        } clearOnExit{state->inRender};

        if (state->bypassed.load()) {
            // Main thread is mutating the system under the gate — silence.
            // Zeroed through the cached pair so null-mData hosts stay valid.
            std::memset(state->lanes[pairBase].data(), 0, frameCount * sizeof(float));
            std::memset(state->lanes[pairBase + 1].data(), 0, frameCount * sizeof(float));
            copyPairToOutput(state, pairBase, outputData, frameCount);
            return noErr;
        }

        SameBoySystem* sys = state->system.get();
        if (sys == nullptr) return kAudioUnitErr_Uninitialized;

        // The host calls this block once per connected bus per quantum; the
        // emulator must advance exactly once. First call of a new quantum
        // renders every lane, the rest copy the cache below.
        if (timestamp->mSampleTime != state->renderedSampleTime) {
            state->renderedSampleTime = timestamp->mSampleTime;

            // --- UI commands first, so a button edge lands in this block
            RtCommand cmd;
            while (state->commands.tryPop(cmd)) {
                switch (cmd.op) {
                    case RtCommand::kButton:   sys->pressButton(cmd.a, cmd.b != 0); break;
                    case RtCommand::kReset:    sys->onReset();                      break;
                    case RtCommand::kGainDb:   sys->setGainDb(cmd.f);               break;
                    case RtCommand::kFastBoot: sys->setFastBoot(cmd.b != 0);        break;
                }
            }

            // --- host MIDI + host clock → GB link-port bytes, dispatched on
            // the sync mode (the desktop lsdj-sync role, ported native — see
            // processSyncInput). Before the triad so every pushed byte lands
            // in this block's serial pump.
            processSyncInput(state, sys, frameCount, realtimeEventListHead);

            // --- render the 8 stem lanes: the triad's split path (finishBlock
            // with 8 lanes fans channel k -> lanes 2k/2k+1, and publishes the
            // periodic snapshots itself). finishBlock SUMS, caller zeroes.
            float* stems[kStemLanes];
            for (std::size_t l = 0; l < kStemLanes; ++l) {
                std::memset(state->lanes[l].data(), 0, frameCount * sizeof(float));
                stems[l] = state->lanes[l].data();
            }
            AudioBlockInfo info;
            info.frames     = frameCount;
            info.sampleRate = state->sampleRate;
            sys->prepareForBlock(info);
            while (sys->stepIfBelowTarget(frameCount)) {}
            sys->finishBlock(info, stems, kStemLanes);

            // MI.OUT / MasterSync: LSDJ's outgoing serial bytes were captured
            // into serialOutLog_ while stepping — decode them into host MIDI.
            const std::uint8_t outMode =
                state->syncMode.load(std::memory_order_relaxed);
            if (outMode == RPMidiSyncModeMidiOut ||
                outMode == RPMidiSyncModeMasterSync) {
                drainSerialOutToHost(state, sys, outMode,
                                     (AUEventSampleTime)timestamp->mSampleTime);
            }

            // Mix pair = sum of the stems. Each stem is highpassed per-channel
            // in the same mode as the mixed bus, so the sum reconstructs the
            // mix (exact in Off/Accurate; Remove-DC-Offset differs only in
            // where the DC is taken out — see the sameboy per-channel patch).
            float* mixL = state->lanes[kMixLane].data();
            float* mixR = state->lanes[kMixLane + 1].data();
            std::memset(mixL, 0, frameCount * sizeof(float));
            std::memset(mixR, 0, frameCount * sizeof(float));
            for (std::size_t k = 0; k < kStemCount; ++k) {
                const float* sL = state->lanes[2 * k].data();
                const float* sR = state->lanes[2 * k + 1].data();
                for (AUAudioFrameCount i = 0; i < frameCount; ++i) {
                    mixL[i] += sL[i];
                    mixR[i] += sR[i];
                }
            }
        }

        copyPairToOutput(state, pairBase, outputData, frameCount);
        return noErr;
    };
}

#pragma mark - Host session persistence (fullState)

// DAW project save/restore. The payload rides inside the plist dictionary the
// host serializes (bridged to kAudioUnitProperty_ClassInfo): the cartridge
// (raw bytes, or the embedded-ROM id when the ROM is baked into the binary),
// battery RAM, a savestate, and the carry-over config. Mirrors the desktop
// default of embedding the ROM in the project (SameBoyConfig::embedRom) — the
// extension sandbox can't reach the container app's ROM files, so bytes are
// the only thing that reliably survives a project reopen. Parameter values
// (the MIDI-sync knobs) are covered by [super fullState].
static NSString* const kRPStateVersionKey     = @"rp-version";
static NSString* const kRPStateEmbeddedRomKey = @"rp-embedded-rom";
static NSString* const kRPStateRomKey         = @"rp-rom";
static NSString* const kRPStateSramKey        = @"rp-sram";
static NSString* const kRPStateSavestateKey   = @"rp-state";
static NSString* const kRPStateModelKey       = @"rp-model";
static NSString* const kRPStateFastBootKey    = @"rp-fast-boot";
static NSString* const kRPStateGainDbKey      = @"rp-gain-db";
static NSString* const kRPStateHighpassKey    = @"rp-highpass";

// Hosts hit the state accessors from XPC worker threads; the CoreBridge
// control plane (and the bypass gate's single-control-thread contract) is
// main-thread-only, so both accessors funnel their system access through main.
static void RPRunOnMain(dispatch_block_t block) {
    if (NSThread.isMainThread) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

static NSNumber* RPNumberForKey(NSDictionary* dict, NSString* key) {
    id v = dict[key];
    return [v isKindOfClass:NSNumber.class] ? v : nil;
}

static NSData* RPDataForKey(NSDictionary* dict, NSString* key) {
    id v = dict[key];
    return [v isKindOfClass:NSData.class] ? v : nil;
}

- (NSDictionary<NSString*, id>*)fullState {
    NSMutableDictionary<NSString*, id>* dict =
        [NSMutableDictionary dictionaryWithDictionary:[super fullState] ?: @{}];

    RenderState* state = _state.get();
    __block NSDictionary<NSString*, id>* payload = nil;
    RPRunOnMain(^{
        SameBoySystem* sys = state->system.get();
        if (sys == nullptr) return;
        BypassGate gate(state);
        if (!gate.acquired()) return; // never yielded — save what super has

        NSMutableDictionary<NSString*, id>* p = [NSMutableDictionary dictionary];
        p[kRPStateVersionKey] = @1;
        const SameBoyConfig& cfg = sys->config_;
        p[kRPStateModelKey]    = @((uint32_t)cfg.model);
        p[kRPStateFastBootKey] = @(cfg.fastBoot);
        p[kRPStateGainDbKey]   = @(cfg.gainDb);
        p[kRPStateHighpassKey] = @((uint32_t)cfg.highpass);
        if (!cfg.embeddedRom.empty()) {
            p[kRPStateEmbeddedRomKey] =
                [NSString stringWithUTF8String:cfg.embeddedRom.c_str()];
        } else if (!sys->rom_.empty()) {
            p[kRPStateRomKey] = [NSData dataWithBytes:sys->rom_.data()
                                               length:sys->rom_.size()];
        }
        const auto sram = sys->saveSramBytes();
        if (!sram.empty())
            p[kRPStateSramKey] = [NSData dataWithBytes:sram.data() length:sram.size()];
        const auto save = sys->saveStateBytes();
        if (!save.empty())
            p[kRPStateSavestateKey] = [NSData dataWithBytes:save.data() length:save.size()];
        payload = p;
    });
    if (payload != nil) [dict addEntriesFromDictionary:payload];
    return dict;
}

- (void)setFullState:(NSDictionary<NSString*, id>*)fullState {
    [super setFullState:fullState]; // restores the parameter values
    if (fullState == nil) return;
    RPRunOnMain(^{ [self rp_restoreFromFullState:fullState]; });
}

- (void)rp_restoreFromFullState:(NSDictionary<NSString*, id>*)dict {
    // No version key = no RetroPlug payload (a fresh insert, or state saved
    // before this feature) — leave the current system alone.
    if (RPNumberForKey(dict, kRPStateVersionKey) == nil) return;
    RenderState* state = _state.get();

    SameBoyConfig cfg = templateConfig(state->nextConfig);
    if (NSNumber* n = RPNumberForKey(dict, kRPStateModelKey)) {
        const auto raw = n.unsignedIntValue;
        if (raw <= (uint32_t)SameBoyModel::Gbp) cfg.model = (SameBoyModel)raw;
    }
    if (NSNumber* n = RPNumberForKey(dict, kRPStateHighpassKey)) {
        const auto raw = n.unsignedIntValue;
        if (raw <= (uint32_t)SameBoyHighpass::RemoveDcOffset)
            cfg.highpass = (SameBoyHighpass)raw;
    }
    if (NSNumber* n = RPNumberForKey(dict, kRPStateFastBootKey)) cfg.fastBoot = n.boolValue;
    if (NSNumber* n = RPNumberForKey(dict, kRPStateGainDbKey))   cfg.gainDb = n.floatValue;
    if (NSData* sram = RPDataForKey(dict, kRPStateSramKey)) {
        const auto* p = static_cast<const std::uint8_t*>(sram.bytes);
        cfg.sram.assign(p, p + sram.length);
    }
    if (NSData* save = RPDataForKey(dict, kRPStateSavestateKey)) {
        const auto* p = static_cast<const std::uint8_t*>(save.bytes);
        cfg.savestate.assign(p, p + save.length);
    }
    // The restored settings become the template for later ROM loads too.
    state->nextConfig = templateConfig(cfg);

    std::vector<std::uint8_t> rom;
    id embedded = dict[kRPStateEmbeddedRomKey];
    if ([embedded isKindOfClass:NSString.class]) {
        // Only "mgb" ships today; refuse unknown ids rather than mis-boot.
        if (![embedded isEqualToString:@"mgb"]) return;
        cfg.embeddedRom = "mgb";
        const auto bytes = rp::embeddedMgbRom();
        rom.assign(bytes.begin(), bytes.end());
    } else if (NSData* romData = RPDataForKey(dict, kRPStateRomKey)) {
        const auto* p = static_cast<const std::uint8_t*>(romData.bytes);
        rom.assign(p, p + romData.length);
    } else {
        return; // config-only payload — nothing to boot
    }
    [self rp_swapSystemWithConfig:std::move(cfg) rom:std::move(rom) error:NULL];
}

#pragma mark - CoreBridge (declared in RetroPlugCoreBridge.h)

- (BOOL)hasSystem {
    return _state->system != nullptr;
}

// Gate-swap the running system for one built from `cfg` + `rom`. The old
// system destructs on the main thread (GB_free off the audio thread).
- (BOOL)rp_swapSystemWithConfig:(SameBoyConfig)cfg
                            rom:(std::vector<std::uint8_t>)rom
                          error:(NSError**)error {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RenderState* state = _state.get();

    BypassGate gate(state);
    if (!gate.acquired()) {
        if (error) *error = bridgeError(RPCoreBridgeErrorGateTimeout,
                                        @"The audio render thread did not yield.");
        return NO;
    }
    state->system = std::make_unique<SameBoySystem>(
        /*id*/ 1, std::move(cfg), std::move(rom));
    // Fresh cart → fresh translator state (play flags, MI.OUT framing); the
    // render thread rebuilds it (and re-arms serial-out capture) next block.
    state->sync = {};
    if (self.renderResourcesAllocated) {
        state->system->onActivate(state->sampleRate);
        state->system->enableStateSnapshot();
    }
    return YES;
}

- (BOOL)loadRomData:(NSData*)rom
               sram:(NSData*)sram
              state:(NSData*)stateData
              error:(NSError**)error {
    if (rom.length == 0) {
        // SameBoySystem::onActivate silently refuses an empty ROM; surface it.
        if (error) *error = bridgeError(RPCoreBridgeErrorEmptyRom, @"ROM data is empty.");
        return NO;
    }
    SameBoyConfig cfg = templateConfig(_state->nextConfig);
    if (sram.length > 0) {
        const auto* p = static_cast<const std::uint8_t*>(sram.bytes);
        cfg.sram.assign(p, p + sram.length);
    }
    if (stateData.length > 0) {
        const auto* p = static_cast<const std::uint8_t*>(stateData.bytes);
        cfg.savestate.assign(p, p + stateData.length);
    }
    const auto* p = static_cast<const std::uint8_t*>(rom.bytes);
    return [self rp_swapSystemWithConfig:std::move(cfg)
                                     rom:std::vector<std::uint8_t>(p, p + rom.length)
                                   error:error];
}

- (BOOL)loadEmbeddedMGBWithSram:(NSData*)sram error:(NSError**)error {
    SameBoyConfig cfg = templateConfig(_state->nextConfig);
    cfg.embeddedRom = "mgb";
    if (sram.length > 0) {
        const auto* p = static_cast<const std::uint8_t*>(sram.bytes);
        cfg.sram.assign(p, p + sram.length);
    }
    const auto rom = rp::embeddedMgbRom();
    return [self rp_swapSystemWithConfig:std::move(cfg)
                                     rom:std::vector<std::uint8_t>(rom.begin(), rom.end())
                                   error:error];
}

- (NSData*)saveSram {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RenderState* state = _state.get();
    if (!state->system) return nil;
    BypassGate gate(state);
    if (!gate.acquired()) return nil;
    const auto bytes = state->system->saveSramBytes();
    if (bytes.empty()) return nil;
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

- (NSData*)saveState {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RenderState* state = _state.get();
    if (!state->system) return nil;
    BypassGate gate(state);
    if (!gate.acquired()) return nil;
    const auto bytes = state->system->saveStateBytes();
    if (bytes.empty()) return nil;
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

- (BOOL)loadState:(NSData*)stateData error:(NSError**)error {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RenderState* state = _state.get();
    if (!state->system) {
        if (error) *error = bridgeError(RPCoreBridgeErrorNoSystem, @"No emulator is running.");
        return NO;
    }
    BypassGate gate(state);
    if (!gate.acquired()) {
        if (error) *error = bridgeError(RPCoreBridgeErrorGateTimeout,
                                        @"The audio render thread did not yield.");
        return NO;
    }
    const auto* p = static_cast<const std::uint8_t*>(stateData.bytes);
    std::vector<std::uint8_t> bytes(p, p + stateData.length);
    if (!state->system->loadStateBytes(bytes)) {
        if (error) *error = bridgeError(RPCoreBridgeErrorStateRejected,
                                        @"Savestate rejected — wrong ROM or model.");
        return NO;
    }
    return YES;
}

- (BOOL)setModel:(RPSameBoyModel)model error:(NSError**)error {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RenderState* state = _state.get();
    state->nextConfig.model = static_cast<SameBoyModel>(model);
    if (!state->system) return YES; // applies at construction

    BypassGate gate(state);
    if (!gate.acquired()) {
        if (error) *error = bridgeError(RPCoreBridgeErrorGateTimeout,
                                        @"The audio render thread did not yield.");
        return NO;
    }
    state->system->config_.model = static_cast<SameBoyModel>(model);
    state->system->restartEmulator(); // SRAM survives; savestate cannot cross models
    return YES;
}

- (NSData*)snapshotSramForAutosave {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    SameBoySystem* sys = _state->system.get();
    if (!sys) return nil;
    std::vector<std::uint8_t> snapshot;
    if (!sys->readStateSnapshot(snapshot)) return nil;
    const auto& region =
        sys->stateRegions()[static_cast<std::size_t>(rp::MemoryType::Sram)];
    if (region.size == 0 ||
        static_cast<std::size_t>(region.offset) + region.size > snapshot.size()) {
        return nil;
    }
    return [NSData dataWithBytes:snapshot.data() + region.offset length:region.size];
}

- (void)pressButton:(RPGameboyButton)button down:(BOOL)down {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RtCommand cmd;
    cmd.op = RtCommand::kButton;
    cmd.a  = button;
    cmd.b  = down ? 1 : 0;
    _state->commands.tryPush(cmd); // drop-on-full: fine for input edges
}

- (void)resetEmulator {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    RtCommand cmd;
    cmd.op = RtCommand::kReset;
    _state->commands.tryPush(cmd);
}

- (void)setGainDb:(float)dB {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    _state->nextConfig.gainDb = dB;
    RtCommand cmd;
    cmd.op = RtCommand::kGainDb;
    cmd.f  = dB;
    _state->commands.tryPush(cmd);
}

- (void)setFastBoot:(BOOL)on {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    _state->nextConfig.fastBoot = on;
    RtCommand cmd;
    cmd.op = RtCommand::kFastBoot;
    cmd.b  = on ? 1 : 0;
    _state->commands.tryPush(cmd);
}

// The sync setters go through the parameter tree (not straight to the
// atomics) so a host observing the parameters sees the app-driven changes.
- (void)setMidiSyncMode:(RPMidiSyncMode)mode {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    [_parameterTree parameterWithAddress:kParamSyncMode].value = mode;
}

- (void)setSyncTempoDivisor:(NSUInteger)divisor {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    [_parameterTree parameterWithAddress:kParamTempoDivisor].value = (AUValue)divisor;
}

- (void)setSyncAutoStart:(BOOL)on {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    [_parameterTree parameterWithAddress:kParamAutoStart].value = on ? 1 : 0;
}

- (BOOL)copyFrameInto:(uint32_t*)dst capacityPixels:(NSUInteger)capacity {
    NSCAssert(NSThread.isMainThread, @"CoreBridge is main-thread-only");
    SameBoySystem* sys = _state->system.get();
    if (!sys) return NO;
    return sys->framebuffer()->readInto(dst, (std::uint32_t)capacity);
}

@end
