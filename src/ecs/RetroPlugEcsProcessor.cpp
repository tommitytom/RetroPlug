#include "RetroPlugEcsProcessor.h"

#include "foundation/Replicator.h"
#include "audio/AudioBuffer.h"
#include "Components.h"
#include "AudioEffect.h"
#include "SineGenerator.h"

namespace rp {
	RetroPlugEcsProcessor::RetroPlugEcsProcessor() {
		AudioEffectContext& effectCtx = _registry.ctx().emplace<AudioEffectContext>();

		effectCtx.effects.push_back(std::make_unique<SineGenerator>());

		fw::EventNode& node = getEventNode();
		fw::Replicator::setupOwner(_registry, node);
		fw::Replicator::replicate<SineGenerator::ComponentType>(_registry);
	}

	RetroPlugEcsProcessor::~RetroPlugEcsProcessor() {
		fw::Replicator::shutdown(_registry);
	}

	void RetroPlugEcsProcessor::onTransportChange(bool playing) {
	}

	void RetroPlugEcsProcessor::onTransportUpdate(const fw::TimeInfo& timeInfo) {
	}

	void RetroPlugEcsProcessor::onBeginUpdate(uint32 frameCount) {
		fw::Replicator::beginUpdate(_registry);
		getEventNode().update();
		fw::Replicator::endUpdate(_registry);
	}

	void RetroPlugEcsProcessor::onRender(f32* output, const f32* input, uint32 frameCount) {
		AudioEffectContext& effectCtx = _registry.ctx().emplace<AudioEffectContext>();
		fw::AudioBuffer outBuffer(2, frameCount, getSampleRate());
		fw::AudioBuffer inBuffer;

		outBuffer.clear();

		if (input) {
			inBuffer.fromInterleaved(input, 2, frameCount);
		} else {
			inBuffer.resize(2, frameCount);
			inBuffer.clear();
		}

		for (auto& effect : effectCtx.effects) {
			effect->update(_registry);
		}

		for (const auto& [e, effect] : _registry.view<AudioProcessorComponent>().each()) {
			effect.effect->process(_registry, e, outBuffer, inBuffer);
		}

		outBuffer.toInterleaved(output, 2, frameCount);
	}

	void RetroPlugEcsProcessor::onMidi(const fw::MidiMessage& message) {
	}

	void RetroPlugEcsProcessor::onSampleRateChange(f32 sampleRate) {
	}

	void RetroPlugEcsProcessor::onSerialize(fw::Uint8Buffer& target) {
	}

	void RetroPlugEcsProcessor::onDeserialize(const fw::Uint8Buffer& source) {
		
	}
}
