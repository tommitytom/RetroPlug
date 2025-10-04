#pragma once

#include "Components.h"
#include "AudioEffect.h"

namespace rp {
	struct RetroPlugComponent {
		f32 frequency = 440.0f;
		f32 amplitude = 0.1f;
	};

	struct RetroPlugStateComponent {
		f32 phase = 0.0f;
	};

	class RetroPlugGenerator : public AudioGenerator<RetroPlugComponent, RetroPlugStateComponent> {
	public:
		void process(fw::AudioBuffer& out, const RetroPlugComponent& comp, RetroPlugStateComponent&) override;
	};
}
