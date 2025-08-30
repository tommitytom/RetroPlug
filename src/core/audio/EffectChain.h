#pragma once

#include "core/audio/Effect.h"

namespace rp {
	class EffectChain {
	private:
		std::vector<std::shared_ptr<Effect>> _effects;
	public:
		void addEffect(std::shared_ptr<Effect> effect) {
			_effects.push_back(effect);
		}

		void removeEffect(std::shared_ptr<Effect> effect) {
			_effects.erase(std::remove(_effects.begin(), _effects.end(), effect), _effects.end());
		}

		void process(fw::AudioBuffer& buffer) {
			for (const auto& effect : _effects) {
				effect->process(buffer);
			}
		}
	};
}
