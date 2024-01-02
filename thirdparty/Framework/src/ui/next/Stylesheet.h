#pragma once

#include <filesystem>
#include <vector>
#include <entt/fwd.hpp>
#include <entt/entity/entity.hpp>
#include <entt/core/any.hpp>

#include "foundation/Types.h"
#include "DocumentTypes.h"
#include "Transitions.h"

namespace fw {
	enum class SelectorFlags {
		None = 0,
		ClassType = 1 << 0,
		Hover = 1 << 2,
		Activated = 1 << 3,
		FirstChild = 1 << 4,
		ElementTag = 1 << 5,
		Id = 1 << 6,
		_entt_enum_as_bitmask
	};

	enum class SelectorType {
		Unknown,
		IdSelector,
		TypeSelector,
		ClassSelector,
		PseudoClassSelector,
		PseudoElementSelector,
		AttributeSelector,
		Combinator,
	};

	

	struct Transition {
		struct State {
			TransitionTimingType type = TransitionTimingType::Linear;
			f32 pos = 0.0f;
			f32 duration = 1.0f;
		};

		DomElementHandle parent = entt::null;
		StyleHandle from = entt::null;
		StyleHandle to = entt::null;
		StyleHandle target = entt::null;
		PropertyTransitionFunc func;
		State state;
	};

	struct Selector {
		struct Item {
			SelectorType type = SelectorType::Unknown;
			std::string name;
		};

		char combinator = 0;
		std::vector<Item> items;

		//using ItemIterator = std::reverse_iterator<std::vector<Item>::iterator>;
		//using ConstItemIterator = std::reverse_iterator<std::vector<Item>::const_iterator>;
	};

	struct Specificity {
		size_t index = 0;
		int32 a = 0;
		int32 b = 0;
		int32 c = 0;

		bool operator==(const Specificity& v) const {
			return a == v.a && b == v.b && c == v.c && index == v.index;
		}
	};

	struct SelectorGroup {
		Specificity specificity;
		std::vector<Selector> selectors;
	};

	struct StylesheetRule {
		struct Property {
			std::string_view name;
			entt::any data;
			PropertySetFunc set;
		};

		std::vector<Property> properties;
	};

	struct Stylesheet {
		std::filesystem::path filePath;
		SelectorGroup selectorGroup;
		std::shared_ptr<StylesheetRule> rule;
	};
}
