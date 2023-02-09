#pragma once

#include <entt/meta/utility.hpp>

#include "foundation/Event.h"
#include "core/Forward.h"
#include "core/System.h"
#include "ui/View.h"

namespace rp {
	class SystemOverlay : public fw::View {
	private:
		SystemPtr _system;
		SystemServicePtr _service;

	public:
		SystemOverlay() {
			setSizingPolicy(fw::SizingPolicy::FitToParent);
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
		
		SystemServicePtr getService() {
			return _service;
		}

		SystemServiceType getServiceType() {
			_service->getType();
		}
	};

	template <typename T, auto Candidate>
	using FieldType = std::remove_reference<std::invoke_result_t<decltype(Candidate), T&>>;

	template <typename T, auto Candidate>
	void parameterSetter(entt::any& obj, entt::any& arg) {
		auto& settings = entt::any_cast<T&>(obj);
		auto& argData = entt::any_cast<typename FieldType<T, Candidate>::type&>(arg);
		settings.*Candidate = std::move(argData);
	}

	template <typename T>
	class TypedSystemOverlay : public SystemOverlay {
	public:
		TypedSystemOverlay() {}

		T& getServiceState() {
			entt::any value = getService()->getState();
			return entt::any_cast<T&>(value);
		}

		const T& getServiceState() const {
			return entt::any_cast<const T&>(getService()->getState());
		}

		template <auto Candidate>
		void setField(FieldType<T, Candidate>::type&& data) {
			static_assert(std::is_member_object_pointer_v<decltype(Candidate)>);
			
			fw::EventNode& node = getState<fw::EventNode>();
			
			T& serviceState = getServiceState();
			serviceState.*Candidate = data;
			
			node.send<SystemServiceEvent>("Audio"_hs, SystemServiceEvent{
				.systemId = getSystem()->getId(),
				.systemServiceType = getService()->getType(),
				.caller = &parameterSetter<T, Candidate>,
				.arg = std::move(data)
			});
		}
	};
}
