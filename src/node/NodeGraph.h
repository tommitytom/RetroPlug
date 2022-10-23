#pragma once

#include <queue>
#include <variant>

#include "foundation/Types.h"
#include "foundation/DataBuffer.h"
#include "node/Node.h"
#include "NodeGraphProcessor.h"

namespace rp {
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

	class NodeGraphBase {
	public:
		struct Connection {
			NodePtr outputNode;
			size_t outputIdx;
			NodePtr inputNode;
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
	};

	using NodeGraphBasePtr = std::shared_ptr<NodeGraphBase>;

	template <typename Processor = NodeGraphProcessor>
	class NodeGraph : public rp::Node<Processor>, NodeGraphBase {
	private:
		entt::registry _sharedState;

		struct ConnectedNode {
			NodeBase* node = nullptr;
			size_t incomingCount = 0;
			std::vector<ConnectedNode*> outgoing;
		};

	public:
		template <typename T>
		std::shared_ptr<T> addNode() {
			std::shared_ptr<T> node = std::make_shared<T>();
			node->_index = (NodeIndex)_nodes.size();
			_nodes.push_back(node);

			NodeProcessorPtr processor = node->createProcessor();
			processor->onInitialize();

			this->sendMessage(AddNodeMessage {
				.node = processor
			});

			return node;
		}

		void connectNodes(NodePtr outputNode, size_t outputIdx, NodePtr inputNode, size_t inputIdx) {
			connectNodes(Connection { outputNode, outputIdx, inputNode, inputIdx });
		}

		void connectNodes(Connection connection) {
			assert(connection.inputIdx < connection.inputNode->_inputs.size());
			assert(connection.outputIdx < connection.outputNode->_outputs.size());

			OutputBase& output = connection.outputNode->_outputs[connection.outputIdx];
			InputBase& input = connection.inputNode->_inputs[connection.inputIdx];
			input.data = &output.data;

			_connections.push_back(connection);

			this->sendMessage(ConnectNodesMessage {
				.outputNode = connection.outputNode->getIndex(),
				.outputIdx = connection.outputIdx,
				.inputNode = connection.inputNode->getIndex(),
				.inputIdx = connection.inputIdx,
			});

			recalculateOrder();
		}

		void disconnectInput(NodePtr node, size_t inputIdx) {
			InputBase& input = node->_inputs[inputIdx];
			input.data = &input.defaultValue;

			for (size_t i = 0; i < _connections.size(); ++i) {
				if (_connections[i].inputNode == node && _connections[i].inputIdx) {
					_connections.erase(_connections.begin() + i);
					break;
				}
			}

			this->sendMessage(DisconnectInputMessage {
				.inputNode = node->getIndex(),
				.inputIdx = inputIdx
			});

			recalculateOrder();
		}

		void recalculateOrder() {
			_processOrder.clear();

			std::vector<ConnectedNode> nodes(_nodes.size());

			for (size_t i = 0; i < _nodes.size(); ++i) {
				nodes[i]= { .node = _nodes[i].get() };
			}

			for (Connection& conn : _connections) {
				ConnectedNode* inputNode = &nodes[conn.inputNode->getIndex()];
				ConnectedNode* outputNode = &nodes[conn.outputNode->getIndex()];
				inputNode->incomingCount++;
				outputNode->outgoing.push_back(inputNode);
			}

			std::queue<ConnectedNode*> orderQueue;
			for (ConnectedNode& node : nodes) {
				if (node.incomingCount == 0/*&& node.node->isAlwaysActive()*/) {
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

			this->sendMessage(SetOrderMessage {
				.indices = _processOrder
			});
		}

		void onProcess() override {
			for (NodePtr& node : _nodes) {
				node->onProcess();
			}
		}
	};
}
