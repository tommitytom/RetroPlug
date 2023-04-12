#pragma once

#include <memory>
#include <variant>
#include <vector>

#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>

#include "foundation/Types.h"
#include "foundation/Math.h"

namespace fw {
	using NodeType = entt::id_type;
	
	template <typename T> class NodeState;
	class Node;
	using NodePtr = std::shared_ptr<Node>;

	enum class NodeLifecycleState {
		Created,
		Initialized
	};

	using NodeIndex = int32;
	using NodePortIndex = int32;
	const NodeIndex INVALID_NODE_INDEX = -1;
	
	struct NodeInput {
		std::string name;
		uint32 type = 0;
	};

	struct NodeOutput {
		std::string name;
		std::vector<std::pair<std::weak_ptr<Node>, NodePortIndex>> targets;
		uint32 type = 0;
	};

	class Node {
	private:
		NodeIndex _index = INVALID_NODE_INDEX;
		std::string _name;
		std::vector<NodeInput> _inputs;
		std::vector<NodeOutput> _outputs;
		NodeLifecycleState _lifecycle = NodeLifecycleState::Created;
		bool _alwaysActive = false;
		fw::Point _position;
		size_t _dataSize = 0;
		NodeType _type = 0;

	public:
		Node() {}
		Node(NodeType type, std::string_view name, size_t dataSize): _type(type), _name(std::string(name)), _dataSize(dataSize) {}
		~Node() = default;

		NodeType getType() {
			return _type;
		}
		
		size_t getDataSize() const {
			return _dataSize;
		}

		void setIndex(NodeIndex index) {
			_index = index;
		}

		NodeIndex getIndex() const {
			return _index;
		}

		const std::string& getName() const {
			return _name;
		}

		Point getPosition() const {
			return _position;
		}

		void setPosition(Point pos) {
			_position = pos;
		}

		void setAlwaysActive(bool alwaysActive) {
			_alwaysActive = alwaysActive;
		}

		bool isAlwaysActive() const {
			return _alwaysActive;
		}

		/*NodeIndex getIndex() const {
			return _index;
		}*/

		std::vector<NodeInput>& getInputs() {
			return _inputs;
		}

		const std::vector<NodeInput>& getInputs() const {
			return _inputs;
		}

		std::vector<NodeOutput>& getOutputs() {
			return _outputs;
		}

		const std::vector<NodeOutput>& getOutputs() const {
			return _outputs;
		}

	protected:
		NodePortIndex addInput(std::string_view name, uint32 type) {
			_inputs.push_back(NodeInput{
				.name = std::string(name),
				.type = type
			});

			return static_cast<NodePortIndex>(_inputs.size() - 1);
		}

		NodePortIndex addOutput(std::string_view name, uint32 type) {
			_outputs.push_back(NodeOutput{
				.name = std::string(name),
				.type = type
			});

			return static_cast<NodePortIndex>(_outputs.size() - 1);
		}		
	};

	
}
