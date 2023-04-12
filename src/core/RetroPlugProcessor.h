#pragma once

#include <vector>

#include <spdlog/spdlog.h>

#include "foundation/TypeRegistry.h"

#include "audio/AudioProcessor.h"
#include "audio/MidiMessage.h"

#include "core/Events.h"
#include "core/System.h"
#include "core/SystemManager.h"

namespace rp {
	class RetroPlugProcessor final : public fw::AudioProcessor {
	private:
		SystemManager _systemManager;

		ProjectState _projectState;
		GlobalConfig _config;

		const fw::TypeRegistry& _typeRegistry;
		const SystemFactory& _systemFactory;
		IoMessageBus& _ioMessageBus;

	public:
		RetroPlugProcessor(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus);
		~RetroPlugProcessor() {}

		void onRender(f32* output, const f32* input, uint32 frameCount) override;

		void onMidi(const fw::MidiMessage& message) override;

		void onTransportChange(bool playing) override;

		void onSampleRateChange(f32 sampleRate) override;

		void onSerialize(fw::Uint8Buffer& target) override;

		void onDeserialize(const fw::Uint8Buffer& source) override;
	};
}
