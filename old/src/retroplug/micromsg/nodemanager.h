#pragma once

#include <vector>
#include <map>

#include "node.h"
#include "caller.h"
#include "platform.h"

namespace micromsg {
	const int DEFAULT_SEND_QUEUE_CAPACITY = 100;

	template <typename NodeType>
	class NodeManager {
	private:
		Node<NodeType> _nodes[(int)NodeType::COUNT];
		Allocator _alloc;
		bool _active = false;

		HandlerLookups _handlers;

	public:
		NodeManager() {
			_handlers.requests.push_back(&requestError);
			_handlers.responses.push_back(&responseError);
			_handlers.names.push_back("Error");
		}

		Allocator* allocator() { return &_alloc; }

		template <typename T>
		void addCall(size_t maxCallCount = DEFAULT_SEND_QUEUE_CAPACITY) {
			_alloc.reserveChunks(sizeof(TypedEnvelope<typename T::Arg>), maxCallCount);

			if constexpr (!IsPushType<T>::value) {
				_alloc.reserveChunks(sizeof(TypedEnvelope<typename T::Return>), maxCallCount);
				_handlers.responseCount += maxCallCount;
			}

			mm_assert(_handlers.typeIds.find(TypeId<T>::get()) == _handlers.typeIds.end());
			_handlers.typeIds[TypeId<T>::get()] = _handlers.requests.size();

			_handlers.requests.push_back(&requestHandler<T>);
			if constexpr (IsPushType<T>::value) {
				_handlers.responses.push_back(&responseError);
			} else {
				_handlers.responses.push_back(&responseHandler<T>);
			}

#ifdef RTTI_ENABLED
			_handlers.names.push_back(typeid(T).name());
#endif
		}

		Node<NodeType>* createNode(NodeType nodeType, const std::vector<NodeType>& targets) {
			assert(!_active);
			Node<NodeType>& node = _getNode(nodeType);

			for (NodeType t : targets) {
				auto* target = &_nodes[(int)t];
				node.setTarget(t, target);
				target->setSource(nodeType, new RequestQueue(DEFAULT_SEND_QUEUE_CAPACITY));
			}

			return &node;
		}

		void start() {
			_alloc.commit();
			std::cout << "Response count " << _handlers.responseCount << std::endl;

			for (Node<NodeType>& node : _nodes) {
				assert(node.isValid());
				if (node.isValid()) {
					node.setActive();
				}
			}

			_active = true;
		}

	private:
		Node<NodeType>& _getNode(NodeType nodeType) {
			Node<NodeType>& node = _nodes[(int)nodeType];
			if (!node.isValid()) {
				node.setup(nodeType, &_alloc, &_handlers);
			}

			return node;
		}
	};
}
