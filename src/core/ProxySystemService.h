#pragma once

#include "core/SystemService.h"

namespace rp {
	class ProxySystemService final : public SystemService {
	private:
		entt::any _state;

	public:
		ProxySystemService(SystemServiceType type, const entt::any& state) : SystemService(type), _state(state) {}
		~ProxySystemService() {}

		void onBeforeLoad(LoadConfig& loadConfig) override {}

		void onAfterLoad(System& system) override {}

		void onBeforeProcess(System& system) override {}

		void onAfterProcess(System& system) override {}

		void onMidi(System& system, const fw::MidiMessage& message) override {}

		void setState(const entt::any& data) override { _state = data; }

		const entt::any getState() const override { return _state; }
	};

	using ProxySystemServicePtr = std::shared_ptr<ProxySystemService>;
}
