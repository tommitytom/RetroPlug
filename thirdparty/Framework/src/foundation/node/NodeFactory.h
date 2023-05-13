#pragma once

#include <unordered_map>
#include "NodeState.h"
#include "TypedNode.h"
#include "Node.h"

namespace fw {
	struct NodeDesc;

	using NodeProcFunc = void(*)(std::byte*);
	using InputSetterFunc = std::function<void(const void*)>;
	using AllocNodeFunc = std::function<NodePtr()>;
	using AllocStateFunc = std::function<void(std::byte*, NodeDesc&)>;

	struct NodeTypeInfo {
		struct Input { std::string name; entt::id_type type; };
		struct Output { std::string name; entt::id_type type; };

		NodeType type = 0;
		std::string_view typeName;
		size_t stateSize = 0;
		NodeProcFunc process = nullptr;
		NodeProcFunc destroy = nullptr;
		AllocStateFunc allocState = nullptr;
		AllocNodeFunc allocNode = nullptr;

		std::vector<Input> inputs;
		std::vector<Output> outputs;
	};

	struct NodeDesc {
		entt::id_type type = entt::null;
		std::string_view typeName;
		NodeStateBase* data = nullptr;

		NodeProcFunc process = nullptr;
		NodeProcFunc destroy = nullptr;

		std::vector<const void*> outputPointers;
		std::vector<InputSetterFunc> inputSetters;
	};

	template <typename T>
	void makeAllocState(std::byte* data, NodeDesc& desc) {
		NodeState<T>* node = new (data) NodeState<T>();

		desc.typeName = entt::type_name<T>::value();

		for_each(refl::reflect<typename T::Input>().members, [&](auto member) {
			desc.inputSetters.push_back([node](const void* data) {
				using MemberType = decltype(member);
				node->input().set<MemberType>(reinterpret_cast<const MemberType::value_type*>(data));
			});
		});

		for_each(refl::reflect<typename T::Output>().members, [&](auto member) {
			desc.outputPointers.push_back(reinterpret_cast<const void*>(&member(node->output())));
		});
	}

	template <typename T>
	NodePtr makeNode() {
		return std::make_shared<TypedNode<T>>();
	}

	template <typename T, auto Func>
	void makeProcess(std::byte* data) {
		Func(*reinterpret_cast<NodeState<T>*>(data));
	}

	template <typename T>
	void makeDestroy(std::byte* data) {
		//reinterpret_cast<NodeState<T>*>(data)->~NodeState();
	}

	template <typename T>
	void makeInspector(entt::any& data) {
		FullNodeState<T>& state = entt::any_cast<FullNodeState<T>>(data);
		//state.input
		
	}
	
	class NodeFactory {
	private:
		std::unordered_map<NodeType, NodeTypeInfo> _nodeTypes;

	public:
		template <typename T, auto Func>
		void addNode() {
			constexpr NodeType type = entt::type_hash<T>::value();
			assert(!_nodeTypes.contains(type));

			_nodeTypes[type] = NodeTypeInfo{
				.type = type,
				.typeName = entt::type_name<T>::value(),
				.stateSize = sizeof(NodeState<T>),
				.process = makeProcess<T, Func>,
				.destroy = makeDestroy<T>,
				.allocState = makeAllocState<T>,
				.allocNode = makeNode<T>
			};
		}

		const NodeTypeInfo& getNodeInfo(NodeType type) {
			auto found = _nodeTypes.find(type);
			assert(found != _nodeTypes.end());
			return found->second;
		}

		template <typename T>
		const NodeTypeInfo& getNodeInfo() {
			return getNodeInfo(entt::type_hash<T>::value());
		}
	};
}
