#pragma once

#include <yoga/Yoga.h>
#include "DocumentTypes.h"

namespace fw::DocumentUtil {
	static DomElementHandle getNodeHandle(YGNodeRef node) {
		return static_cast<DomElementHandle>(reinterpret_cast<std::uintptr_t>(YGNodeGetContext(node)));
	}
	
	template <typename Func>
	void each(entt::registry& reg, DomElementHandle node, Func&& f) {
		YGNodeRef n = reg.get<YGNodeRef>(node);
		uint32 childCount = YGNodeGetChildCount(n);

		for (uint32 i = 0; i < childCount; ++i) {
			f(getNodeHandle(YGNodeGetChild(n, i)));
		}
	}
}
