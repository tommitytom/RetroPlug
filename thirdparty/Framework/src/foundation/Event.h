#pragma once

#include <assert.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/core/any.hpp>
#include <moodycamel/concurrentqueue.h>
#include <moodycamel/blockingconcurrentqueue.h>

#include "foundation/TypeRegistry.h"

namespace orb {
	class EventNode {
	public:
		using NodeId = entt::id_type;
		using EventType = entt::id_type;
		using SubscriptionHandler = std::function<void(entt::any&)>;

	private:
		const size_t EVENTS_PER_UPDATE = 128;

		struct Event {
			enum class Kind {
				System,
				User
			};

			NodeId sender;
			Kind kind;
			entt::any value;
		};

		using Queue = moodycamel::BlockingConcurrentQueue<Event>;
		using QueuePtr = std::shared_ptr<Queue>;
		using QueueWeakPtr = std::weak_ptr<Queue>;

		struct NodeReference {
			std::string name; // For debugging only
			NodeId id;
			QueueWeakPtr queue;

			bool operator==(const NodeReference& other) const { return id == other.id; }
		};

		struct EventNodeState {
			std::unordered_map<NodeId, NodeReference> nodes;
			std::unordered_map<EventType, std::vector<NodeReference>> lookup;
		};

		struct AddNodeEvent { std::string name;  NodeId nodeId; QueueWeakPtr queue; };
		struct SubscribeEvent { EventType eventType; };
		struct UnsubscribeEvent { EventType eventType; };
		struct RemoveNodeEvent {};

		NodeId _id;
		std::string _name;

		QueuePtr _incoming;
		std::unordered_map<EventType, SubscriptionHandler> _subscriptions;

		EventNodeState _state;
		std::vector<Event> _incomingScratch;

		EventNode(const std::string& name, const EventNodeState& state) : _name(name), _state(state) {
			_id = entt::hashed_string{ _name.data() };
			assert(_state.nodes.contains(_id));

			_incoming = _state.nodes[_id].queue.lock();
			assert(_incoming);

			_incomingScratch.resize(EVENTS_PER_UPDATE);
		}

	public:
		EventNode(const std::string& name) : _name(name) {
			_id = entt::hashed_string{ _name.data() };
			_incoming = std::make_shared<Queue>();
			_incomingScratch.resize(EVENTS_PER_UPDATE);

			handleAddNode(NodeReference{ .name = name, .id = _id, .queue = _incoming });
		}

		EventNode(EventNode&& other) noexcept { *this = std::move(other); }
		EventNode(const EventNode&) = delete;

		~EventNode() {
			destroy();
		}

		EventNode spawn(const std::string& name) {
			const NodeId nodeId = entt::hashed_string{ name.c_str() };
			assert(!_state.nodes.contains(nodeId));

			const QueuePtr queue = std::make_shared<Queue>();

			handleAddNode(NodeReference{ .name = name, .id = nodeId, .queue = queue });
			broadcastSystem(AddNodeEvent{ .name = name, .nodeId = nodeId, .queue = queue });

			return EventNode(name, _state);
		}

		void destroy() {
			broadcastSystem<RemoveNodeEvent>();
			_id = 0;
			_name.clear();
			_subscriptions.clear();
			_state = EventNodeState();
			_incomingScratch.clear();
			_incoming = nullptr;
		}

		template <typename T, std::enable_if_t<std::is_empty_v<T>, bool> = true>
		EventType subscribe(std::function<void()>&& func) {
			const EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](entt::any& v) { func(); });
			return eventType;
		}

		template <typename T, std::enable_if_t<!std::is_empty_v<T>, bool> = true>
		EventType subscribe(std::function<void(const T&)>&& func) {
			const EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](entt::any& v) { func(entt::any_cast<const T&>(v)); });
			return eventType;
		}

		template <typename T, std::enable_if_t<std::is_empty_v<T>, bool> = true>
		EventType receive(std::function<void()>&& func) {
			const EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](entt::any& v) { func(); });
			return eventType;
		}

		template <typename T, std::enable_if_t<!std::is_empty_v<T>, bool> = true>
		EventType receive(std::function<void(T&&)>&& func) {
			const EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](entt::any& v) { func(std::move(entt::any_cast<T&>(v))); });
			return eventType;
		}

		/*template <typename T, typename Func>
		EventType subscribe(Func&& func) {
			EventType eventType = entt::type_id<T>().index();

			if constexpr (std::is_empty_v<T>) {
				subscribe(eventType, [func = std::move(func)](const entt::any& v) { func(); });
			} else {
				subscribe(eventType, [func = std::move(func)](const entt::any& v) { func(entt::any_cast<const T&>(v)); });
			}

			return eventType;
		}*/

		void unsubscribe(const EventType eventType) {
			assert(hasSubscription(eventType));

			handleUnsubscribe(_id, eventType);
			broadcastSystem(UnsubscribeEvent{ .eventType = eventType });

			_subscriptions.erase(eventType);
		}

		template <typename T>
		void unsubscribe() {
			unsubscribe(entt::type_id<T>().index());
		}

		// Add this public method to the EventNode class
		void unsubscribeAll() {
			// Collect all event types we're subscribed to
			std::vector<EventType> eventTypes;
			eventTypes.reserve(_subscriptions.size());

			for (const auto& [eventType, handler] : _subscriptions) {
				eventTypes.push_back(eventType);
			}

			// Unsubscribe from each event type
			for (EventType eventType : eventTypes) {
				handleUnsubscribe(_id, eventType);
				broadcastSystem(UnsubscribeEvent{ .eventType = eventType });
			}

			// Clear all subscriptions
			_subscriptions.clear();
		}

		template <typename T>
		void broadcast(const T& event, const bool includeSender = false) {
			const EventType eventType = entt::type_id<T>().index();
			const auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				for (const NodeReference& node : found->second) {
					if (node.id != _id || includeSender) {
						const QueuePtr queue = node.queue.lock();
						if (queue) {
							queue->enqueue(Event{
								.sender = _id,
								.kind = Event::Kind::User,
								.value = event
							});
						}
					}
				}
			}
		}

		template <typename T>
		void broadcast(T&& event, const bool includeSender = false) {
			const EventType eventType = entt::type_id<T>().index();
			const auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				const size_t subCount = found->second.size();
				for (size_t i = 0; i < subCount; ++i) {
					const NodeReference& node = found->second[i];

					if (node.id != _id || includeSender) {
						const QueuePtr queue = node.queue.lock();
						if (queue) {
							queue->enqueue(Event{
								.sender = _id,
								.kind = Event::Kind::User,
								.value = i < subCount - 1 ? entt::make_any<T>(event) : entt::forward_as_any(event)
							});
						}
					}
				}
			}
		}

		template <typename T>
		void broadcast(const bool includeSender = false) {
			const EventType eventType = entt::type_id<T>().index();
			const auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				for (const NodeReference& node : found->second) {
					if (node.id != _id || includeSender) {
						const QueuePtr queue = node.queue.lock();
						if (queue) {
							queue->enqueue(Event{
								.sender = _id,
								.kind = Event::Kind::User,
								.value = entt::make_any<T>()
							});
						}
					}
				}
			}
		}

		bool hasSubscribers(const EventType eventType) const {
			const auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				return found->second.size() > 0;
			}

			return false;
		}

		template <typename T>
		bool hasSubscribers() const {
			const EventType eventType = entt::type_id<T>().index();
			return hasSubscribers(eventType);
		}

		template <typename T>
		void send(const NodeId targetNodeId, const T& event) {
			assert(_state.nodes.contains(targetNodeId));

			const QueuePtr queue = _state.nodes[targetNodeId].queue.lock();
			if (queue) {
				queue->enqueue(Event{
					.sender = _id,
					.kind = Event::Kind::User,
					.value = event
				});
			}
		}

		template <typename T>
		bool trySend(const NodeId targetNodeId, T&& event) {
			auto found = _state.nodes.find(targetNodeId);
			if (found != _state.nodes.end()) {
				const QueuePtr queue = found->second.queue.lock();
				if (queue) {
					queue->enqueue(Event{
						.sender = _id,
						.kind = Event::Kind::User,
						.value = std::move(event)
					});

					return true;
				}
			}

			return false;
		}

		template <typename T>
		void send(const NodeId targetNodeId, T&& event) {
			const bool valid = trySend(targetNodeId, std::forward<T>(event));
			assert(valid);
		}

		template <typename T>
		void send(const NodeId targetNodeId) {
			assert(_state.nodes.contains(targetNodeId));

			const QueuePtr queue = _state.nodes[targetNodeId].queue.lock();
			if (queue) {
				queue->enqueue(Event{
					.sender = _id,
					.kind = Event::Kind::User,
					.value = entt::make_any<T>()
				});
			}
		}

		void update() {
			const size_t amount = _incoming->try_dequeue_bulk(_incomingScratch.data(), _incomingScratch.size());
			processIncoming(amount);
		}

		void wait(int64 timeout) {
			const size_t amount = _incoming->wait_dequeue_bulk_timed(_incomingScratch.data(), _incomingScratch.size(), timeout);
			processIncoming(amount);
		}

		inline bool hasSubscription(const EventType eventType) const {
			return _subscriptions.find(eventType) != _subscriptions.end();
		}

		template <typename T>
		inline bool hasSubscription() const {
			return hasSubscription(entt::type_id<T>().index());
		}

		NodeId getId() const {
			return _id;
		}

		const EventNodeState& getState() const {
			return _state;
		}

		EventNode& operator=(EventNode&& other) noexcept {
			_id = other._id;
			_name = std::move(other._name);
			_incoming = std::move(other._incoming);
			_state = std::move(other._state);
			_subscriptions = std::move(other._subscriptions);
			_incomingScratch = std::move(other._incomingScratch);

			other._id = 0;

			return *this;
		}

		EventNode& operator=(const EventNode&) = delete;

	private:
		void processIncoming(const size_t amount) {
			for (size_t i = 0; i < amount; ++i) {
				Event& ev = _incomingScratch[i];

				if (ev.kind == Event::Kind::User) {
					assert(_state.nodes.contains(ev.sender));

					const SubscriptionHandler* handler = findSubscriptionHandler(ev.value.type().index());

					if (handler) {
						(*handler)(ev.value);
					} else {
						//spdlog::warn("Node '{}' received a message it has not subscribed to: {}", _name, ev.value.type().name());
					}
				} else {
					processSystemEvent(ev);
				}
			}
		}

		void subscribe(const EventType eventType, SubscriptionHandler&& func) {
			assert(!hasSubscription(eventType));

			handleSubscribe(_id, eventType);
			broadcastSystem(SubscribeEvent{ .eventType = eventType });

			_subscriptions[eventType] = std::move(func);
		}

		template <typename T>
		void broadcastSystem(T&& ev) {
			for (const auto& [nodeId, node] : _state.nodes) {
				if (nodeId != _id) {
					const QueuePtr queue = node.queue.lock();
					if (queue) {
						queue->enqueue(Event{
							.sender = _id,
							.kind = Event::Kind::System,
							.value = ev
						});
					}
				}
			}
		}

		template <typename T>
		void broadcastSystem() {
			for (const auto& [nodeId, node] : _state.nodes) {
				if (nodeId != _id) {
					const QueuePtr queue = node.queue.lock();
					if (queue) {
						queue->enqueue(Event{
							.sender = _id,
							.kind = Event::Kind::System,
							.value = entt::make_any<T>()
						});
					}
				}
			}
		}

		void processSystemEvent(const Event& ev) {
			const EventType t = ev.value.type().index();

			if (t == entt::type_id<AddNodeEvent>().index()) {
				const AddNodeEvent& evt = entt::any_cast<const AddNodeEvent&>(ev.value);
				handleAddNode(NodeReference{ .name = evt.name, .id = evt.nodeId, .queue = evt.queue });
			}

			if (t == entt::type_id<SubscribeEvent>().index()) {
				const SubscribeEvent& evt = entt::any_cast<const SubscribeEvent&>(ev.value);
				handleSubscribe(ev.sender, evt.eventType);
			}

			if (t == entt::type_id<UnsubscribeEvent>().index()) {
				const UnsubscribeEvent& evt = entt::any_cast<const UnsubscribeEvent&>(ev.value);
				handleUnsubscribe(ev.sender, evt.eventType);
			}

			if (t == entt::type_id<RemoveNodeEvent>().index()) {
				handleRemoveNode(ev.sender);
			}
		}

		void handleAddNode(const NodeReference& node) {
			// NOTE: This may get called more than once, but there are no negative side effects.
			_state.nodes[node.id] = node;
		}

		void handleRemoveNode(const NodeId nodeId) {
			assert(_state.nodes.contains(nodeId));
			_state.nodes.erase(nodeId);

			// Remove subscriptions
			for (auto& [k, v] : _state.lookup) {
				const size_t idx = vectorIndexAt(v, NodeReference{ .id = nodeId });

				if (idx != -1) {
					v.erase(v.begin() + idx);
				}
			}
		}

		void handleSubscribe(const NodeId nodeId, const EventType eventType) {
			assert(_state.nodes.contains(nodeId));
			assert(!vectorContains(_state.lookup[eventType], _state.nodes[nodeId]));

			_state.lookup[eventType].push_back(_state.nodes[nodeId]);
		}

		void handleUnsubscribe(const NodeId nodeId, const EventType eventType) {
			assert(_state.nodes.contains(nodeId));
			std::vector<NodeReference>& nodes = _state.lookup[eventType];

			const size_t idx = vectorIndexAt(nodes, NodeReference{ .id = nodeId });
			assert(idx != -1);

			nodes.erase(nodes.begin() + idx);
		}

		inline const SubscriptionHandler* findSubscriptionHandler(const EventType eventType) const {
			const auto found = _subscriptions.find(eventType);
			if (found != _subscriptions.end()) {
				return &found->second;
			}

			return nullptr;
		}

		template <typename T>
		inline size_t vectorIndexAt(const std::vector<T>& vec, const T& item) const {
			for (size_t i = 0; i < vec.size(); ++i) {
				if (vec[i] == item) {
					return i;
				}
			}

			return -1;
		}

		template <typename T>
		inline bool vectorContains(const std::vector<T>& vec, const T& item) const {
			return vectorIndexAt(vec, item) != -1;
		}
	};

	class EventReceiver;

	class EventEmitter {
	private:
		EventNode& _node;
		EventNode::NodeId _targetNode;

		std::weak_ptr<EventReceiver> _target;

	public:
		EventEmitter(EventNode& node, std::string_view targetNode, std::weak_ptr<EventReceiver> target) : EventEmitter(node, entt::hashed_string(targetNode.data(), targetNode.size()), target) {}
		EventEmitter(EventNode& node, EventNode::NodeId targetNode, std::weak_ptr<EventReceiver> target): _node(node), _targetNode(targetNode), _target(target) {}
		~EventEmitter() = default;


		void emit() {
			assert(false);
			//_node.send(_targetNode)
		}
	};

	class EventReceiver {
	private:
		std::unordered_map<EventNode::EventType, EventNode::SubscriptionHandler> _subscriptions;

	public:
		void receiveEvent(entt::any& ev) {
			EventNode::EventType eventType = ev.type().index();

			auto found = _subscriptions.find(eventType);
			if (found != _subscriptions.end()) {
				found->second(ev);
			}
		}

	protected:
		template <typename T, std::enable_if_t<std::is_empty_v<T>, bool> = true>
		EventNode::EventType receive(std::function<void()>&& func) {
			EventNode::EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](entt::any& v) { func(); });
			return eventType;
		}

		template <typename T, std::enable_if_t<!std::is_empty_v<T>, bool> = true>
		EventNode::EventType receive(std::function<void(T&&)>&& func) {
			EventNode::EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](entt::any& v) { func(std::move(entt::any_cast<T&>(v))); });
			return eventType;
		}

	private:
		void subscribe(EventNode::EventType type, EventNode::SubscriptionHandler&& handler) {
			_subscriptions[type] = std::move(handler);
		}
	};

	struct Foo : public EventReceiver {
		int v = 0;
	};

	static void create() {
		//Foo obj;

		//EventNode node("Root");

		//EventEmitter emitter(node, "Audio", obj);
	}
}
