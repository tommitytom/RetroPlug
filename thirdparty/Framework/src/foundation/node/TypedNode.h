#pragma once 

#include <memory>
#include <variant>

#include <refl.hpp>
#include <entt/core/type_info.hpp>

#include "foundation/Types.h"
#include "Node.h"
#include "NodeState.h"

namespace fw {
	template <typename T>
	class TypedNode : public Node {
	public:
		TypedNode(): Node(entt::type_hash<T>::value(), get_display_name(refl::reflect<T>()), sizeof(NodeState<T>)) {
			for_each(refl::reflect<T::Input>().members, [&](auto member) {
				if constexpr (is_writable(member)) {
					//std::string fieldName = StringUtil::formatMemberName(get_display_name(member));
					this->addInput(get_display_name(member), entt::type_id<decltype(member)>().hash());
				}
			});

			for_each(refl::reflect<T::Output>().members, [&](auto member) {
				if constexpr (is_readable(member)) {
					//std::string fieldName = StringUtil::formatMemberName(get_display_name(member));
					this->addOutput(get_display_name(member), entt::type_id<decltype(member)>().hash());
				}
			});
		}
		
		~TypedNode() = default;
	};
}
