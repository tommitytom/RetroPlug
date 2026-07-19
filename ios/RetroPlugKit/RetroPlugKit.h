// Umbrella header for RetroPlugKit — the iOS framework wrapping the
// emulator core (SameBoy + SystemBase) and the AUv3 audio unit. Linked by
// both the container app (in-process hosting) and the AUv3 extension.
#import <Foundation/Foundation.h>

FOUNDATION_EXPORT double RetroPlugKitVersionNumber;
FOUNDATION_EXPORT const unsigned char RetroPlugKitVersionString[];

#import <RetroPlugKit/RetroPlugAudioUnit.h>
#import <RetroPlugKit/RetroPlugCoreBridge.h>
