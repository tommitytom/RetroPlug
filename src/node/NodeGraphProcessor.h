#pragma once 

#include "NodeGraphMessages.h"

namespace rp {
	using NodeIndex = int32;

	class NodeGraphProcessor : public rp::NodeProcessor<NodeGraphMessage> {
	private:
		std::vector<NodeProcessorPtr> _nodes;
		std::vector<NodeIndex> _processOrder;
		bool _orderDirty = false;
		entt::registry _sharedState;

	public:
		NodeGraphProcessor() {
			_shared = &_sharedState;
		}

		void onProcess() override {
			handleMessageProcessing();
			processNodes();			
		}

		template <typename T>
		T& addState(T&& value = T()) {
			return _sharedState.ctx().emplace<T>(std::forward<T>(value));
		}

		std::vector<NodeProcessorPtr>& getNodes() {
			return _nodes;
		}

	protected:
		void processNodes() {
			// This will happen if incoming messages are not bundled before sending
			//assert(!_orderDirty);

			if (!_orderDirty) {
				for (NodeIndex idx : _processOrder) {
					_nodes[idx]->onProcess();
				}
			}
		}

		void handleMessageProcessing() {
			processMessages(overload{
				[&](AddNodeMessage& val) {
					_nodes.push_back(std::move(val.node));
					_nodes.back()->_shared = &_sharedState;
				},
				[&](RemoveNodeMessage& val) {
					val.returnNode = std::move(_nodes[val.index]);
					_nodes.erase(_nodes.begin() + val.index);
					_orderDirty = true;
				},
				[&](const ConnectNodesMessage& val) {
					connectNodes(val);
					_orderDirty = true;
				},
				[&](const DisconnectInputMessage& val) {
					disconnectInput(val);
					_orderDirty = true;
				},
				[&](SetOrderMessage& val) {
					val.returnIndices = std::move(_processOrder);
					_processOrder = std::move(val.indices);
					_orderDirty = false;
				},
			});
		}

	private:
		void connectNodes(const ConnectNodesMessage& val) {
			NodeProcessorBase* outputNode = _nodes[val.outputNode].get();
			NodeProcessorBase* inputNode = _nodes[val.inputNode].get();

			OutputBase& output = outputNode->_outputs[val.outputIdx];
			InputBase& input = inputNode->_inputs[val.inputIdx];

			input.data = &output.data;
		}

		void disconnectInput(const DisconnectInputMessage& val) {
			NodeProcessorBase* inputNode = _nodes[val.inputNode].get();
			InputBase& input = inputNode->_inputs[val.inputIdx];
			input.data = &input.defaultValue;
		}
	};

	using NodeGraphProcessorPtr = std::shared_ptr<NodeGraphProcessor>;
}
