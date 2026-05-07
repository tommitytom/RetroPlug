#pragma once

#include <vector>
#include <entt/entity/fwd.hpp>

#include "foundation/Types.h"
#include "audio/AudioBuffer.h"

namespace rp {
	class AudioEffectBase;

	struct AudioEffectContext {
		std::vector<std::unique_ptr<AudioEffectBase>> effects;

		// Delete copy operations
		AudioEffectContext(const AudioEffectContext&) = delete;
		AudioEffectContext& operator=(const AudioEffectContext&) = delete;

		// Keep move operations (automatically generated)
		AudioEffectContext(AudioEffectContext&&) = default;
		AudioEffectContext& operator=(AudioEffectContext&&) = default;

		AudioEffectContext() = default;
		~AudioEffectContext() = default;
	};

	struct AudioProcessorComponent {
		AudioEffectBase* effect = nullptr;
	};
}
