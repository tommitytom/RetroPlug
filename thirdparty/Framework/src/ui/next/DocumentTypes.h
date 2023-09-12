#pragma once

#include <string>
#include <entt/core/enum.hpp>
#include <entt/entity/fwd.hpp>
#include "foundation/Math.h"

namespace fw {
	struct ElementTag {
		std::string tag;
	};

	struct WorldAreaComponent {
		RectF area;
	};

	using DomElementHandle = entt::entity;
	using StyleHandle = entt::entity;

	enum class EventFlag {
		Empty = 0,
		MouseButton = 1 << 0,
		MouseMove = 1 << 1,
		_entt_enum_as_bitmask
	};

	enum class FlexStyleFlag {
		Empty = 0,
		FlexDirection = 1 << 0,
		JustifyContent = 1 << 1,
		FlexAlignItems = 1 << 2,
		FlexAlignSelf = 1 << 3,
		FlexAlignContent = 1 << 4,
		LayoutDirection = 1 << 5,
		FlexWrap = 1 << 6,
		FlexGrow = 1 << 7,
		FlexShrink = 1 << 8,
		FlexBasis = 1 << 9,
		MinWidth = 1 << 10,
		MaxWidth = 1 << 11,
		MinHeight = 1 << 12,
		MaxHeight = 1 << 13,
		Width = 1 << 14,
		Height = 1 << 15,
		AspectRatio = 1 << 16,
		Position = 1 << 17,
		Padding = 1 << 18,
		Margin = 1 << 19,
		Border = 1 << 20,
		Overflow = 1 << 21,
		FlexPositionType = 1 << 22,
		_entt_enum_as_bitmask
	};
}
