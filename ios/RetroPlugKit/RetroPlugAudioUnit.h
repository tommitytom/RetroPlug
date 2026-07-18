// RetroPlugAudioUnit — AUv3 spike: hosts one SameBoySystem running the
// embedded mGB ROM (a Game Boy MIDI synth) and renders it through the
// standard AUAudioUnit render path. No UI, no project layer, no QuickJS —
// this is the minimal proof that the emulator core runs on iOS/iPadOS.
#import <AudioToolbox/AudioToolbox.h>
#import <AVFAudio/AVFAudio.h>

NS_ASSUME_NONNULL_BEGIN

@interface RetroPlugAudioUnit : AUAudioUnit
@end

NS_ASSUME_NONNULL_END
