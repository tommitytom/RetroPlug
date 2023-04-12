#pragma once

#include <queue>
#include <variant>

#include "foundation/Types.h"
#include "foundation/DataBuffer.h"
#include "foundation/node/TypedNode.h"

namespace fw {
	template <typename T>
	static bool removeVectorElement(std::vector<T>& v, const T& item) {
		for (size_t i = 0; i < v.size(); ++i) {
			if (v[i] == item) {
				v.erase(v.begin() + i);
				return true;
			}
		}

		return false;
	}

	class NodeGraph : public Node {
	public:
		struct Connection {
			std::weak_ptr<Node> outputNode;
			size_t outputIdx;
			std::weak_ptr<Node> inputNode;
			size_t inputIdx;
		};
	protected:
		std::vector<NodePtr> _nodes;
		std::vector<Connection> _connections;
		std::vector<NodeIndex> _processOrder;

	public:
		std::vector<NodePtr>& getNodes() {
			return _nodes;
		}

		std::vector<Connection>& getConnections() {
			return _connections;
		}
	private:
		//entt::registry _sharedState;

		struct ConnectedNode {
			Node* node = nullptr;
			size_t incomingCount = 0;
			std::vector<ConnectedNode*> outgoing;
		};

	public:
		NodeGraph() {}
		NodeGraph(std::string_view name) : Node(entt::type_hash<NodeGraph>::value(), name, 0) {}

		NodePtr addNode(NodePtr node) {
			node->setIndex((NodeIndex)_nodes.size());
			_nodes.push_back(node);
			return node;
		}

		template <typename T>
		std::shared_ptr<TypedNode<T>> addNode() {
			std::shared_ptr<TypedNode<T>> node = std::make_shared<TypedNode<T>>();
			addNode(node);
			return node;
		}

		void connectNodes(NodePtr outputNode, size_t outputIdx, NodePtr inputNode, size_t inputIdx) {
			connectNodes(Connection { outputNode, outputIdx, inputNode, inputIdx });
		}

		void connectNodes(Connection connection) {
			auto inputNode = connection.inputNode.lock();
			auto outputNode = connection.outputNode.lock();

			assert(inputNode);
			assert(outputNode);

			assert(connection.inputIdx < inputNode->getInputs().size());
			assert(connection.outputIdx < outputNode->getOutputs().size());

			NodeOutput& output = outputNode->getOutputs()[connection.outputIdx];
			//NodeInput& input = inputNode->getInputs()[connection.inputIdx];
			//input.data = &output.data;

			output.targets.push_back(std::make_pair(inputNode, (NodePortIndex)connection.inputIdx));

			_connections.push_back(connection);

			recalculateOrder();
		}

		void disconnectInput(NodePtr node, size_t inputIdx) {
			NodeInput& input = node->getInputs()[inputIdx];
			//input.data = &input.defaultValue;

			for (size_t i = 0; i < _connections.size(); ++i) {
				if (_connections[i].inputNode.lock() == node && _connections[i].inputIdx) {
					_connections.erase(_connections.begin() + i);
					break;
				}
			}

			recalculateOrder();
		}

		size_t calculateGraphSize() {
			size_t size = 0;

			for (NodePtr node : _nodes) {
				size += node->getDataSize();
			}

			return size;
		}

		void recalculateOrder() {
			_processOrder.clear();

			std::vector<ConnectedNode> nodes(_nodes.size());

			for (size_t i = 0; i < _nodes.size(); ++i) {
				nodes[i] = { .node = _nodes[i].get() };
			}

			for (Connection& conn : _connections) {
				auto inputNode = conn.inputNode.lock();
				auto outputNode = conn.outputNode.lock();

				assert(inputNode);
				assert(outputNode);

				ConnectedNode* inputConn = &nodes[inputNode->getIndex()];
				ConnectedNode* outputConn = &nodes[outputNode->getIndex()];
				inputConn->incomingCount++;
				outputConn->outgoing.push_back(inputConn);
			}

			std::queue<ConnectedNode*> orderQueue;
			for (ConnectedNode& node : nodes) {
				//if (node.incomingCount == 0 && node.node->isAlwaysActive()) {
				if (node.incomingCount == 0) {
					orderQueue.push(&node);
				}
			}

			while (orderQueue.size()) {
				ConnectedNode* node = orderQueue.front();
				orderQueue.pop();

				_processOrder.push_back(node->node->getIndex());

				for (ConnectedNode* dependant : node->outgoing) {
					assert(dependant->incomingCount > 0);

					if (--dependant->incomingCount == 0) {
						orderQueue.push(dependant);
					}
				}
			}
		}
	};

	using NodeGraphPtr = std::shared_ptr<NodeGraph>;
}
