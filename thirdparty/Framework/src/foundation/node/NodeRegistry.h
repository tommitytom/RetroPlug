#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "foundation/Types.h"

namespace fw {
	struct VisualLayer {};
	struct AudioLayer {};

	struct NodeDesc;

	using NodeProcFunc = void(*)(std::byte*);
	using InputSetterFunc = std::function<void(const void*)>;
	using AllocNodeFunc = std::function<NodePtr()>;
	using AllocStateFunc = std::function<void(std::byte*, NodeDesc&)>;

	class NodeRegistry {
	public:
		using LayerType = uint32;
		using NodeType = uint32;
		using TypeId = entt::id_type;

		using CreateFunc = void(*)(std::byte*);
		
		struct Layer {
			entt::id_type type;
			NodeProcFunc process = nullptr;
			NodeProcFunc destroy = nullptr;
			AllocStateFunc allocState = nullptr;
			AllocNodeFunc allocNode = nullptr;
			size_t stateSize = 0;
		};
		
		struct Node {
			struct Input { 
				std::string name; 
				entt::id_type type; 
				std::vector<entt::any> attribs;
			};
			
			struct Output { 
				std::string name; 
				entt::id_type type; 
			};
			
			NodeType type = 0;
			std::string name;
			std::string displayName;
			std::string category;
			std::string subCategory;
			std::string description;
			
			std::vector<Input> inputs;
			std::vector<Output> outputs;
			std::unordered_map<LayerType, Layer> _layers;
		};

	private:
		std::unordered_map<NodeType, Node> _nodes;

	public:
		class NodeFactory {
		private:
			Node& _data;

		public:
			NodeFactory(Node& data): _data(data) {}

			template <typename ...Attribs>
			Node::Input& addInput(std::string_view name, TypeId type, Attribs&&... attribs) {
				_data.inputs.push_back(Node::Input{ 
					std::string(name), 
					type,
					{ std::forward<Attribs>(attribs)... }
				});

				return _data.inputs.back();
			}

			Node::Output& addOutput(std::string_view name, TypeId type) {
				_data.outputs.push_back(Node::Output{ std::string(name), type });
				return _data.outputs.back();
			}

			void setCategory(std::string_view category) {
				_data.category = std::string(category);
			}

			void setSubCategory(std::string_view subCategory) {
				_data.subCategory = std::string(subCategory);
			}

			void setDescription(std::string_view description) {
				_data.description = std::string(description);
			}

			void setDisplayName(std::string_view displayName) {
				_data.displayName = std::string(displayName);
			}
			
			template <typename LayerT, typename NodeT, auto Func>
			void addLayer() {
				/*_data.layer[entt::type_hash<LayerT>::value()] = {
					.alloc = Func 
				};*/
			}
		};

		NodeFactory addNode(std::string_view name, NodeType type = 0) {
			if (type == 0) {
				type = entt::hashed_string{ name.data() };
			}

			assert(!_nodes.contains(type));
			
			Node& node = _nodes[type];
			node.name = name;
			node.type = type;
			
			return node;
		}
	};
}
