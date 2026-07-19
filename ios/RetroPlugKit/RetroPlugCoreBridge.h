// CoreBridge — the control-plane surface SwiftUI talks to. Everything here is
// MAIN-THREAD-ONLY (asserted); the implementation routes each call to the
// render thread through one of two channels:
//
//   * a lock-free SPSC command ring for realtime-cheap ops (buttons, reset,
//     gain, fast-boot) — drained at the top of every render block;
//   * a "bypass gate" for heavy ops (ROM swap, model change, save/load
//     state & SRAM): the render block emits silence while the main thread
//     mutates the emulator directly.
//
// Video is read via the emulator's lock-free triple buffer and is safe to
// call at any time (returns NO before the first frame is published).
#import <RetroPlugKit/RetroPlugAudioUnit.h>

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSErrorDomain const RPCoreBridgeErrorDomain;

typedef NS_ERROR_ENUM(RPCoreBridgeErrorDomain, RPCoreBridgeError) {
    RPCoreBridgeErrorEmptyRom      = 1, // ROM data was empty (would silently no-op)
    RPCoreBridgeErrorNoSystem      = 2, // no emulator constructed yet
    RPCoreBridgeErrorGateTimeout   = 3, // render thread never yielded (should not happen)
    RPCoreBridgeErrorStateRejected = 4, // savestate refused (wrong ROM or model)
    RPCoreBridgeErrorSramRejected  = 5, // battery RAM refused (wrong size for the cart)
};

// Game Boy LCD geometry. Frames are XRGB8888; in memory each pixel is
// little-endian B,G,R,X (CGBitmapInfo: byteOrder32Little | noneSkipFirst).
FOUNDATION_EXPORT const NSUInteger RPScreenWidth;   // 160
FOUNDATION_EXPORT const NSUInteger RPScreenHeight;  // 144

// Mirrors GameboyButton (system/InputTypes.hpp), which matches SameBoy's
// GB_key_t — pass through without translation.
typedef NS_ENUM(uint8_t, RPGameboyButton) {
    RPGameboyButtonRight  = 0,
    RPGameboyButtonLeft   = 1,
    RPGameboyButtonUp     = 2,
    RPGameboyButtonDown   = 3,
    // Single-letter cases import to Swift as uppercase initialisms (.A/.B)
    // without an explicit name, breaking the lowercase convention of the rest.
    RPGameboyButtonA      NS_SWIFT_NAME(a) = 4,
    RPGameboyButtonB      NS_SWIFT_NAME(b) = 5,
    RPGameboyButtonSelect = 6,
    RPGameboyButtonStart  = 7,
};

// How host MIDI reaching the render block is translated for the Game Boy.
// Full parity with the desktop lsdj-sync role's active modes
// (packages/retroplug/src/dspRoles.ts) — the only desktop mode without an iOS
// twin is `keyboard`, which is a later phase on desktop too (it needs a host
// key feed). Raw values are shared with the render thread and the AU
// parameter tree — keep them stable.
typedef NS_ENUM(uint8_t, RPMidiSyncMode) {
    RPMidiSyncModeOff      = 0, // host MIDI is dropped
    // Forward every MIDI byte verbatim over the link port — mGB parses MIDI
    // itself (the desktop `mgb` role / lsdj `midiPassthrough` mode). Default.
    RPMidiSyncModeMgb      = 1,
    // LSDj "MIDI" sync mode as slave: the host's transport/tempo is walked at
    // 24 PPQN (÷ tempo divisor) and each tick lands as an 0xF8 clock byte on
    // the link port (desktop `midiSync` mode). Notes are not forwarded. When
    // the host provides no musical context, incoming MIDI realtime clock
    // (0xF8) bytes are forwarded instead.
    RPMidiSyncModeMidiSync = 2,
    // Arduinoboy slave (SYNC=Lsdj): notes 24/25 arm/disarm the clock, 26-29
    // pick the divisor, 30+ push a raw row byte; host transport edges are
    // bookended with 0xFA/0xFC and 0xF8 flows only while armed.
    RPMidiSyncModeMidiSyncArduinoboy = 3,
    // NoteOn → SONG-row jump byte (ch 1: note, ch 2: note+128); the matching
    // NoteOff sends the 0xFE handshake.
    RPMidiSyncModeMidiMap  = 4,
    // MIDI notes → LSDj PS/2 keyboard scancodes (SYNC=KEYBD), sliding the
    // octave cursor to track the incoming note.
    RPMidiSyncModeKeyboardMidi = 5,
    // Arduinoboy master / MI.OUT: LSDj's outgoing serial is captured, the
    // flag-framed protocol decoded, and the result emitted on the AU's MIDI
    // output (notes / CC / PC / realtime).
    RPMidiSyncModeMidiOut  = 6,
    // Master Sync (SYNC=LSDJ): LSDj self-clocks; each captured serial byte
    // becomes one 0xF8 on the AU's MIDI output (plus row NoteOn + 0xFA at run
    // start, 0xFC on the idle flood) so the host can follow LSDj's tempo.
    RPMidiSyncModeMasterSync = 7,
    // GB Note Out (APU tap): MIDI is derived from the emulated sound hardware
    // itself — channel triggers become NoteOn (with an explicit NoteOff for
    // the voice's previous note), frequency slides become pitch bend, and
    // envelope / pan / duty writes become CCs. Works with ANY LSDj build (or
    // any ROM at all) — no MI.OUT-patched ROM needed — because it reads what
    // the APU is told to play, not what the ROM chooses to transmit over
    // serial. Uses the MI.OUT per-voice note/CC channel assignments.
    RPMidiSyncModeNoteOut = 8,
};

// Per-mode MIDI channel assignments — the software twin of the Arduinoboy
// Editor's per-application channel settings (stored in EEPROM on the real
// hardware). Channels are 1-based (1–16) like the editor GUI. Raw values are
// AU parameter address offsets (kParamChannelBase + raw) — keep them stable.
typedef NS_ENUM(uint8_t, RPMidiChannelSetting) {
    // LSDj slave sync (midiSyncArduinoboy): the channel whose NoteOns carry
    // the note-24+ control protocol.
    RPMidiChannelSettingArduinoboySlave = 0,
    // LSDj master sync: the channel of the song-row NoteOn at run start.
    RPMidiChannelSettingMasterSync      = 1,
    // keyboardMidi: the channel whose notes become PS/2 scancodes.
    RPMidiChannelSettingKeyboard        = 2,
    // midiMap: NoteOns on this channel jump rows 0–127; the next channel up
    // carries rows 128–255.
    RPMidiChannelSettingMidiMap         = 3,
    // mGB: the input channel remapped to each of mGB's five fixed voices
    // (PU1/PU2/WAV/NOI/POLY = mGB channels 1–5). Unassigned channels drop.
    RPMidiChannelSettingMgbPu1          = 4,
    RPMidiChannelSettingMgbPu2          = 5,
    RPMidiChannelSettingMgbWav          = 6,
    RPMidiChannelSettingMgbNoi          = 7,
    RPMidiChannelSettingMgbPoly         = 8,
    // midiOut (MI.OUT): the output channel for each GB voice's notes (and
    // program changes), then for each voice's CCs — the editor's
    // "Note MIDI CH" / "CC MIDI CH" columns.
    RPMidiChannelSettingMidiOutNotePu1  = 9,
    RPMidiChannelSettingMidiOutNotePu2  = 10,
    RPMidiChannelSettingMidiOutNoteWav  = 11,
    RPMidiChannelSettingMidiOutNoteNoi  = 12,
    RPMidiChannelSettingMidiOutCcPu1    = 13,
    RPMidiChannelSettingMidiOutCcPu2    = 14,
    RPMidiChannelSettingMidiOutCcWav    = 15,
    RPMidiChannelSettingMidiOutCcNoi    = 16,
};
FOUNDATION_EXPORT const NSUInteger RPMidiChannelSettingCount; // 17

// MI.OUT CC matrix (the editor's "CC Mode" / "CC SCALING" / CC#0–6 grid),
// configured per GB voice (0=PU1, 1=PU2, 2=WAV, 3=NOI). Ported verbatim from
// the firmware's playCC.
typedef NS_ENUM(uint8_t, RPMidiOutCcMode) {
    // The whole 0–111 command value goes out on CC#0.
    RPMidiOutCcModeSingle = 0,
    // The value's high digit picks one of CC#0–6; the low nibble is the
    // value. The firmware factory default.
    RPMidiOutCcModeMulti  = 1,
};
FOUNDATION_EXPORT const NSUInteger RPMidiOutVoiceCount;    // 4 (PU1/PU2/WAV/NOI)
FOUNDATION_EXPORT const NSUInteger RPMidiOutCcNumberCount; // 7 CC#s per voice

// Mirrors SameBoyModel (system/sameboy/SameBoyConfig.hpp).
typedef NS_ENUM(uint32_t, RPSameBoyModel) {
    RPSameBoyModelAuto   = 0,
    RPSameBoyModelDmgB   = 1,  // Game Boy
    RPSameBoyModelMgb    = 2,  // Game Boy Pocket
    RPSameBoyModelSgb    = 3,  // Super Game Boy NTSC
    RPSameBoyModelSgbPal = 4,  // Super Game Boy PAL
    RPSameBoyModelSgb2   = 5,  // Super Game Boy 2
    RPSameBoyModelCgb0   = 6,  // Game Boy Color CPU-0
    RPSameBoyModelCgbA   = 7,  // Game Boy Color CPU-A
    RPSameBoyModelCgbB   = 8,  // Game Boy Color CPU-B
    RPSameBoyModelCgbC   = 9,  // Game Boy Color CPU-C
    RPSameBoyModelCgbD   = 10, // Game Boy Color CPU-D
    RPSameBoyModelCgbE   = 11, // Game Boy Color CPU-E
    RPSameBoyModelAgb    = 12, // Game Boy Advance
    RPSameBoyModelGbp    = 13, // Game Boy Player
};

@interface RetroPlugAudioUnit (CoreBridge)

// YES once an emulator exists (constructed at first render-resource
// allocation, or by one of the load methods).
@property (nonatomic, readonly) BOOL hasSystem;

// -- Heavy path (bypass gate; blocks up to one render quantum) --------------

// Swap in a new cartridge. `sram` seeds battery RAM, `state` a savestate
// (savestate's embedded SRAM wins when both are set). The current
// model/fast-boot/gain settings carry over.
- (BOOL)loadRomData:(NSData *)rom
               sram:(nullable NSData *)sram
              state:(nullable NSData *)state
              error:(NSError **)error;

// Swap in the embedded mGB ROM (the Game Boy MIDI synth). `sram` restores
// mGB's saved synth settings (it keeps them in cartridge RAM).
- (BOOL)loadEmbeddedMGBWithSram:(nullable NSData *)sram error:(NSError **)error;

// Exact battery-RAM / savestate capture of the live emulator. nil when no
// system, no battery on the cart, or the gate timed out.
- (nullable NSData *)saveSram;
- (nullable NSData *)saveState;
- (BOOL)loadState:(NSData *)state error:(NSError **)error;

// Replace the running cartridge's battery RAM wholesale (a manual .sav load
// or an LSDj song-manager swap), then reset — LSDj (like most carts) only
// reads its save at boot. Rejected when no system is loaded or the image
// doesn't match the cartridge's battery size.
- (BOOL)loadSram:(NSData *)sram error:(NSError **)error;

// Rebuilds the emulator on the new model (SRAM survives, savestate cannot).
// Also becomes the default for subsequently loaded ROMs.
- (BOOL)setModel:(RPSameBoyModel)model error:(NSError **)error;

// -- Autosave path (never blocks) --------------------------------------------

// Battery RAM sliced from the render thread's periodic state snapshot; up to
// ~0.5 s stale. nil until the first snapshot publishes or when the cart has
// no battery. Use for opportunistic autosave; use -saveSram for exact saves.
- (nullable NSData *)snapshotSramForAutosave;

// -- Realtime path (lock-free ring; never blocks, drops when unrendered) ----

- (void)pressButton:(RPGameboyButton)button down:(BOOL)down NS_SWIFT_NAME(press(_:down:));
- (void)resetEmulator;
- (void)setGainDb:(float)dB;      // smoothed over ~20 ms, no clicks
- (void)setFastBoot:(BOOL)on;     // takes effect on next boot/reset

// -- MIDI sync (also exposed as AU parameters, so DAW hosts can set them) ----

// These route through the parameter tree (KVO-visible to hosts) into atomics
// the render block reads each quantum; they survive ROM/model swaps.
- (void)setMidiSyncMode:(RPMidiSyncMode)mode;
- (void)setSyncTempoDivisor:(NSUInteger)divisor; // 1–8; 24/divisor ticks per quarter
- (void)setSyncAutoStart:(BOOL)on;               // tap START on transport rise (midiSync)
- (void)setMidiChannel:(NSUInteger)channel       // 1–16, per the editor GUI
            forSetting:(RPMidiChannelSetting)setting;

// mGB base channel: 0 = use the five per-voice assignments (default); 1–12 =
// the voices sit contiguously at base..base+4, so each plugin instance can
// take its own channel block with one setting.
- (void)setMgbBaseChannel:(NSUInteger)base;

// MI.OUT CC matrix, per voice (0–3 = PU1/PU2/WAV/NOI). Scaling stretches the
// value to the full MIDI range (×8 in multi mode, /111×127 in single mode);
// unscaled passes LSDj's byte through untouched, firmware-style.
- (void)setMidiOutCcMode:(RPMidiOutCcMode)ccMode forVoice:(NSUInteger)voice;
- (void)setMidiOutCcScaling:(BOOL)scaled forVoice:(NSUInteger)voice;
- (void)setMidiOutCcNumber:(NSUInteger)cc        // 0–127
                   atIndex:(NSUInteger)index     // 0–6 (CC#0–6)
                  forVoice:(NSUInteger)voice;

// -- Video -------------------------------------------------------------------

// Copy the latest published frame (RPScreenWidth × RPScreenHeight XRGB8888
// pixels) into `dst`. Returns NO before the first frame or when `capacity`
// is too small. Tear-free; safe to call every display-link tick.
- (BOOL)copyFrameInto:(uint32_t *)dst
       capacityPixels:(NSUInteger)capacity NS_SWIFT_NAME(copyFrame(into:capacityPixels:));

@end

NS_ASSUME_NONNULL_END
