#pragma once

#include "audio/MidiMessage.h"
#include "core/Forward.h"
#include "core/System.h"

namespace rp {
	struct SystemServiceEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		SystemServiceType systemServiceType = INVALID_SYSTEM_SERVICE_TYPE;
		void(*caller)(entt::any&, entt::any&) = nullptr;
		entt::any arg;
	};
	
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

		virtual void setState(const entt::any& data) {}

		virtual void setState(entt::any&& data) {}

		virtual const entt::any getState() const { return entt::any{}; }

		virtual entt::any getState() { return entt::any{}; }
		
		template <typename T>
		T& getStateAs() {
			entt::any state = getState();
			return entt::any_cast<T&>(state);
		}

		template <typename T>
		const T& getStateAs() const {
			return entt::any_cast<const T&>(getState());
		}

		SystemServiceType getType() const {
			return _type;
		}
	};

	using SystemServicePtr = std::shared_ptr<SystemService>;

	template <typename T>
	class TypedSystemService : public SystemService {
	private:
		T _state;
		
	public:
		TypedSystemService(SystemServiceType type) : SystemService(type) {}
		
		void setState(const entt::any& data) override {
			_state = entt::any_cast<const T&>(data);
		}

		void setState(entt::any&& data) override {
			_state = std::move(entt::any_cast<T&>(data));
		}

		const entt::any getState() const override {
			return entt::forward_as_any(_state);
		}

		entt::any getState() override {
			return entt::forward_as_any(_state);
		}

		T& getRawState() {
			return _state;
		}

		const T& getRawState() const {
			return _state;
		}
	};
}
