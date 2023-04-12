#pragma once

#include "audio/MidiMessage.h"
#include "core/Forward.h"
#include "core/System.h"
#include "foundation/Event.h"

namespace rp {
	struct SystemServiceEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		SystemServiceType systemServiceType = INVALID_SYSTEM_SERVICE_TYPE;
		entt::any data;
	};

	struct SystemServiceDataEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		SystemServiceType systemServiceType = INVALID_SYSTEM_SERVICE_TYPE;
		void(*caller)(entt::any&, entt::any&) = nullptr;
		entt::any arg;
	};
	
	class SystemService :public fw::EventReceiver {
	private:
		SystemServiceType _type = INVALID_SYSTEM_TYPE;

	public:
		SystemService(SystemServiceType type) : _type(type) {}
		virtual ~SystemService() {}

		virtual void onBeforeLoad(LoadConfig& loadConfig) {}

		virtual void onAfterLoad(System& system) {}

		virtual void onBeforeProcess(System& system) {}

		virtual void onAfterProcess(System& system) {}

		virtual void onTransportChange(System& system, bool running) {}

		virtual void onMidi(System& system, const fw::MidiMessage& message) {}

		virtual void onMidiClock(System& system) {}

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

	class SystemServiceNode {
	protected:
		fw::EventNode::NodeId _targetNode = 0;
		SystemPtr _system;
		SystemServicePtr _service;

	public:
		SystemServiceNode(fw::EventNode::NodeId targetNode, SystemPtr system, SystemServicePtr service): 
			_targetNode(targetNode), _system(system), _service(service) {}
		~SystemServiceNode() {}

		template <typename EventT>
		void sendEvent(fw::EventNode& node, EventT&& ev) {
			node.send<SystemServiceEvent>(_targetNode, SystemServiceEvent{
				.systemId = _system->getId(),
				.systemServiceType = _service->getType(),
				.data = std::move(ev)
			});
		}

		void setSystem(SystemPtr system) {
			_system = system;
		}

		void setSystemService(SystemServicePtr service) {
			_service = service;
		}
		
		SystemPtr getSystem() {
			return _system;
		}
		
		SystemServicePtr getSystemService() {
			return _service;
		}

		fw::EventNode::NodeId getTargetNode() const {
			return _targetNode;
		}
	};

	using SystemServiceNodePtr = std::shared_ptr<SystemServiceNode>;

	template <typename T, auto Candidate>
	using FieldType = std::remove_reference<std::invoke_result_t<decltype(Candidate), T&>>;

	template <typename T, auto Candidate>
	void parameterSetter(entt::any& obj, entt::any& arg) {
		auto& settings = entt::any_cast<T&>(obj);
		auto& argData = entt::any_cast<typename FieldType<T, Candidate>::type&>(arg);
		settings.*Candidate = std::move(argData);
	}

	template <typename StateT>
	class TypedSystemServiceNode : public SystemServiceNode {
	public:
		StateT& getServiceState() {
			entt::any value = _service->getState();
			return entt::any_cast<StateT&>(value);
		}

		const StateT& getServiceState() const {
			return entt::any_cast<const StateT&>(_service->getState());
		}
		
		template <auto Candidate>
		void setField(fw::EventNode& node, FieldType<StateT, Candidate>::type&& data) {
			static_assert(std::is_member_object_pointer_v<decltype(Candidate)>);

			StateT& serviceState = getServiceState();
			serviceState.*Candidate = data;

			node.send<SystemServiceDataEvent>(getTargetNode(), SystemServiceDataEvent{
				.systemId = _system->getId(),
				.systemServiceType = _service->getType(),
				.caller = &parameterSetter<StateT, Candidate>,
				.arg = std::move(data)
			});
		}

		template <auto Candidate>
		void setField(fw::EventNode& node, const typename FieldType<StateT, Candidate>::type& data) {
			static_assert(std::is_member_object_pointer_v<decltype(Candidate)>);

			StateT& serviceState = getServiceState();
			serviceState.*Candidate = data;

			node.send<SystemServiceDataEvent>(getTargetNode(), SystemServiceDataEvent{
				.systemId = _system->getId(),
				.systemServiceType = _service->getType(),
				.caller = &parameterSetter<StateT, Candidate>,
				.arg = data
			});
		}
	};
}
