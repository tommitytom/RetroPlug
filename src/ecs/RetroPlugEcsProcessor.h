#pragma once

#include <entt/entity/registry.hpp>

#include "audio/AudioProcessor.h"
#include "audio/AudioBuffer.h"

namespace rp {
	using SerializeFunction = std::function<void(fw::Uint8Buffer&)>;
	using DeserializeFunction = std::function<void(const fw::Uint8Buffer&)>;

	class RetroPlugEcsProcessor final : public fw::AudioProcessor {
	private:
		entt::registry _registry;
		SerializeFunction _serializeHook;
		DeserializeFunction _deserializeHook;

	public:
		RetroPlugEcsProcessor(fw::EventNode&& eventNode);
		~RetroPlugEcsProcessor();

		void setSerializeHook(SerializeFunction&& func) {
			_serializeHook = std::move(func);
		}

		void setDeserializeHook(DeserializeFunction&& func) {
			_deserializeHook = std::move(func);
		}

		void onBeginUpdate(uint32 frameCount) override;

		void onRender(f32* output, const f32* input, uint32 frameCount) override;

		void onRenderFull(fw::AudioBuffer& out, const fw::AudioBuffer& in);

		void onMidi(const fw::MidiMessage& message) override;

		void onTransportChange(bool playing) override;

		void onTransportUpdate(const fw::TimeInfo& timeInfo) override;

		void onSampleRateChange(f32 sampleRate) override;

		void onSerialize(fw::Uint8Buffer& target) override;

		void onDeserialize(const fw::Uint8Buffer& source) override;
	};
}
