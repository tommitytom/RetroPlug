// The AUv3 <-> SameBoySystem bridge. Mirrors the shape of the desktop DPF
// plugin's run() (packages/native/plugin/PluginDSP.cpp): stage MIDI, zero the
// output lanes, drive the system's per-block triad via onProcess(), which SUMS
// stereo into the two lanes. Single system, no router — the degenerate path
// SystemBase::onProcess implements (finishBlock with laneCount = 2).
#import "RetroPlugAudioUnit.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "EmbeddedRoms.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/MidiTypes.hpp"

namespace {

constexpr AUAudioFrameCount kMaxFrames = 4096;
constexpr std::size_t       kMaxMidiPerBlock = 128;

// Everything the render block touches. Owned by the AU, captured by raw
// pointer in internalRenderBlock (the block must not retain self).
struct RenderState {
    std::unique_ptr<SameBoySystem> system;
    double sampleRate = 44100.0;
    // Scratch stereo lanes, so rendering is independent of the host's ABL
    // buffer layout (null mData, interleaved hosts, etc.).
    std::vector<float> laneL, laneR;
    ::MidiEvent midi[kMaxMidiPerBlock];
};

} // namespace

@implementation RetroPlugAudioUnit {
    std::unique_ptr<RenderState> _state;
    AUAudioUnitBus*      _outputBus;
    AUAudioUnitBusArray* _outputBusArray;
}

- (nullable instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                               options:(AudioComponentInstantiationOptions)options
                                                 error:(NSError**)outError {
    self = [super initWithComponentDescription:componentDescription options:options error:outError];
    if (self == nil) return nil;

    _state = std::make_unique<RenderState>();

    AVAudioFormat* format =
        [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100.0 channels:2];
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    if (_outputBus == nil) return nil;
    _outputBus.maximumChannelCount = 2;
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                             busType:AUAudioUnitBusTypeOutput
                                                              busses:@[ _outputBus ]];
    self.maximumFramesToRender = kMaxFrames;
    return self;
}

- (AUAudioUnitBusArray*)outputBusses {
    return _outputBusArray;
}

- (BOOL)allocateRenderResourcesAndReturnError:(NSError**)outError {
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;

    const double sr = _outputBus.format.sampleRate;
    _state->sampleRate = sr;
    _state->laneL.assign(kMaxFrames, 0.0f);
    _state->laneR.assign(kMaxFrames, 0.0f);

    // Build the one system: the embedded mGB ROM on a Game Boy Color core.
    // Mirrors SameBoyBackend::buildSameBoy for the embeddedRom="mgb" spec.
    SameBoyConfig cfg;
    cfg.embeddedRom = "mgb";
    const auto rom = rp::embeddedMgbRom();
    std::vector<std::uint8_t> romBytes(rom.begin(), rom.end());

    _state->system = std::make_unique<SameBoySystem>(
        /*id*/ 1, std::move(cfg), std::move(romBytes));
    _state->system->onActivate(sr);
    return YES;
}

- (void)deallocateRenderResources {
    if (_state->system) {
        _state->system->onDeactivate();
        _state->system.reset();
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
        (void)actionFlags; (void)outputBusNumber; (void)pullInputBlock;

        SameBoySystem* sys = state->system.get();
        if (sys == nullptr || frameCount > kMaxFrames) return kAudioUnitErr_Uninitialized;

        // --- MIDI from the host (mGB is note-driven; ch 1-4 = pu1/pu2/wav/noi)
        std::size_t midiCount = 0;
        for (const AURenderEvent* ev = realtimeEventListHead; ev != nullptr;
             ev = ev->head.next) {
            if (ev->head.eventType != AURenderEventMIDI &&
                ev->head.eventType != AURenderEventMIDISysEx) continue;
            const AUMIDIEvent& m = ev->MIDI;
            if (m.length == 0 || m.length > ::MidiEvent::kDataSize) continue;
            if (midiCount >= kMaxMidiPerBlock) break;

            ::MidiEvent& out = state->midi[midiCount++];
            const AUEventSampleTime offset =
                ev->head.eventSampleTime - (AUEventSampleTime)timestamp->mSampleTime;
            out.frame = (std::uint32_t)std::clamp<AUEventSampleTime>(offset, 0, frameCount - 1);
            out.size  = m.length;
            std::memcpy(out.data, m.data, m.length);
            out.dataExt = nullptr;
        }
        if (midiCount > 0) sys->onMidi(state->midi, (std::uint32_t)midiCount);

        // --- render: onProcess SUMS into the lanes, caller zeroes (SystemBase contract)
        std::memset(state->laneL.data(), 0, frameCount * sizeof(float));
        std::memset(state->laneR.data(), 0, frameCount * sizeof(float));
        float* lanes[2] = { state->laneL.data(), state->laneR.data() };

        AudioBlockInfo info;
        info.frames     = frameCount;
        info.sampleRate = state->sampleRate;
        sys->onProcess(info, lanes);

        // --- copy into the host's buffers (deinterleaved float32; mono falls back to L+R mix)
        const UInt32 chans = outputData->mNumberBuffers;
        for (UInt32 c = 0; c < chans; ++c) {
            AudioBuffer& buf = outputData->mBuffers[c];
            if (buf.mData == nullptr) buf.mData = lanes[std::min<UInt32>(c, 1)];
            else std::memcpy(buf.mData, lanes[std::min<UInt32>(c, 1)],
                             frameCount * sizeof(float));
            buf.mDataByteSize = frameCount * sizeof(float);
        }
        return noErr;
    };
}

@end
