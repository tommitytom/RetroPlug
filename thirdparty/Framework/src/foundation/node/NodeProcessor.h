#pragma once

#include <functional>

#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>

#include "NodeState.h"
#include "NodeFactory.h"

namespace fw {
	struct NodeProcessor {
	private:
		std::vector<std::byte> _data;
		std::vector<NodeDesc> _nodes;
		std::vector<size_t> _nodeLookup;

	public:
		NodeProcessor(size_t dataSize) {
			_data.reserve(dataSize);
		}

		~NodeProcessor() {
			clear();
		}

		void clear() {
			for (NodeDesc& node : _nodes) {
				node.destroy(node.data);
			}
			
			_data.clear();
			_nodes.clear();
			_nodeLookup.clear();
		}

		void reset(size_t dataSize) {
			clear();
			_data.reserve(dataSize);
		}

		void addNode(size_t idx, NodeDesc&& desc) {
			_nodes.push_back(std::move(desc));
			_nodeLookup.resize(idx + 1, 0);
			_nodeLookup[idx] = _nodes.size() - 1;
		}

		std::byte* alloc(size_t size) {
			const size_t offset = _data.size();
			assert(offset + size < _data.capacity());
			_data.resize(offset + size);
			return &_data[offset];
		}

		void connectNodes(size_t outputNodeIdx, size_t outputPort, size_t inputNodeIdx, size_t inputPort) {
			NodeDesc& outputNode = _nodes[_nodeLookup[outputNodeIdx]];
			NodeDesc& inputNode = _nodes[_nodeLookup[inputNodeIdx]];

			const void* outputPointer = outputNode.outputPointers[outputPort];
			InputSetterFunc setter = inputNode.inputSetters[outputPort];

			setter(outputPointer);
		}

		void process() {
			for (NodeDesc& node : _nodes) {
				node.process(node.data);
			}
		}

		template <typename T>
		NodeState<T>& getNode(size_t idx) {
			assert(idx < _nodeLookup.size());
			size_t offset = _nodeLookup[idx];
			assert(_nodes[offset].type == entt::type_hash<T>::value());
			return *reinterpret_cast<NodeState<T>*>(_nodes[offset].data);
		}

		template <typename T>
		const NodeState<T>& getNode(size_t idx) const {
			assert(idx < _nodeLookup.size());
			size_t offset = _nodeLookup[idx];
			assert(_nodes[offset].type == entt::type_hash<T>::value());
			return *reinterpret_cast<const NodeState<T>*>(_nodes[offset].data);
		}
	};
}
