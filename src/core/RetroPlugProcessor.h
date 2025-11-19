#pragma once

#include <entt/entity/registry.hpp>

#include "audio/AudioProcessor.h"
#include "audio/AudioBuffer.h"

namespace rp {
	class RetroPlugProcessor final : public orb::AudioProcessor {
	private:
		entt::registry _registry;

	public:
		RetroPlugProcessor(orb::EventNode&& eventNode);
		~RetroPlugProcessor();

		void onBeginUpdate(uint32 frameCount) override;

		void onRender(f32* output, const f32* input, uint32 frameCount) override;

		void onRenderFull(orb::AudioBuffer& out, const orb::AudioBuffer& in);

		void onMidi(const orb::MidiMessage& message) override;

		void onTransportChange(bool playing) override;

		void onTransportUpdate(const orb::TimeInfo& timeInfo) override;

		void onSampleRateChange(f32 sampleRate) override;
	};
}
