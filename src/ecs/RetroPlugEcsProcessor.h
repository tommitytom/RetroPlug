#pragma once

#include <entt/entity/registry.hpp>

#include "audio/AudioProcessor.h"

namespace rp {
	class RetroPlugEcsProcessor final : public fw::AudioProcessor {
	private:
		entt::registry _registry;

	public:
		RetroPlugEcsProcessor();
		~RetroPlugEcsProcessor();

		void onBeginUpdate(uint32 frameCount) override;

		void onRender(f32* output, const f32* input, uint32 frameCount) override;

		void onMidi(const fw::MidiMessage& message) override;

		void onTransportChange(bool playing) override;

		void onTransportUpdate(const fw::TimeInfo& timeInfo) override;

		void onSampleRateChange(f32 sampleRate) override;

		void onSerialize(fw::Uint8Buffer& target) override;

		void onDeserialize(const fw::Uint8Buffer& source) override;
	};
}
