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
#include <spdlog/spdlog.h>

namespace fw {
	class EventNode {
	public:
		using NodeId = entt::id_type;
		using EventType = entt::id_type;
		using SubscriptionHandler = std::function<void(const entt::any&)>;

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

		using Queue = moodycamel::ConcurrentQueue<Event>;
		using QueuePtr = std::shared_ptr<Queue>;

		struct NodeReference {
			NodeId id;
			QueuePtr queue;

			bool operator==(const NodeReference& other) const { return id == other.id; }
		};

		struct EventNodeState {
			std::unordered_map<NodeId, NodeReference> nodes;
			std::unordered_map<EventType, std::vector<NodeReference>> lookup;
		};

		struct AddNodeEvent { NodeId nodeId; QueuePtr queue; };
		struct SubscribeEvent { EventType eventType; };
		struct UnsubscribeEvent { EventType eventType; };
		struct RemoveNodeEvent {};

		NodeId _id;
		std::string _name;

		QueuePtr _incoming;
		std::unordered_map<EventType, SubscriptionHandler> _subscriptions;

		EventNodeState _state;

		std::vector<Event> _incomingScratch;

	public:
		EventNode(const std::string& name, const EventNodeState& state) : _name(name), _state(state) {
			_id = entt::hashed_string{ _name.data() };
			assert(_state.nodes.contains(_id));

			_incoming = _state.nodes[_id].queue;
			assert(_incoming);

			_incomingScratch.resize(EVENTS_PER_UPDATE);
		}

		EventNode(const std::string& name) : _name(name) {
			_id = entt::hashed_string{ _name.data() };
			_incoming = std::make_shared<Queue>();
			_incomingScratch.resize(EVENTS_PER_UPDATE);

			handleAddNode(NodeReference{ .id = _id, .queue = _incoming });
		}

		EventNode(EventNode&& other) noexcept { *this = std::move(other); }
		EventNode(const EventNode&) = delete;
		
		~EventNode() {
			destroy();
		}

		void destroy() {
			broadcastSystem<RemoveNodeEvent>();
			_id = 0;
			_name.clear();
			_incoming = nullptr;
			_subscriptions.clear();
			_state = EventNodeState();
			_incomingScratch.clear();
		}

		EventNode spawn(const std::string& name) {
			NodeId nodeId = entt::hashed_string{ name.c_str() };
			assert(!_state.nodes.contains(nodeId));

			QueuePtr queue = std::make_shared<Queue>();

			handleAddNode(NodeReference{ .id = nodeId, .queue = queue });
			broadcastSystem(AddNodeEvent{ .nodeId = nodeId, .queue = queue });

			return EventNode(name, _state);
		}
		
		void subscribe(EventType eventType, SubscriptionHandler&& func) {
			assert(!hasSubscription(eventType));

			handleSubscribe(_id, eventType);
			broadcastSystem(SubscribeEvent{ .eventType = eventType });
			
			_subscriptions[eventType] = std::move(func);
		}

		template <typename T, typename Func>
		EventType subscribe(Func&& func) {
			EventType eventType = entt::type_id<T>().index();

			if constexpr (std::is_empty_v<T>) {
				subscribe(eventType, [func = std::move(func)](const entt::any& v) { func(); });
			} else {
				subscribe(eventType, [func = std::move(func)](const entt::any& v) { func(entt::any_cast<const T&>(v)); });
			}
			
			return eventType;
		}

		void unsubscribe(EventType eventType) {
			assert(hasSubscription(eventType));

			handleUnsubscribe(_id, eventType);
			broadcastSystem(UnsubscribeEvent{ .eventType = eventType });

			_subscriptions.erase(eventType);
		}

		template <typename T>
		void unsubscribe() {
			unsubscribe(entt::type_id<T>().index());
		}

		template <typename T>
		void broadcast(const T& event, bool includeSender = false) {
			EventType eventType = entt::type_id<T>().index();
			auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				for (const NodeReference& node : found->second) {
					if (includeSender || node.id != _id) {
						node.queue->enqueue(Event{
							.sender = _id,
							.kind = Event::Kind::User,
							.value = event
						});
					}
				}
			}
		}

		/*template <typename T>
		void broadcast(T&& event, bool includeSender = false) {
			//static_cast<std::is_copy_constructible_v<T>>();

			EventType eventType = entt::type_id<T>().index();
			auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				size_t foundFirst = -1;

				for (size_t i = 0; i < found->second.size(); ++i) {
					const NodeReference& node = found->second[i];

					if (includeSender || node.id != _id) {
						if (foundFirst == -1) {
							foundFirst = i;
						} else {
							node.queue->enqueue(Event{
								.sender = _id,
								.kind = Event::Kind::User,
								.value = event
							});
						}
						
					}
				}

				if (foundFirst != -1) {
					found->second[foundFirst].queue->enqueue(Event{
						.sender = _id,
						.kind = Event::Kind::User,
						.value = std::move(event)
					});
				}
			}
		}*/

		template <typename T>
		void broadcast(bool includeSender = false) {
			EventType eventType = entt::type_id<T>().index();
			auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				for (const NodeReference& node : found->second) {
					if (includeSender || node.id != _id) {
						node.queue->enqueue(Event{
							.sender = _id,
							.kind = Event::Kind::User,
							.value = entt::make_any<T>()
						});
					}
				}
			}
		}

		bool hasSubscribers(EventType eventType) const {
			auto found = _state.lookup.find(eventType);

			if (found != _state.lookup.end()) {
				return found->second.size() > 0;
			}

			return false;
		}

		template <typename T>
		bool hasSubscribers() const {
			EventType eventType = entt::type_id<T>().index();
			return hasSubscribers(eventType);
		}


		template <typename T>
		void send(NodeId targetNodeId, const T& event) {
			assert(_state.nodes.contains(targetNodeId));

			_state.nodes[targetNodeId].queue->enqueue(Event{
				.sender = _id,
				.kind = Event::Kind::User,
				.value = event
			});
		}

		template <typename T>
		void send(NodeId targetNodeId, T&& event) {
			assert(_state.nodes.contains(targetNodeId));

			_state.nodes[targetNodeId].queue->enqueue(Event{
				.sender = _id,
				.kind = Event::Kind::User,
				.value = std::move(event)
			});
		}

		template <typename T>
		void send(NodeId targetNodeId) {
			assert(_state.nodes.contains(targetNodeId));

			_state.nodes[targetNodeId].queue->enqueue(Event{
				.sender = _id,
				.kind = Event::Kind::User,
				.value = entt::make_any<T>()
			});
		}

		void update() {
			size_t amount = _incoming->try_dequeue_bulk(_incomingScratch.data(), EVENTS_PER_UPDATE);

			for (size_t i = 0; i < amount; ++i) {
				const Event& ev = _incomingScratch[i];

				if (ev.kind == Event::Kind::User) {
					assert(_state.nodes.contains(ev.sender));

					const SubscriptionHandler* handler = getSubscriptionHandler(ev.value.type().index());

					if (handler) {
						(*handler)(ev.value);
					} else {
						spdlog::warn("Node received a message it has not subscribed to: {}", ev.value.type().name());
					}
				} else {
					processSystemEvent(ev);
				}
			}
		}

		bool hasSubscription(EventType eventType) const {
			return _subscriptions.find(eventType) != _subscriptions.end();
		}

		template <typename T>
		bool hasSubscription() const {
			return hasSubscription(entt::type_id<T>().index());
		}

		NodeId getId() const {
			return _id;
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
		template <typename T>
		void broadcastSystem(T&& ev) {
			for (const auto& [nodeId, node] : _state.nodes) {
				if (nodeId != _id) {
					node.queue->enqueue(Event{
						.sender = _id,
						.kind = Event::Kind::System,
						.value = ev
					});
				}
			}
		}

		template <typename T>
		void broadcastSystem() {
			for (const auto& [nodeId, node] : _state.nodes) {
				if (nodeId != _id) {
					node.queue->enqueue(Event{
						.sender = _id,
						.kind = Event::Kind::System,
						.value = entt::make_any<T>()
					});
				}
			}
		}

		void processSystemEvent(const Event& ev) {
			EventType t = ev.value.type().index();

			/*entt::any_visit(entt::overloaded{
				[&](const AddNodeEvent& evt) {
					handleAddNode(NodeReference{.id = evt.nodeId, .queue = evt.queue }); 
				},
				[&](const SubscribeEvent& evt) {
					handleSubscribe(ev.sender, evt.eventType);
				}
			}, myAny);*/

			if (t == entt::type_id<AddNodeEvent>().index()) {
				const AddNodeEvent& evt = entt::any_cast<const AddNodeEvent&>(ev.value);
				handleAddNode(NodeReference{ .id = evt.nodeId, .queue = evt.queue });
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

		void handleRemoveNode(NodeId nodeId) {
			assert(_state.nodes.contains(nodeId));
			_state.nodes.erase(nodeId);

			for (auto& [k, v] : _state.lookup) {
				size_t idx = vectorIndexAt(v, NodeReference{ .id = nodeId });
				
				if (idx != -1) { 
					v.erase(v.begin() + idx); 
				}
			}
		}

		void handleSubscribe(NodeId nodeId, EventType eventType) {
			assert(_state.nodes.contains(nodeId));
			assert(!vectorContains(_state.lookup[eventType], _state.nodes[nodeId]));

			_state.lookup[eventType].push_back(_state.nodes[nodeId]);
		}

		void handleUnsubscribe(NodeId nodeId, EventType eventType) {
			assert(_state.nodes.contains(nodeId));
			std::vector<NodeReference>& nodes = _state.lookup[eventType];

			size_t idx = vectorIndexAt(nodes, NodeReference{ .id = nodeId });
			assert(idx != -1);

			nodes.erase(nodes.begin() + idx);

			_state.nodes.erase(nodeId);
		}	

		inline const SubscriptionHandler* getSubscriptionHandler(EventType eventType) const {
			auto found = _subscriptions.find(eventType);
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
}
