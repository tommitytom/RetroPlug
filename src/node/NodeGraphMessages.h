#pragma once

#include <variant>
#include "MessageBus.h"
#include "NodeProcessor.h"

namespace rp {
	struct AddNodeMessage {
		NodeProcessorPtr node;
	};

	struct RemoveNodeMessage {
		size_t index = 0;
		NodeProcessorPtr returnNode;
	};

	struct ConnectNodesMessage {
		NodeIndex outputNode;
		size_t outputIdx;
		NodeIndex inputNode;
		size_t inputIdx;
	};

	struct DisconnectInputMessage {
		NodeIndex inputNode;
		size_t inputIdx;
	};

	struct SetOrderMessage {
		std::vector<NodeIndex> indices;
		std::vector<NodeIndex> returnIndices;
	};

	using NodeGraphMessage = std::variant<AddNodeMessage, RemoveNodeMessage, ConnectNodesMessage, SetOrderMessage, DisconnectInputMessage>;
}
