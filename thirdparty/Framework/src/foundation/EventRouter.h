#pragma once

#include "Event.h"

namespace fw {
	class EventRouter;
	using RouterNodeId = entt::id_type;
	constexpr RouterNodeId INVALID_ROUTER_NODE_ID = -1;

	struct RouterEvent {
		RouterNodeId id = INVALID_ROUTER_NODE_ID;
		entt::any data;
	};

	struct RouterDataEvent {
		RouterNodeId id = INVALID_ROUTER_NODE_ID;
		void(*caller)(entt::any&, entt::any&) = nullptr;
		entt::any arg;
	};

	class EventRouterNode : public std::enable_shared_from_this<EventRouterNode> {
	protected:
		RouterNodeId _id = INVALID_ROUTER_NODE_ID;
		fw::EventNode& _node;
		fw::EventNode::NodeId _targetNode = 0;

	public:
		EventRouterNode(EventRouter& router): _node(router.getNode()) {
			router.add(this);
		}
		~EventRouterNode() {
			router.remove(this);
		}

		template <std::derived_from<EventRouterNode> T>
		std::shared_ptr<EventRouterNode> addChild() {
			std::shared_ptr<T> child = std::make_shared<T>(_node, _id);
		}

		template <typename EventT>
		void sendEvent(EventT&& ev) {
			assert(_node);
			_node.send<RouterEvent>(_targetNode, RouterEvent{
				.id = _id,
				.data = std::move(ev)
			});
		}

		RouterNodeId getNodeId() const {
			return _id;
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

	template <typename StateT>
	class TypedEventRouterNode : public EventRouterNode {
	private:
		StateT _state;

	public:
		template <auto Candidate>
		void set(typename FieldType<StateT, Candidate>::type&& data) {
			static_assert(std::is_member_object_pointer_v<decltype(Candidate)>);

			StateT& serviceState = getServiceState();
			serviceState.*Candidate = data;

			_node.send<RouterDataEvent>(_targetNode, RouterDataEvent{
				.id = _id,
				.caller = &parameterSetter<StateT, Candidate>,
				.arg = std::move(data)
			});
		}

		template <auto Candidate>
		void set(const typename FieldType<StateT, Candidate>::type& data) {
			static_assert(std::is_member_object_pointer_v<decltype(Candidate)>);

			StateT& serviceState = getServiceState();
			serviceState.*Candidate = data;

			_node.send<RouterDataEvent>(_targetNode, RouterDataEvent{
				.id = _id,
				.caller = &parameterSetter<StateT, Candidate>,
				.arg = data
			});
		}

		StateT& getState() {
			return _state;
		}

		const StateT& getState() const {
			return _state;
		}
	};

	class EventRouter {
	private:
		fw::EventNode& _node;
		std::vector<std::weak_ptr<EventRouterNode>> _nodes;

	public:
		EventRouter(fw::EventNode& node): _node(node) {}
		~EventRouter() = default;

		fw::EventNode& getNode() {
			return _node;
		}
	};
}

namespace fw {
	void test(fw::EventNode& node) {
		EventRouter router(node);
	}
}