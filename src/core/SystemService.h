#pragma once

#include "audio/MidiMessage.h"
#include "core/Forward.h"
#include "core/System.h"

namespace rp {
	class SystemService {
	private:
		SystemServiceType _type = INVALID_SYSTEM_TYPE;

	public:
		SystemService(SystemServiceType type) : _type(type) {}
		virtual ~SystemService() {}

		virtual void onBeforeLoad(LoadConfig& loadConfig) {}

		virtual void onAfterLoad(System& system) {}

		virtual void onBeforeProcess(System& system) {}

		virtual void onAfterProcess(System& system) {}

		virtual void onMidi(System& system, const fw::MidiMessage& message) {}

		virtual void setState(const entt::any& data) = 0;

		virtual const entt::any getState() const = 0;

		SystemServiceType getType() const {
			return _type;
		}
	};

	using SystemServicePtr = std::shared_ptr<SystemService>;
}
