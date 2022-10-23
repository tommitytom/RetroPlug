#pragma once

#include "core/AudioState.h"
#include "core/Proxies.h"
#include "foundation/DataBuffer.h"
#include "SystemManager.h"

namespace rp {
	class AudioStreamSystem final : public System<AudioStreamSystem> {
	private:
		SystemType _targetType;

		// Circular buffer

	public:
		AudioStreamSystem(SystemId id): System<AudioStreamSystem>(id) {}

		void setDesc(SystemType targetType) {
			_targetType = targetType;
		}

		void process(uint32 frameCount) override {}
	};

	using AudioPlayerSystemPtr = std::shared_ptr<AudioStreamSystem>;
}
