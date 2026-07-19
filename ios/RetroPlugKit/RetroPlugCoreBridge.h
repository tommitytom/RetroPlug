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
};

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

// -- Video -------------------------------------------------------------------

// Copy the latest published frame (RPScreenWidth × RPScreenHeight XRGB8888
// pixels) into `dst`. Returns NO before the first frame or when `capacity`
// is too small. Tear-free; safe to call every display-link tick.
- (BOOL)copyFrameInto:(uint32_t *)dst
       capacityPixels:(NSUInteger)capacity NS_SWIFT_NAME(copyFrame(into:capacityPixels:));

@end

NS_ASSUME_NONNULL_END
