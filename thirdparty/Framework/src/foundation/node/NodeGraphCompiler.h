#pragma once

#include "NodeGraph.h"
#include "NodeFactory.h"
#include "NodeProcessor.h"

namespace fw::NodeGraphCompiler {
	static void build(NodeGraph& graph, NodeFactory& factory, NodeProcessor& processor) {
		processor.reset(2048);
		
		for (auto node : graph.getNodes()) {
			const NodeTypeInfo& info = factory.getNodeInfo(node->getType());
			std::byte* data = processor.alloc(info.stateSize);

			NodeDesc nodeDesc {
				.type = info.type,
				.data = reinterpret_cast<NodeStateBase*>(data),
				.process = info.process,
				.destroy = info.destroy
			};

			info.allocState(data, nodeDesc);

			processor.addNode(node->getIndex(), std::move(nodeDesc));
		}

		for (auto& conn : graph.getConnections()) {
			NodePtr inputNode = conn.inputNode.lock();
			NodePtr outputNode = conn.outputNode.lock();
			processor.connectNodes(outputNode->getIndex(), conn.outputIdx, inputNode->getIndex(), conn.inputIdx);
		}
	}

	/*static NodeProcessor build(NodeGraph& graph, NodeFactory& factory) {
		NodeProcessor proc(graph.calculateGraphSize());
		build(graph, factory, proc);
		return proc;
	}*/
}