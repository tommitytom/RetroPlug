#pragma once

#include <entt/entity/registry.hpp>
#include <yoga/Yoga.h>
#include "DocumentTypes.h"

namespace fw{
	struct DocumentState {
		DomElementHandle rootNode = entt::null;
	};
}

namespace fw::DocumentUtil {
	static void setup(entt::registry& reg, const DomElementHandle rootNode) {
		reg.ctx().emplace<DocumentState>(rootNode);
	}
	
	static DomElementHandle getRootNode(const entt::registry& reg) {
		return reg.ctx().at<DocumentState>().rootNode;
	}

	static DomElementHandle getNodeHandle(YGNodeRef node) {
		return static_cast<DomElementHandle>(reinterpret_cast<std::uintptr_t>(YGNodeGetContext(node)));
	}

	static DomElementHandle getParent(const entt::registry& reg, const DomElementHandle node) {
		YGNodeRef n = reg.get<YGNodeRef>(node);
		YGNodeRef parent = YGNodeGetParent(n);
		
		if (parent) {
			return getNodeHandle(parent);
		}

		return entt::null;
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
