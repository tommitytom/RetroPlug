#pragma once

#include <entt/meta/utility.hpp>

#include "foundation/Event.h"
#include "core/Forward.h"
#include "core/System.h"
#include "ui/View.h"

namespace rp {
	class SystemOverlay : public fw::View {
	private:
		SystemServiceNodePtr _node;

	public:
		SystemOverlay() {
			setSizingPolicy(fw::SizingPolicy::FitToParent);
		}
		
		void setNode(SystemServiceNodePtr node) {
			_node = node;
		}

		SystemServiceNodePtr getNode() {
			return _node;
		}
	};

	template <typename T>
	class TypedSystemOverlay : public SystemOverlay {
	public:
		TypedSystemOverlay() {}
		~TypedSystemOverlay() = default;

		T& getServiceState() {
			return getNode()->getSystemService()->getStateAs<T&>();
			//entt::any value = getService()->getState();
			//return entt::any_cast<T&>(value);
		}

		const T& getServiceState() const {
			return getNode()->getSystemService()->getStateAs<const T&>();
			//return entt::any_cast<const T&>(getService()->getState());
		}

		std::shared_ptr<TypedSystemServiceNode<T>> getServiceNode() {
			return std::static_pointer_cast<TypedSystemServiceNode<T>>(getNode());
		}

		template <auto Candidate>
		void setField(const typename FieldType<T, Candidate>::type& data) {
			getServiceNode()->template setField<Candidate>(getState<fw::EventNode>(), data);
		}

		template <auto Candidate>
		void setField(FieldType<T, Candidate>::type&& data) {
			getServiceNode()->template setField<Candidate>(getState<fw::EventNode>(), std::forward<FieldType<T, Candidate>::type>(data));
		}
	};
}
